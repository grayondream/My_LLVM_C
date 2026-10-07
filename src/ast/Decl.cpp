#include "Decl.h"
#include "LayoutBuilder.h"
#include "codegen/CodegenContext.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Constants.h"
#include "Mangle.h"
#include <functional>
#include <unordered_set>

static llvm::Constant* foldToConstant(CodegenContext& ctx, const FoldedValue& fv) {
    switch (fv.type) {
        case FoldedValue::INT:
            return llvm::ConstantInt::get(ctx.getLLVMType(new Type(TypeKind::Int32)), fv.intVal);
        case FoldedValue::DOUBLE:
            return llvm::ConstantFP::get(ctx.getLLVMType(new Type(TypeKind::Float64)), fv.doubleVal);
        case FoldedValue::CHAR:
            return llvm::ConstantInt::get(ctx.getLLVMType(new Type(TypeKind::Char)), fv.charVal);
        default:
            return nullptr;
    }
}

static Type* stripTypedefs(Type* type) {
    while (type && type->kind == TypeKind::Typedef) {
        type = static_cast<TypedefType*>(type)->aliasedType;
    }
    return type;
}

// Codegen helpers for brace initializers -------------------------------------

static void storeScalarInitializer(CodegenContext& ctx, llvm::Value* dest,
                                   llvm::Type* destLLVM, ExprAST& expr,
                                   Type* targetType) {
    llvm::Value* v = expr.codegen(ctx);
    if (!v) return;
    if (expr.isLValue) v = ctx.loadValue(v, expr.type);
    if (targetType) v = ctx.castValue(v, expr.type, ctx.getLLVMType(targetType));
    if (destLLVM) v = ctx.castValue(v, destLLVM);
    ctx.getBuilder().CreateStore(v, dest);
}

