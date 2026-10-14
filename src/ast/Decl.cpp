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

// P1-07 / CG-09: unified lvalue read (mirror of Expr.cpp's readLValue) —
// bit-field lvalues are read via loadBitFieldValue, everything else via
// loadValue. Non-lvalues pass through unchanged.
static llvm::Value* readLValue(CodegenContext& ctx, ExprAST& e, llvm::Value* v) {
    if (!v) return nullptr;
    if (!e.isLValue) return v;
    if (e.isBitFieldAccess()) {
        auto& ma = static_cast<MemberAccessExprAST&>(e);
        return ctx.loadBitFieldValue(v, ma.bitOffset, ma.bitWidth, e.type);
    }
    return ctx.loadValue(v, e.type);
}

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
    if (expr.isLValue) v = readLValue(ctx, expr, v);
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
            // P1-07 / AGG-06: 位域成员 → 初始值 RMW 进存储单元。
            if (ut->members[0].bitWidth != 0 || ut->members[0].name.empty()) {
                auto it = ut->bitFields.find(ut->members[0].name);
                if (it != ut->bitFields.end()) {
                    llvm::Value* v = item.codegen(ctx);
                    if (v) {
                        v = readLValue(ctx, item, v);
                        if (llvm::Type* dt = ctx.getLLVMType(mtype)) {
                            v = ctx.castValue(v, item.type, dt);
                        }
                        ctx.storeBitFieldValue(memberPtr, it->second.bitOffset,
                                               it->second.bitWidth, mtype, v);
                    }
                }
                break;
            }
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
            std::vector<FieldInfo>* semFields = nullptr;
            std::unordered_map<std::string, BitFieldLayout>* bfMap = nullptr;
            if (type->kind == TypeKind::Struct) {
                auto* st = static_cast<StructType*>(type);
                fields = &st->fields;
                if (!st->semanticFields.empty()) {
                    semFields = &st->semanticFields;
                    bfMap = &st->bitFields;
                }
            } else {
                auto* ct = static_cast<ClassType*>(type);
                fields = &ct->fields;
                if (!ct->baseClass.empty()) baseOffset = 1;
                if (!ct->semanticFields.empty()) {
                    semFields = &ct->semanticFields;
                    bfMap = &ct->bitFields;
                }
            }
            // P1-07 / AGG-06: 单个初始值项写入一个字段（嵌套/string 深拷贝/标量）。
            auto emitFieldItem = [&](ExprAST& item, Type* ftype, llvm::Value* fptr) {
                if (auto* nested = dynamic_cast<InitializerListExprAST*>(&item)) {
                    emitAggregateInitializer(ctx, fptr, ftype, nested);
                    return;
                }
                Type* fr = ftype;
                while (fr && fr->kind == TypeKind::Typedef)
                    fr = static_cast<TypedefType*>(fr)->aliasedType;
                // I4 (D7): brace-init string 字段活值深拷贝（新鲜值直接 store）。
                if (fr && fr->kind == TypeKind::String &&
                    !ctx.isFreshStringValue(item)) {
                    llvm::Value* v = item.codegen(ctx);
                    if (v) {
                        v = readLValue(ctx, item, v);
                        v = ctx.castValue(v, item.type, ctx.getLLVMType(ftype));
                        auto savedIP = ctx.getBuilder().saveIP();
                        llvm::Function* copyFn = ctx.getStringCopyFn();
                        ctx.getBuilder().restoreIP(savedIP);
                        ctx.getBuilder().CreateCall(copyFn, {fptr, v});
                    }
                } else {
                    storeScalarInitializer(ctx, fptr, ctx.getLLVMType(ftype), item, ftype);
                }
            };
            if (semFields) {
                // P1-07 / AGG-06: 位域感知初始化——按语义字段序消费初始值；
                // 位域值 RMW 进存储单元，普通字段按物化索引 GEP。
                std::unordered_map<std::string, unsigned> nameIdx;
                for (unsigned k = 0; k < fields->size(); ++k) {
                    if (!(*fields)[k].name.empty()) nameIdx[(*fields)[k].name] = k;
                }
                for (size_t i = 0; i < semFields->size() && i < init->initializers.size(); ++i) {
                    auto& sf = (*semFields)[i];
                    auto& item = *init->initializers[i];
                    if (sf.bitWidth != 0 || sf.name.empty()) {
                        auto it = bfMap->find(sf.name);
                        if (it == bfMap->end()) continue;
                        llvm::Value* fptr = builder.CreateStructGEP(
                            destLLVM, dest, baseOffset + it->second.unitIndex, "bfinit");
                        llvm::Value* v = item.codegen(ctx);
                        if (!v) continue;
                        v = readLValue(ctx, item, v);
                        if (llvm::Type* dt = ctx.getLLVMType(sf.type)) {
                            v = ctx.castValue(v, item.type, dt);
                        }
                        ctx.storeBitFieldValue(fptr, it->second.bitOffset,
                                               it->second.bitWidth, sf.type, v);
                    } else {
                        auto nit = nameIdx.find(sf.name);
                        if (nit == nameIdx.end()) continue;
                        emitFieldItem(item, sf.type, builder.CreateStructGEP(
                            destLLVM, dest, baseOffset + nit->second, "fieldinit"));
                    }
                }
                break;
            }
            for (size_t i = 0; i < fields->size() && i < init->initializers.size(); ++i) {
                auto& item = *init->initializers[i];
                Type* ftype = (*fields)[i].type;
                llvm::Value* fptr = builder.CreateStructGEP(
                    destLLVM, dest, baseOffset + (unsigned)i, "fieldinit");
                emitFieldItem(item, ftype, fptr);
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
            // P1-07 / AGG-06: getLLVMType 先触发 LayoutBuilder 物化
            // （填充 semanticFields/bitFields），再读语义字段序。
            auto* stTy = llvm::dyn_cast<llvm::StructType>(ctx.getLLVMType(type));
            if (!stTy) return nullptr;
            std::vector<FieldInfo>* semFields = nullptr;
            std::unordered_map<std::string, BitFieldLayout>* bfMap = nullptr;
            if (type->kind == TypeKind::Struct) {
                auto* st = static_cast<StructType*>(type);
                if (!st->semanticFields.empty()) {
                    semFields = &st->semanticFields;
                    bfMap = &st->bitFields;
                }
            } else {
                auto* ct = static_cast<ClassType*>(type);
                if (!ct->semanticFields.empty()) {
                    semFields = &ct->semanticFields;
                    bfMap = &ct->bitFields;
                }
            }
            std::vector<llvm::Constant*> elems;
            for (unsigned e = 0; e < stTy->getNumElements(); ++e) {
                elems.push_back(llvm::Constant::getNullValue(stTy->getElementType(e)));
            }
            if (semFields) {
                // P1-07 / AGG-06: 位域感知——按语义字段序消费初始值；位域值
                // 截断+移位 OR 进所属存储单元常量，普通字段写物化索引。
                std::unordered_map<std::string, unsigned> nameIdx;
                for (unsigned k = 0; k < fields->size(); ++k) {
                    if (!(*fields)[k].name.empty()) nameIdx[(*fields)[k].name] = k;
                }
                std::unordered_map<unsigned, llvm::APInt> bfAcc;
                for (size_t i = 0; i < semFields->size() && i < init->initializers.size(); ++i) {
                    auto& sf = (*semFields)[i];
                    auto& item = *init->initializers[i];
                    if (sf.bitWidth != 0 || sf.name.empty()) {
                        auto it = bfMap->find(sf.name);
                        if (it == bfMap->end()) continue;
                        llvm::Constant* c = constFor(item, sf.type);
                        auto* ci = llvm::dyn_cast_or_null<llvm::ConstantInt>(c);
                        if (!ci) return nullptr;  // 非常量 → 放弃（回退零初始化）
                        unsigned unitIdx = it->second.unitIndex;
                        unsigned unitBits = stTy->getElementType(unitIdx)->getIntegerBitWidth();
                        // APInt 默认构造不初始化位宽——用 emplace 显式建零值。
                        if (bfAcc.find(unitIdx) == bfAcc.end()) {
                            bfAcc.emplace(unitIdx, llvm::APInt::getZero(unitBits));
                        }
                        llvm::APInt& acc = bfAcc[unitIdx];
                        llvm::APInt wMask =
                            llvm::APInt::getLowBitsSet(unitBits, it->second.bitWidth);
                        acc |= (ci->getValue().zext(unitBits) & wMask)
                                   .shl(it->second.bitOffset);
                    } else {
                        auto nit = nameIdx.find(sf.name);
                        if (nit == nameIdx.end()) continue;
                        llvm::Constant* c = nullptr;
                        if (auto* nested = dynamic_cast<InitializerListExprAST*>(&item)) {
                            c = buildAggregateConstant(ctx, sf.type, nested);
                        } else {
                            c = constFor(item, sf.type);
                        }
                        if (!c) c = llvm::Constant::getNullValue(stTy->getElementType(nit->second));
                        elems[nit->second] = c;
                    }
                }
                for (auto& kv : bfAcc) {
                    elems[kv.first] =
                        llvm::ConstantInt::get(stTy->getElementType(kv.first), kv.second);
                }
                return llvm::ConstantStruct::get(stTy, elems);
            }
            // 既有路径（无位域）：字段 1:1。
            size_t elemIdx = 0;
            if (hasBase) {
                elems[elemIdx] = llvm::Constant::getNullValue(stTy->getElementType(elemIdx));
                ++elemIdx;
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
                elems[elemIdx] = c;
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
        // P1-05 / ANN-03: [[align(N)]] 全局变量对齐。
        if (declAlign) {
            global->setAlignment(llvm::Align(declAlign));
        }
        ctx.declareVariable(name, global, type);
        return global;
    }
    
    llvm::AllocaInst* alloca = ctx.getBuilder().CreateAlloca(llvmType, nullptr, name);
    // P1-05 / ANN-03: [[align(N)]] 局部变量对齐。
    if (declAlign) {
        alloca->setAlignment(llvm::Align(declAlign));
    }

    // TYP-12 D3 + I4 found issue: 未初始化聚合零初始化——
    //   * slice → 空 slice {null, 0}（TYP-12 D3）；
    //   * string / 含 string 聚合 → {null,0,0}：D7 赋值 helper 的 free 守卫
    //     （oldPtr != null）绝不能看到野指针，否则 free(garbage) 崩溃。
    {
        Type* st = stripTypedefs(type);
        if (st && !initExpr && !isConstexpr) {
            bool needZero = st->kind == TypeKind::Slice ||
                            st->kind == TypeKind::String ||
                            ((st->kind == TypeKind::Struct || st->kind == TypeKind::Class) &&
                             ctx.aggregateHasStringField(st));
            if (needZero) {
                ctx.getBuilder().CreateStore(llvm::Constant::getNullValue(llvmType), alloca);
            }
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
                    initVal = readLValue(ctx, *initExpr, initVal);
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
                // I4 (D2): string 初始化——活值深拷贝 / 新鲜值移动。
                // 类型为 string（剥 Typedef）且非 constexpr、非 initList 时：
                //   fresh（string.new / 返回 string 的调用 rvalue）→ 直接 store（移动）；
                //   其余（变量/字段/三元等活值）→ stringCopy 深拷贝到 alloca，
                //   跳过裸 store（copy 已写三字段）——`string t = s;` 后 s/t 独立。
                Type* strType = stripTypedefs(type);
                if (strType && strType->kind == TypeKind::String && !isConstexpr &&
                    !ctx.isFreshStringValue(*initExpr)) {
                    auto savedIP = ctx.getBuilder().saveIP();
                    llvm::Function* copyFn = ctx.getStringCopyFn();
                    ctx.getBuilder().restoreIP(savedIP);
                    ctx.getBuilder().CreateCall(copyFn, {alloca, initVal});
                } else if (strType && !isConstexpr &&
                           (strType->kind == TypeKind::Struct ||
                            strType->kind == TypeKind::Class) &&
                           ctx.aggregateHasStringField(strType)) {
                    // I4 (D7): 聚合含 string 字段 → 字段感知深拷贝
                    //（`struct S y = x;` 后 x/y 字段各自独立）。
                    ctx.emitAggregateStringCopy(alloca, initVal, strType,
                                                /*assign=*/false);
                } else {
                    ctx.getBuilder().CreateStore(initVal, alloca);
                }
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
    // P1-05 / ANN-04/05: 函数属性（inline→alwaysinline、cold→cold、
    // 参数 nonnull→NonNull）。幂等：重复 add 去重。
    for (auto& ann : annotations) {
        if (ann.name == "inline") {
            function->addFnAttr(llvm::Attribute::AlwaysInline);
        } else if (ann.name == "cold") {
            function->addFnAttr(llvm::Attribute::Cold);
        }
    }
    for (size_t i = 0; i < params.size(); ++i) {
        for (const auto& ann : params[i]->annotations) {
            if (ann.name == "nonnull") {
                function->addParamAttr(static_cast<unsigned>(i), llvm::Attribute::NonNull);
            }
        }
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
            if (initExpr->isLValue) initVal = readLValue(ctx, *initExpr, initVal);
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
    // P1-05 评审 I1: 按注册对象实际 kind 探测（带基类 struct 注册为
    // StructType，isClass 门控会误走 getClass 拿到 null → 回退丢注解）。
    (void)isClass;
    Type* aggType = TypeContext::instance().getStruct(name);
    if (!aggType) aggType = TypeContext::instance().getClass(name);
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
