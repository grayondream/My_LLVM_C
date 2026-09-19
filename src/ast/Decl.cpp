#include "Decl.h"
#include "codegen/CodegenContext.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Constants.h"
#include "Mangle.h"

static llvm::Constant* foldToConstant(CodegenContext& ctx, const FoldedValue& fv) {
    switch (fv.type) {
        case FoldedValue::INT:
            return llvm::ConstantInt::get(ctx.getLLVMType(new Type(TypeKind::Int)), fv.intVal);
        case FoldedValue::DOUBLE:
            return llvm::ConstantFP::get(ctx.getLLVMType(new Type(TypeKind::Double)), fv.doubleVal);
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
    if (targetType) v = ctx.castValue(v, ctx.getLLVMType(targetType));
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
            Type* mtype = ut->members[0].second;
            auto& item = *init->initializers[0];
            if (auto* nested = dynamic_cast<InitializerListExprAST*>(&item)) {
                emitAggregateInitializer(ctx, memberPtr, mtype, nested);
            } else {
                storeScalarInitializer(ctx, memberPtr, ctx.getLLVMType(mtype), item, mtype);
            }
            break;
        }
        case TypeKind::Struct:
        case TypeKind::Class: {
            std::vector<std::pair<std::string, Type*>>* fields = nullptr;
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
                Type* ftype = (*fields)[i].second;
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
            c = llvm::dyn_cast_or_null<llvm::Constant>(ctx.castValue(c, lt));
        }
        return c;
    };

    switch (type->kind) {
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
            std::vector<std::pair<std::string, Type*>>* fields = nullptr;
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
                        c = buildAggregateConstant(ctx, (*fields)[i].second, nested);
                    } else {
                        c = constFor(*init->initializers[i], (*fields)[i].second);
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
                first = buildAggregateConstant(ctx, ut->members[0].second, nested);
            } else {
                first = constFor(*init->initializers[0], ut->members[0].second);
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
            initConstant = llvm::dyn_cast_or_null<llvm::Constant>(ctx.castValue(initConstant, llvmType));
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
                         elem->kind == TypeKind::Union)) {
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
                initVal = ctx.castValue(initVal, llvmType);
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

llvm::Value* FunctionDeclAST::codegen(CodegenContext& ctx) {
    llvm::Type* retType = ctx.getLLVMType(returnType);
    std::vector<llvm::Type*> paramTypes;
    for (auto& param : params) {
        paramTypes.push_back(ctx.getLLVMType(param->type));
    }

    // Use mangled name for LLVM IR
    std::vector<Type*> astParamTypes;
    for (auto& param : params) {
        astParamTypes.push_back(param->type);
    }
    std::string mangledName = mangleFunction(name, astParamTypes);

    // Reuse a previous declaration (prototype) if one already exists.
    llvm::Function* function = ctx.getModule().getFunction(mangledName);
    if (!function) {
        llvm::FunctionType* funcType = llvm::FunctionType::get(retType, paramTypes, isVarArg);
        function = llvm::Function::Create(
            funcType, llvm::Function::ExternalLinkage, mangledName, ctx.getModule());
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
        // Still need to generate methods if this is a class
        if (!methods.empty()) {
            for (auto& method : methods) {
                method->codegen(ctx);
            }
        }
        return nullptr;
    }

    bool isClass = !methods.empty() || !baseClass.empty();
    std::vector<llvm::Type*> fieldTypes;

    // For classes with inheritance, add base class struct as first field
    if (isClass && !baseClass.empty()) {
        if (auto* baseType = llvm::StructType::getTypeByName(ctx.getContext(), baseClass)) {
            fieldTypes.push_back(baseType);
        }
    }

    for (auto& field : fields) {
        fieldTypes.push_back(ctx.getLLVMType(field.second));
    }

    llvm::StructType* structType = llvm::StructType::create(ctx.getContext(), fieldTypes, name);

    // Generate methods as separate functions
    if (isClass) {
        for (auto& method : methods) {
            method->codegen(ctx);
        }
    }

    return nullptr;
}

llvm::Value* UnionDeclAST::codegen(CodegenContext& ctx) {
    // The union's LLVM layout depends on its members and is created lazily by
    // CodegenContext::getLLVMType(TypeKind::Union); nothing to emit here.
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