// Emit stores for `{a, b, ...}` into an already allocated aggregate `dest`.
static void emitAggregateInitializer(CodegenContext& ctx, llvm::Value* dest,
                                     Type* type, InitializerListExprAST* init) {
    type = stripTypedefs(type);
    if (!type || !init) return;

    auto& builder = ctx.getBuilder();
    llvm::Type* destLLVM = ctx.getLLVMType(type);
    if (!destLLVM) return;

    switch (type->kind) {
        case TypeKind::Array: {
            auto* at = static_cast<ArrayType*>(type);
            auto* i64 = llvm::Type::getInt64Ty(ctx.getContext());
            auto* i32 = llvm::Type::getInt32Ty(ctx.getContext());
            for (size_t i = 0; i < init->initializers.size() && (int)i < at->size; ++i) {
                auto& item = *init->initializers[i];
                llvm::Value* elemPtr = builder.CreateInBoundsGEP(
                    destLLVM, dest,
                    {llvm::ConstantInt::get(i64, 0), llvm::ConstantInt::get(i32, (int)i)},
                    "elemp");
                if (auto* nested = dynamic_cast<InitializerListExprAST*>(&item)) {
                    emitAggregateInitializer(ctx, elemPtr, at->elementType, nested);
                } else {
                    storeScalarInitializer(ctx, elemPtr, ctx.getLLVMType(at->elementType),
                                           item, at->elementType);
                }
            }
            break;
        }
        case TypeKind::Union: {
            auto* ut = static_cast<UnionType*>(type);
            if (ut->members.empty() || init->initializers.empty()) return;
            llvm::Value* memberPtr = builder.CreateStructGEP(destLLVM, dest, 0, "unioninit");
            Type* mtype = ut->members[0].type;
            auto& item = *init->initializers[0];
            if (auto* nested = dynamic_cast<InitializerListExprAST*>(&item)) {
                emitAggregateInitializer(ctx, memberPtr, mtype, nested);
            } else {
                storeScalarInitializer(ctx, memberPtr, ctx.getLLVMType(mtype), item, mtype);
            }
            break;
        }
        case TypeKind::Optional:
        case TypeKind::Result: {
            // P1-02 (TYP-13/14) DS4: {i1 flag, value[, error]} — GEP per
            // pseudo-field, recursing for nested brace lists.
            bool isOpt = type->kind == TypeKind::Optional;
            unsigned n = isOpt ? 2 : 3;
            for (unsigned i = 0; i < init->initializers.size() && i < n; ++i) {
                auto& item = *init->initializers[i];
                Type* ftype;
                if (i == 0) {
                    ftype = TypeContext::instance().getBool();
                } else if (isOpt) {
                    ftype = static_cast<OptionalType*>(type)->elementType;
                } else {
                    ftype = i == 1 ? static_cast<ResultType*>(type)->successType
                                   : static_cast<ResultType*>(type)->errorType;
                }
                llvm::Value* fptr = builder.CreateStructGEP(destLLVM, dest, i, "optresinit");
                if (auto* nested = dynamic_cast<InitializerListExprAST*>(&item)) {
                    emitAggregateInitializer(ctx, fptr, ftype, nested);
                } else {
                    storeScalarInitializer(ctx, fptr, ctx.getLLVMType(ftype), item, ftype);
                }
            }
            break;
        }
        case TypeKind::Struct:
        case TypeKind::Class: {
            std::vector<FieldInfo>* fields = nullptr;
            unsigned baseOffset = 0;
            if (type->kind == TypeKind::Struct) {
                fields = &static_cast<StructType*>(type)->fields;
            } else {
                auto* ct = static_cast<ClassType*>(type);
                fields = &ct->fields;
                if (!ct->baseClass.empty()) baseOffset = 1;
            }
            for (size_t i = 0; i < fields->size() && i < init->initializers.size(); ++i) {
                auto& item = *init->initializers[i];
                Type* ftype = (*fields)[i].type;
                llvm::Value* fptr = builder.CreateStructGEP(
                    destLLVM, dest, baseOffset + (unsigned)i, "fieldinit");
                if (auto* nested = dynamic_cast<InitializerListExprAST*>(&item)) {
                    emitAggregateInitializer(ctx, fptr, ftype, nested);
                } else {
                    storeScalarInitializer(ctx, fptr, ctx.getLLVMType(ftype), item, ftype);
                }
            }
            break;
        }
        default:
            // Scalar braced initializer: `int x = {5};`
            if (!init->initializers.empty()) {
                storeScalarInitializer(ctx, dest, destLLVM,
                                       *init->initializers.back(), type);
            }
            break;
    }
}

// Build a constant for a global brace initializer, or nullptr if it is not a
// constant expression (the caller then falls back to zero initialization).
static llvm::Constant* buildAggregateConstant(CodegenContext& ctx, Type* type,
                                              InitializerListExprAST* init) {
    type = stripTypedefs(type);
    if (!type || !init) return nullptr;

    auto constFor = [&](ExprAST& e, Type* target) -> llvm::Constant* {
        if (e.isLValue) return nullptr;
        llvm::Constant* c = llvm::dyn_cast_or_null<llvm::Constant>(e.codegen(ctx));
        if (!c) return nullptr;
        if (llvm::Type* lt = ctx.getLLVMType(target)) {
            c = llvm::dyn_cast_or_null<llvm::Constant>(ctx.castValue(c, e.type, lt));
        }
        return c;
    };

    switch (type->kind) {
        case TypeKind::Optional:
        case TypeKind::Result: {
            // P1-02 (TYP-13/14) DS4: global Optional/Result brace initializers.
            bool isOpt = type->kind == TypeKind::Optional;
            unsigned n = isOpt ? 2 : 3;
            auto* stTy = llvm::dyn_cast<llvm::StructType>(ctx.getLLVMType(type));
            if (!stTy) return nullptr;
            std::vector<llvm::Constant*> elems;
            for (unsigned i = 0; i < n; ++i) {
                llvm::Constant* c = nullptr;
                Type* ftype;
                if (i == 0) {
                    ftype = TypeContext::instance().getBool();
                } else if (isOpt) {
                    ftype = static_cast<OptionalType*>(type)->elementType;
                } else {
                    ftype = i == 1 ? static_cast<ResultType*>(type)->successType
                                   : static_cast<ResultType*>(type)->errorType;
                }
                if (i < init->initializers.size()) {
                    if (auto* nested = dynamic_cast<InitializerListExprAST*>(
                            init->initializers[i].get())) {
                        c = buildAggregateConstant(ctx, ftype, nested);
                    } else {
                        c = constFor(*init->initializers[i], ftype);
                    }
                }
                if (!c) c = llvm::Constant::getNullValue(stTy->getElementType(i));
                elems.push_back(c);
            }
            return llvm::ConstantStruct::get(stTy, elems);
        }
        case TypeKind::Array: {
            auto* at = static_cast<ArrayType*>(type);
            auto* arrTy = llvm::dyn_cast<llvm::ArrayType>(ctx.getLLVMType(type));
            if (!arrTy) return nullptr;
            llvm::Constant* zero = llvm::Constant::getNullValue(arrTy->getElementType());
            std::vector<llvm::Constant*> elems;
            for (int i = 0; i < at->size; ++i) {
                llvm::Constant* c = nullptr;
                if ((size_t)i < init->initializers.size()) {
                    if (auto* nested = dynamic_cast<InitializerListExprAST*>(init->initializers[i].get())) {
                        c = buildAggregateConstant(ctx, at->elementType, nested);
                    } else {
                        c = constFor(*init->initializers[i], at->elementType);
                    }
                }
                elems.push_back(c ? c : zero);
            }
            return llvm::ConstantArray::get(arrTy, elems);
        }
        case TypeKind::Struct:
        case TypeKind::Class: {
            std::vector<FieldInfo>* fields = nullptr;
            bool hasBase = false;
            if (type->kind == TypeKind::Struct) {
                fields = &static_cast<StructType*>(type)->fields;
            } else {
                auto* ct = static_cast<ClassType*>(type);
                fields = &ct->fields;
                hasBase = !ct->baseClass.empty();
            }
            auto* stTy = llvm::dyn_cast<llvm::StructType>(ctx.getLLVMType(type));
            if (!stTy) return nullptr;
            std::vector<llvm::Constant*> elems;
            size_t elemIdx = 0;
            if (hasBase) {
                elems.push_back(llvm::Constant::getNullValue(stTy->getElementType(elemIdx++)));
            }
            for (size_t i = 0; i < fields->size(); ++i) {
                llvm::Constant* c = nullptr;
                if (i < init->initializers.size()) {
                    if (auto* nested = dynamic_cast<InitializerListExprAST*>(init->initializers[i].get())) {
                        c = buildAggregateConstant(ctx, (*fields)[i].type, nested);
                    } else {
                        c = constFor(*init->initializers[i], (*fields)[i].type);
                    }
                }
                if (!c) c = llvm::Constant::getNullValue(stTy->getElementType(elemIdx));
                elems.push_back(c);
                ++elemIdx;
            }
            return llvm::ConstantStruct::get(stTy, elems);
        }
        case TypeKind::Union: {
            auto* ut = static_cast<UnionType*>(type);
            auto* stTy = llvm::dyn_cast<llvm::StructType>(ctx.getLLVMType(type));
            if (!stTy || ut->members.empty() || init->initializers.empty()) return nullptr;
            std::vector<llvm::Constant*> elems;
            llvm::Constant* first = nullptr;
            if (auto* nested = dynamic_cast<InitializerListExprAST*>(init->initializers[0].get())) {
                first = buildAggregateConstant(ctx, ut->members[0].type, nested);
            } else {
                first = constFor(*init->initializers[0], ut->members[0].type);
            }
            if (!first) first = llvm::Constant::getNullValue(stTy->getElementType(0));
            elems.push_back(first);
            for (unsigned i = 1; i < stTy->getNumElements(); ++i) {
                elems.push_back(llvm::Constant::getNullValue(stTy->getElementType(i)));
            }
            return llvm::ConstantStruct::get(stTy, elems);
        }
        default:
            if (!init->initializers.empty()) {
                return constFor(*init->initializers.back(), type);
            }
            return nullptr;
    }
}

llvm::Value* VarDeclAST::codegen(CodegenContext& ctx) {
    llvm::Type* llvmType = ctx.getLLVMType(type);
    
    if (ctx.isGlobalScope()) {
        llvm::Constant* initConstant = nullptr;
        if (isConstexpr && foldedValue) {
            initConstant = foldToConstant(ctx, *foldedValue);
        } else if (initExpr) {
            if (auto* initList = dynamic_cast<InitializerListExprAST*>(initExpr.get())) {
                initConstant = buildAggregateConstant(ctx, type, initList);
            } else {
                initConstant = llvm::dyn_cast_or_null<llvm::Constant>(initExpr->codegen(ctx));
            }
        }
        if (initConstant) {
            initConstant = llvm::dyn_cast_or_null<llvm::Constant>(
                ctx.castValue(initConstant, initExpr ? initExpr->type : nullptr, llvmType));
        }
        // 全局无初始化器时必须是零初始化**定义**：InitVal=nullptr 会让 LLVM
        // 视为 external 声明（@g = external global T），不分配存储，链接时
        // undefined reference。对齐 C 的 tentative definition 语义。
        if (!initConstant) {
            initConstant = llvm::Constant::getNullValue(llvmType);
        }
        
        llvm::GlobalVariable::LinkageTypes linkage = type->isConst 
            ? llvm::GlobalVariable::PrivateLinkage 
            : llvm::GlobalVariable::ExternalLinkage;
        llvm::GlobalVariable* global = new llvm::GlobalVariable(
            ctx.getModule(), llvmType, type->isConst, linkage, initConstant, name);
        ctx.declareVariable(name, global, type);
        return global;
    }
    
    llvm::AllocaInst* alloca = ctx.getBuilder().CreateAlloca(llvmType, nullptr, name);

    // TYP-12 D3: uninitialized slices are the empty slice {null, 0}.
    {
        Type* st = stripTypedefs(type);
        if (st && st->kind == TypeKind::Slice && !initExpr && !isConstexpr) {
            ctx.getBuilder().CreateStore(llvm::Constant::getNullValue(llvmType), alloca);
        }
    }
    
    if (isConstexpr && foldedValue) {
        llvm::Value* constVal = foldToConstant(ctx, *foldedValue);
        if (constVal) {
            constVal = ctx.castValue(constVal, llvmType);
            ctx.getBuilder().CreateStore(constVal, alloca);
        }
    } else if (initExpr) {
        if (auto* initList = dynamic_cast<InitializerListExprAST*>(initExpr.get())) {
            // Zero the whole object first so unspecified fields stay zero.
            Type* elem = stripTypedefs(type);
            if (elem && (elem->kind == TypeKind::Struct || elem->kind == TypeKind::Class ||
                         elem->kind == TypeKind::Union ||
                         elem->kind == TypeKind::Optional ||
                         elem->kind == TypeKind::Result)) {
                ctx.getBuilder().CreateStore(llvm::Constant::getNullValue(llvmType), alloca);
            }
            emitAggregateInitializer(ctx, alloca, type, initList);
        } else {
            llvm::Value* initVal = initExpr->codegen(ctx);
            if (initVal) {
                // An lvalue initializer denotes a location; load its value
                // (arrays decay to a pointer) before storing it.
                if (initExpr->isLValue) {
                    initVal = ctx.loadValue(initVal, initExpr->type);
                }
                initVal = ctx.castValue(initVal, initExpr->type, llvmType);
                // TYP-12: array initializer decays to a slice view.
                {
                    Type* it = stripTypedefs(initExpr->type);
                    Type* vt = stripTypedefs(type);
                    if (it && it->kind == TypeKind::Array && vt &&
                        vt->kind == TypeKind::Slice) {
                        initVal = ctx.emitArrayToSliceDecay(
                            static_cast<ArrayType*>(it), initVal);
                    }
                }
                ctx.getBuilder().CreateStore(initVal, alloca);
            }
        }
    }
    ctx.declareVariable(name, alloca, type);

    if (!sourceFile.empty()) {
        ctx.emitVariableDebug(alloca, name, type, sourceLine);
    }

    return alloca;
}

// P1-03 / GEN-05: 仅创建函数签名（无体）。TU codegen 预扫用——模板实例
// 函数追加在翻译单元尾部，其前方的调用点必须能解析到符号。
llvm::Value* FunctionDeclAST::codegenPrototype(CodegenContext& ctx) {
    llvm::Type* retType = ctx.getLLVMType(returnType);
    std::vector<llvm::Type*> paramTypes;
    for (auto& param : params) {
        paramTypes.push_back(ctx.getLLVMType(param->type));
    }
    std::vector<Type*> astParamTypes;
    for (auto& param : params) {
        astParamTypes.push_back(param->type);
    }
    std::string mangledName = mangleFunction(name, astParamTypes);

    llvm::Function* function = ctx.getModule().getFunction(mangledName);
    if (!function) {
        llvm::FunctionType* funcType = llvm::FunctionType::get(retType, paramTypes, isVarArg);
        function = llvm::Function::Create(
            funcType, llvm::Function::ExternalLinkage, mangledName, ctx.getModule());
    }
    return function;
}

llvm::Value* FunctionDeclAST::codegen(CodegenContext& ctx) {
    // 签名已由 codegenPrototype（TU 预扫）或本函数创建；复用已有声明。
    std::vector<Type*> astParamTypes;
    for (auto& param : params) {
        astParamTypes.push_back(param->type);
    }
    std::string mangledName = mangleFunction(name, astParamTypes);

    llvm::Function* function = ctx.getModule().getFunction(mangledName);
    if (!function) {
        function = static_cast<llvm::Function*>(codegenPrototype(ctx));
    }
    // P1-03 / GEN-05: 函数体只生成一次——TU 预扫（实例 struct 布局先行）
    // 与主循环可能两次到达同一方法定义。
    if (function->size() > 0) {
        return function;
    }

    // A declaration without a body needs no entry block or code.
    if (!body) {
        return function;
    }

    if (!sourceFile.empty()) {
        ctx.emitFunctionDebug(function, returnType, name, sourceLine);
    }

    llvm::BasicBlock* bb = llvm::BasicBlock::Create(ctx.getContext(), "entry", function);
    ctx.getBuilder().SetInsertPoint(bb);

    if (!sourceFile.empty()) {
        ctx.setDebugLocation(sourceLine);
    }

    ctx.pushScope();

    unsigned idx = 0;
    for (auto& param : params) {
        llvm::Argument* arg = function->getArg(idx);
        arg->setName(param->name);
        llvm::AllocaInst* alloca = ctx.getBuilder().CreateAlloca(
            ctx.getLLVMType(param->type), nullptr, param->name);
        ctx.getBuilder().CreateStore(arg, alloca);
        ctx.declareVariable(param->name, alloca, param->type);
        idx++;
    }

    if (body) {
        body->codegen(ctx);
    }

    // Ensure the body ends with a terminator. Void functions implicitly return;
    // falling off the end of a non-void function is undefined behaviour.
    if (!ctx.getBuilder().GetInsertBlock()->getTerminator()) {
        if (returnType->kind == TypeKind::Void) {
            ctx.getBuilder().CreateRetVoid();
        } else {
            ctx.getBuilder().CreateUnreachable();
        }
    }

    ctx.popScope();

    return function;
}

llvm::Value* DeclStmtAST::codegen(CodegenContext& ctx) {
    if (decl) {
        return decl->codegen(ctx);
    }
    return nullptr;
}

llvm::Value* TranslationUnitAST::codegen(CodegenContext& ctx) {
    // 预扫 1：struct/class 布局——模板实例（名字含 '$'）先行，且按基类
    // 依赖序出码（评审 C3：模板类继承非模板基类时，基类子对象槽依赖基类
    // llvm::StructType 先存在）。函数体只生成一次（FunctionDeclAST 幂等
    // 守卫），此处可安全重复到达。
    std::unordered_set<std::string> emittedStructs;
    std::function<void(StructDeclAST*)> emitStruct = [&](StructDeclAST* st) {
        if (!st || emittedStructs.count(st->name)) return;
        emittedStructs.insert(st->name);
        ClassType* ct = TypeContext::instance().getClass(st->name);
        std::string base = ct ? ct->baseClass : std::string();
        if (!base.empty()) {
            for (auto& d : declarations) {
                if (auto* b = dynamic_cast<StructDeclAST*>(d.get())) {
                    if (b->name == base) {
                        emitStruct(b);
                        break;
                    }
                }
            }
        }
        st->codegen(ctx);
    };
    for (auto& decl : declarations) {
        if (auto* st = dynamic_cast<StructDeclAST*>(decl.get())) {
            if (st->name.find('$') != std::string::npos) emitStruct(st);
        }
    }
    for (auto& decl : declarations) {
        if (auto* st = dynamic_cast<StructDeclAST*>(decl.get())) {
            if (st->name.find('$') == std::string::npos) emitStruct(st);
        }
    }

    // 预扫 2：函数签名声明——调用点（含前向引用的模板实例方法/函数）按需
    // 解析符号。
    for (auto& decl : declarations) {
        if (auto* fn = dynamic_cast<FunctionDeclAST*>(decl.get())) {
            fn->codegenPrototype(ctx);
        }
    }

    llvm::Value* last = nullptr;
    for (auto& decl : declarations) {
        last = decl->codegen(ctx);
    }
    return last;
}

llvm::Value* ArrayDeclAST::codegen(CodegenContext& ctx) {
    llvm::Type* elemLLVMType = ctx.getLLVMType(elementType);
    if (!elemLLVMType) return nullptr;

    auto* initList = dynamic_cast<InitializerListExprAST*>(initExpr.get());
    int effectiveSize = size;
    if (initList && effectiveSize == 0) {
        effectiveSize = static_cast<int>(initList->initializers.size());
    }

    llvm::ArrayType* arrType = llvm::ArrayType::get(elemLLVMType, effectiveSize);

    // Global arrays mirror VarDeclAST's global branch: a definition with
    // storage. The zero-init fallback keeps it a definition (InitVal=nullptr
    // would make LLVM treat it as an external declaration). Nested
    // initializer lists recurse through buildAggregateConstant.
    if (ctx.isGlobalScope()) {
        llvm::Constant* initConstant = nullptr;
        if (initList) {
            Type* astArrayType = new ArrayType(elementType, effectiveSize);
            initConstant = buildAggregateConstant(ctx, astArrayType, initList);
        }
        if (!initConstant) {
            initConstant = llvm::Constant::getNullValue(arrType);
        }
        llvm::GlobalVariable* global = new llvm::GlobalVariable(
            ctx.getModule(), arrType, elementType->isConst,
            llvm::GlobalVariable::ExternalLinkage, initConstant, name);
        ctx.declareVariable(name, global, new ArrayType(elementType, effectiveSize));
        return global;
    }

    llvm::AllocaInst* alloca = ctx.getBuilder().CreateAlloca(arrType, nullptr, name);

    if (initList) {
        ArrayType astArray(elementType, effectiveSize);
        ctx.getBuilder().CreateStore(llvm::Constant::getNullValue(arrType), alloca);
        emitAggregateInitializer(ctx, alloca, &astArray, initList);
    } else if (initExpr) {
        llvm::Value* initVal = initExpr->codegen(ctx);
        if (initVal) {
            if (initExpr->isLValue) initVal = ctx.loadValue(initVal, initExpr->type);
            ctx.getBuilder().CreateStore(initVal, alloca);
        }
    }

    ctx.declareVariable(name, alloca, elementType);
    return alloca;
}

llvm::Value* MultiVarDeclAST::codegen(CodegenContext& ctx) {
    llvm::Value* last = nullptr;
    for (auto& decl : decls) {
        if (decl) last = decl->codegen(ctx);
    }
    return last;
}

llvm::Value* StructDeclAST::codegen(CodegenContext& ctx) {
    // Reuse existing struct type if one with this name already exists
    if (auto* existing = llvm::StructType::getTypeByName(ctx.getContext(), name)) {
        // AGG-11: nested types must still be generated on this path — the
        // type object exists (e.g. via an earlier forward declaration), but
        // their statics/methods have never been emitted. Idempotent.
        for (auto& nested : nestedTypes) {
            if (nested) nested->codegen(ctx);
        }
        // AGG-10: static data members must be declared BEFORE method bodies
        // are generated (method bodies may reference them). Idempotent.
        emitStaticMembers(ctx);
        // Still need to generate methods if this is a class
        if (!methods.empty()) {
            for (auto& method : methods) {
                method->codegen(ctx);
            }
        }
        return nullptr;
    }

    bool isClass = !methods.empty() || !baseClass.empty();
    // P1-05 / ANN: 布局单源化——经注册的聚合 Type 走 LayoutBuilder（与
    // getLLVMType 同一实现）。类型注册缺失时回退旧内联构造（懒路径）。
    Type* aggType = nullptr;
    if (isClass) aggType = TypeContext::instance().getClass(name);
    else aggType = TypeContext::instance().getStruct(name);
    std::vector<llvm::Type*> fieldTypes;
    bool isPacked = false;
    if (aggType && (aggType->kind == TypeKind::Struct || aggType->kind == TypeKind::Class)) {
        auto LR = LayoutBuilder::buildAggregate(aggType, ctx.getContext(),
            ctx.getModule().getDataLayout(),
            [&ctx](Type* t) { return ctx.getLLVMType(t); },
            [](const std::string& n) -> Type* {
                TypeContext& tc = TypeContext::instance();
                if (auto* st = tc.getStruct(n)) return st;
                if (auto* ct = tc.getClass(n)) return ct;
                if (auto* ut = tc.getUnion(n)) return ut;
                return nullptr;
            });
        for (auto& f : LR.fields) fieldTypes.push_back(f.type);
        isPacked = LR.isPacked;
    } else {
        // For classes with inheritance, add base class struct as first field
        if (isClass && !baseClass.empty()) {
            if (auto* baseType = llvm::StructType::getTypeByName(ctx.getContext(), baseClass)) {
                fieldTypes.push_back(baseType);
            }
        }
        for (auto& field : fields) {
            fieldTypes.push_back(ctx.getLLVMType(field.type));
        }
    }

    llvm::StructType* structType = llvm::StructType::create(ctx.getContext(), fieldTypes, name, isPacked);

    // AGG-11: nested types first — outer method bodies may reference their
    // members (same ordering rule as sema: nestedTypes -> statics -> methods).
    for (auto& nested : nestedTypes) {
        if (nested) nested->codegen(ctx);
    }

    // AGG-10: static data members before methods — method bodies may
    // reference them (same ordering requirement as sema).
    emitStaticMembers(ctx);

    // Generate methods as separate functions
    if (isClass) {
        for (auto& method : methods) {
            method->codegen(ctx);
        }
    }

    return nullptr;
}

// AGG-10: emit static data members as global variables. Idempotent: the
// global branch of VarDeclAST::codegen reuses an existing GlobalVariable.
void StructDeclAST::emitStaticMembers(CodegenContext& ctx) {
    for (auto& vd : staticMembers) {
        if (vd) vd->codegen(ctx);
    }
}

llvm::Value* UnionDeclAST::codegen(CodegenContext& ctx) {
    // The union's LLVM layout depends on its members and is created lazily by
    // CodegenContext::getLLVMType(TypeKind::Union); nothing to emit here.
    // AGG-11: but nested type declarations must be generated (their members
    // may carry statics/methods).
    for (auto& nested : nestedTypes) {
        if (nested) nested->codegen(ctx);
    }
    (void)ctx;
    return nullptr;
}

llvm::Value* EnumDeclAST::codegen(CodegenContext& ctx) {
    return nullptr;
}

llvm::Value* TypedefDeclAST::codegen(CodegenContext& ctx) {
    return nullptr;
}

llvm::Value* ForwardDeclAST::codegen(CodegenContext& ctx) {
    return nullptr;
}

// 新增AST节点的codegen实现
llvm::Value* UsingDeclAST::codegen(CodegenContext& ctx) {
    // using 声明在代码生成阶段不需要做任何事情
    // 类型别名已经在语义分析阶段处理
    return nullptr;
}

llvm::Value* TemplateDeclAST::codegen(CodegenContext& ctx) {
    // P1-03 / GEN-05: 模板定义本身不产生代码——惰性实例化，未使用的模板
    // 零符号。实例化产生的实例 decl 由 sema 追加到翻译单元尾部出码。
    return nullptr;
}

llvm::Value* TypeDeclAST::codegen(CodegenContext& ctx) {
    // type 声明在代码生成阶段不需要做任何事情
    // 新类型已经在语义分析阶段处理
    return nullptr;
}

llvm::Value* DeferStmtAST::codegen(CodegenContext& ctx) {
    // Register the deferred call; it is emitted by the enclosing compound
    // statement when its scope exits (or before return/break/continue).
    if (callExpr) {
        ctx.addDefer(callExpr.get());
    }
    return nullptr;
}

llvm::Value* ModuleDeclAST::codegen(CodegenContext& ctx) {
    // 模块声明在代码生成阶段不需要做任何事情
    // 模块系统已经在语义分析阶段处理
    return nullptr;
}

llvm::Value* NamespaceDeclAST::codegen(CodegenContext& ctx) {
    // Namespace members are emitted into the same module; semantic analysis
    // has already qualified their names (see SemanticAnalyzer::visit).
    llvm::Value* last = nullptr;
    for (auto& decl : declarations) {
        if (decl) {
            last = decl->codegen(ctx);
        }
    }
    return last;
}

// P1-04 / CT-01: compile_time 顶层声明节点不直接出码——static_assert 无值，
// compile_time.if 由 sema 选中分支后、codegen 阶段另行处理（CT-03/SEM-07）。
// P1-04 / CT-03: compile_time.if——sema 已选择分支（ctResolved/selectedThen），
// codegen 只出选中分支；未解析（诊断已发、编译将中止）时不出码。
llvm::Value* CompileTimeIfDeclAST::codegen(CodegenContext& ctx) {
    if (!ctResolved) {
        return nullptr;
    }
    auto& decls = selectedThen ? thenDecls : elseDecls;
    llvm::Value* last = nullptr;
    for (auto& decl : decls) {
        if (decl) {
            last = decl->codegen(ctx);
        }
    }
    return last;
}

llvm::Value* CompileTimeAssertDeclAST::codegen(CodegenContext& ctx) {
    (void)ctx;
    return nullptr;
}
