#include "CodegenContext.h"
#include "ast/Type.h"
#include "ast/Expr.h"
#include "ast/Symbol.h"
#include "support/Log.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Metadata.h"

CodegenContext::CodegenContext()
    : context(std::make_unique<llvm::LLVMContext>()),
      builder(*context),
      module(std::make_unique<llvm::Module>("my_llvm_c", *context)),
      diBuilder(std::make_unique<llvm::DIBuilder>(*module)) {
    pushScope();
}

void CodegenContext::setSourceFile(const std::string& file) {
    sourceFileName = file;
    diFile = diBuilder->createFile(file, ".");
    diCompileUnit = diBuilder->createCompileUnit(
        llvm::dwarf::DW_LANG_C, diFile, "My_LLVM_C Compiler",
        false, "", 0);
    diCurrentScope = diFile;
}

llvm::DIType* getDIType(CodegenContext& ctx, Type* type) {
    if (!type) return nullptr;

    auto& builder = ctx.getDIBuilder();

    switch (type->kind) {
        case TypeKind::Void:
            return nullptr;
        case TypeKind::Int:
            return builder.createBasicType("int", 32, llvm::dwarf::DW_ATE_signed);
        case TypeKind::Float:
            return builder.createBasicType("float", 32, llvm::dwarf::DW_ATE_float);
        case TypeKind::Double:
            return builder.createBasicType("double", 64, llvm::dwarf::DW_ATE_float);
        case TypeKind::Char:
            return builder.createBasicType("char", 8, llvm::dwarf::DW_ATE_signed_char);
        case TypeKind::Bool:
            return builder.createBasicType("bool", 1, llvm::dwarf::DW_ATE_boolean);
        // 新增整数类型
        case TypeKind::Int8:
            return builder.createBasicType("int8", 8, llvm::dwarf::DW_ATE_signed);
        case TypeKind::Int16:
            return builder.createBasicType("int16", 16, llvm::dwarf::DW_ATE_signed);
        case TypeKind::Int32:
            return builder.createBasicType("int32", 32, llvm::dwarf::DW_ATE_signed);
        case TypeKind::Int64:
            return builder.createBasicType("int64", 64, llvm::dwarf::DW_ATE_signed);
        case TypeKind::Int128:
            return builder.createBasicType("int128", 128, llvm::dwarf::DW_ATE_signed);
        case TypeKind::UInt8:
            return builder.createBasicType("uint8", 8, llvm::dwarf::DW_ATE_unsigned);
        case TypeKind::UInt16:
            return builder.createBasicType("uint16", 16, llvm::dwarf::DW_ATE_unsigned);
        case TypeKind::UInt32:
            return builder.createBasicType("uint32", 32, llvm::dwarf::DW_ATE_unsigned);
        case TypeKind::UInt64:
            return builder.createBasicType("uint64", 64, llvm::dwarf::DW_ATE_unsigned);
        case TypeKind::UInt128:
            return builder.createBasicType("uint128", 128, llvm::dwarf::DW_ATE_unsigned);
        case TypeKind::ISize:
            return builder.createBasicType("isize", 64, llvm::dwarf::DW_ATE_signed);
        case TypeKind::USize:
            return builder.createBasicType("usize", 64, llvm::dwarf::DW_ATE_unsigned);
        case TypeKind::Float32:
            return builder.createBasicType("float32", 32, llvm::dwarf::DW_ATE_float);
        case TypeKind::Float64:
            return builder.createBasicType("float64", 64, llvm::dwarf::DW_ATE_float);
        case TypeKind::Pointer: {
            auto* pointee = getDIType(ctx, type->base);
            if (!pointee) {
                auto* opaque = builder.createUnspecifiedType("void");
                return builder.createPointerType(opaque, 64);
            }
            return builder.createPointerType(pointee, 64);
        }
        default:
            return builder.createBasicType("int", 32, llvm::dwarf::DW_ATE_signed);
    }
}

void CodegenContext::emitFunctionDebug(llvm::Function* func, Type* returnType,
                                       const std::string& name, unsigned line) {
    if (!diBuilder || !diFile) return;

    auto* retDIType = getDIType(*this, returnType);

    std::vector<llvm::Metadata*> paramTypes;
    if (retDIType) paramTypes.push_back(retDIType);

    auto* typeArray = llvm::MDTuple::get(*context, paramTypes);
    auto* subroutineType = diBuilder->createSubroutineType(
        llvm::DITypeRefArray(typeArray));

    auto* sp = diBuilder->createFunction(
        diFile, name, name, diFile,
        line, subroutineType,
        line, llvm::DINode::FlagZero,
        llvm::DISubprogram::SPFlagDefinition);

    func->setSubprogram(sp);
    diCurrentScope = sp;
}

void CodegenContext::emitVariableDebug(llvm::AllocaInst* alloca, const std::string& name,
                                       Type* type, unsigned line) {
    if (!diBuilder || !diFile || !diCurrentScope) return;

    auto* diType = getDIType(*this, type);
    if (!diType) return;

    diBuilder->createAutoVariable(
        diCurrentScope, name, diFile, line, diType);
}

void CodegenContext::setDebugLocation(unsigned line, unsigned col) {
    if (!diCurrentScope) return;
    auto* dl = llvm::DILocation::get(*context, line, col, diCurrentScope);
    builder.SetCurrentDebugLocation(dl);
}

void CodegenContext::finalizeDebugInfo() {
    if (diBuilder) {
        diBuilder->finalize();
    }
}

void CodegenContext::pushScope() {
    Scope* parent = scopes.empty() ? nullptr : scopes.back().get();
    scopes.push_back(std::make_unique<Scope>(parent));
}

void CodegenContext::popScope() {
    if (scopes.size() > 1) {
        scopes.pop_back();
    }
}

Scope* CodegenContext::currentScope() {
    return scopes.back().get();
}

bool CodegenContext::isGlobalScope() const {
    return scopes.size() == 1;
}

llvm::Value* CodegenContext::lookupVariable(const std::string& name) {
    Symbol* sym = currentScope()->lookup(name);
    if (!sym || !sym->value) return nullptr;
    return builder.CreateLoad(getLLVMType(sym->type), sym->value, name);
}

llvm::Value* CodegenContext::lookupVariableAddr(const std::string& name) {
    Symbol* sym = currentScope()->lookup(name);
    if (!sym || !sym->value) return nullptr;
    return sym->value;
}

llvm::Value* CodegenContext::loadValue(llvm::Value* ptr, Type* type) {
    if (!ptr) return nullptr;
    if (!ptr->getType()->isPointerTy()) return ptr;

    Type* t = type;
    while (t && t->kind == TypeKind::Typedef) {
        t = static_cast<TypedefType*>(t)->aliasedType;
    }

    // Arrays decay to a pointer to their first element in value contexts.
    if (t && t->kind == TypeKind::Array) {
        llvm::Type* arrTy = getLLVMType(t);
        if (!arrTy) return ptr;
        auto* i64 = llvm::Type::getInt64Ty(*context);
        return builder.CreateInBoundsGEP(
            arrTy, ptr,
            {llvm::ConstantInt::get(i64, 0), llvm::ConstantInt::get(i64, 0)},
            "decay");
    }

    llvm::Type* loadType = t ? getLLVMType(t) : llvm::Type::getInt32Ty(*context);
    if (!loadType) return ptr;
    return builder.CreateLoad(loadType, ptr, "loadtmp");
}

void CodegenContext::declareVariable(const std::string& name, llvm::Value* alloca, Type* type) {
    currentScope()->declare(name, new Symbol(name, type, alloca));
}

void CodegenContext::pushBreakBlock(llvm::BasicBlock* bb) {
    breakBlocks.push_back(bb);
    breakDeferBoundaries.push_back(deferScopes.size());
}

void CodegenContext::popBreakBlock() {
    if (!breakBlocks.empty()) {
        breakBlocks.pop_back();
    }
    if (!breakDeferBoundaries.empty()) {
        breakDeferBoundaries.pop_back();
    }
}

llvm::BasicBlock* CodegenContext::getBreakBlock() const {
    if (breakBlocks.empty()) return nullptr;
    return breakBlocks.back();
}

void CodegenContext::pushContinueBlock(llvm::BasicBlock* bb) {
    continueBlocks.push_back(bb);
    continueDeferBoundaries.push_back(deferScopes.size());
}

void CodegenContext::popContinueBlock() {
    if (!continueBlocks.empty()) {
        continueBlocks.pop_back();
    }
    if (!continueDeferBoundaries.empty()) {
        continueDeferBoundaries.pop_back();
    }
}

llvm::BasicBlock* CodegenContext::getContinueBlock() const {
    if (continueBlocks.empty()) return nullptr;
    return continueBlocks.back();
}

void CodegenContext::pushDeferScope() {
    deferScopes.emplace_back();
}

void CodegenContext::addDefer(ExprAST* expr) {
    if (expr && !deferScopes.empty()) {
        deferScopes.back().push_back(expr);
    }
}

void CodegenContext::emitDefersFrom(size_t depth) {
    if (depth > deferScopes.size()) return;
    for (size_t i = deferScopes.size(); i > depth; --i) {
        auto& scope = deferScopes[i - 1];
        for (auto it = scope.rbegin(); it != scope.rend(); ++it) {
            if (*it) (*it)->codegen(*this);
        }
    }
}

void CodegenContext::popDeferScope() {
    if (deferScopes.empty()) return;
    emitDefersFrom(deferScopes.size() - 1);
    deferScopes.pop_back();
}

void CodegenContext::discardDeferScope() {
    if (!deferScopes.empty()) {
        deferScopes.pop_back();
    }
}

void CodegenContext::emitAllDefers() {
    emitDefersFrom(0);
}

size_t CodegenContext::getBreakDeferBoundary() const {
    if (breakDeferBoundaries.empty()) return 0;
    return breakDeferBoundaries.back();
}

size_t CodegenContext::getContinueDeferBoundary() const {
    if (continueDeferBoundaries.empty()) return 0;
    return continueDeferBoundaries.back();
}

llvm::Value* CodegenContext::coerceToBool(llvm::Value* val) {
    if (!val) return nullptr;
    if (val->getType()->isIntegerTy(1)) return val;
    if (val->getType()->isIntegerTy()) {
        return builder.CreateICmpNE(val,
            llvm::ConstantInt::get(val->getType(), 0), "tobool");
    }
    if (val->getType()->isFloatingPointTy()) {
        return builder.CreateFCmpUNE(val,
            llvm::ConstantFP::get(val->getType(), 0.0), "tobool");
    }
    if (val->getType()->isPointerTy()) {
        // Compare against the null pointer. (ptrtoint to i1 would test only the
        // low address bit, so any aligned non-null pointer would look false.)
        auto* ptrTy = llvm::cast<llvm::PointerType>(val->getType());
        return builder.CreateICmpNE(val, llvm::ConstantPointerNull::get(ptrTy), "tobool");
    }
    return val;
}

llvm::Value* CodegenContext::castValue(llvm::Value* val, llvm::Type* targetLLVMType) {
    if (!val || !targetLLVMType) return val;
    llvm::Type* srcType = val->getType();
    if (srcType == targetLLVMType) return val;

    if (srcType->isIntegerTy() && targetLLVMType->isIntegerTy()) {
        unsigned srcBits = srcType->getIntegerBitWidth();
        unsigned dstBits = targetLLVMType->getIntegerBitWidth();
        // A 1-bit integer (bool) must be zero-extended so that `true` becomes 1.
        if (srcBits == 1 && dstBits > 1) return builder.CreateZExt(val, targetLLVMType, "zexttmp");
        if (srcBits < dstBits) return builder.CreateSExt(val, targetLLVMType, "sexttmp");
        if (srcBits > dstBits) return builder.CreateTrunc(val, targetLLVMType, "trunctmp");
        return val;
    }

    if (srcType->isIntegerTy() && targetLLVMType->isFloatingPointTy()) {
        return builder.CreateSIToFP(val, targetLLVMType, "sitofptmp");
    }
    if (srcType->isFloatingPointTy() && targetLLVMType->isIntegerTy()) {
        return builder.CreateFPToSI(val, targetLLVMType, "fptositmp");
    }
    if (srcType->isFloatingPointTy() && targetLLVMType->isFloatingPointTy()) {
        unsigned srcWidth = srcType->getFPMantissaWidth();
        unsigned dstWidth = targetLLVMType->getFPMantissaWidth();
        if (srcWidth < dstWidth) return builder.CreateFPExt(val, targetLLVMType, "fpexttmp");
        if (srcWidth > dstWidth) return builder.CreateFPTrunc(val, targetLLVMType, "fptrunctmp");
        return val;
    }

    if (srcType->isPointerTy() && targetLLVMType->isPointerTy()) {
        return builder.CreateBitCast(val, targetLLVMType, "bitcasttmp");
    }

    // Null-pointer constant / address conversions (TYP-24). Without these a
    // `T* p = null;` or pointer/integer comparison produced mismatched types.
    if (srcType->isIntegerTy() && targetLLVMType->isPointerTy()) {
        return builder.CreateIntToPtr(val, targetLLVMType, "inttoptr");
    }
    if (srcType->isPointerTy() && targetLLVMType->isIntegerTy()) {
        return builder.CreatePtrToInt(val, targetLLVMType, "ptrtoint");
    }

    return val;
}

llvm::Value* CodegenContext::castValue(llvm::Value* val, Type* fromAST, llvm::Type* targetLLVMType) {
    if (!val || !targetLLVMType || !fromAST) return castValue(val, targetLLVMType);
    llvm::Type* srcType = val->getType();
    if (srcType == targetLLVMType) return val;

    const bool fromUnsigned = isUnsignedIntegerType(fromAST);

    if (srcType->isIntegerTy() && targetLLVMType->isIntegerTy()) {
        unsigned srcBits = srcType->getIntegerBitWidth();
        unsigned dstBits = targetLLVMType->getIntegerBitWidth();
        // A 1-bit integer (bool) must be zero-extended so that `true` becomes 1.
        if (srcBits == 1 && dstBits > 1) return builder.CreateZExt(val, targetLLVMType, "zexttmp");
        if (srcBits < dstBits) {
            return fromUnsigned ? builder.CreateZExt(val, targetLLVMType, "zexttmp")
                                : builder.CreateSExt(val, targetLLVMType, "sexttmp");
        }
        if (srcBits > dstBits) return builder.CreateTrunc(val, targetLLVMType, "trunctmp");
        return val;
    }

    if (srcType->isIntegerTy() && targetLLVMType->isFloatingPointTy()) {
        return fromUnsigned ? builder.CreateUIToFP(val, targetLLVMType, "uitofptmp")
                            : builder.CreateSIToFP(val, targetLLVMType, "sitofptmp");
    }

    // Float -> int, float widening/narrowing and pointer conversions do not
    // depend on the source signedness; reuse the generic implementation.
    return castValue(val, targetLLVMType);
}

llvm::Type* CodegenContext::getLLVMType(Type* type) {
    if (!type) return llvm::Type::getVoidTy(*context);

    switch (type->kind) {
        case TypeKind::Int:    return llvm::Type::getInt32Ty(*context);
        case TypeKind::Float:  return llvm::Type::getFloatTy(*context);
        case TypeKind::Double: return llvm::Type::getDoubleTy(*context);
        case TypeKind::Char:   return llvm::Type::getInt8Ty(*context);
        case TypeKind::Void:   return llvm::Type::getVoidTy(*context);
        case TypeKind::Bool:   return llvm::Type::getInt1Ty(*context);
        // 新增整数类型
        case TypeKind::Int8:   return llvm::Type::getInt8Ty(*context);
        case TypeKind::Int16:  return llvm::Type::getInt16Ty(*context);
        case TypeKind::Int32:  return llvm::Type::getInt32Ty(*context);
        case TypeKind::Int64:  return llvm::Type::getInt64Ty(*context);
        case TypeKind::Int128: return llvm::Type::getInt128Ty(*context);
        case TypeKind::UInt8:  return llvm::Type::getInt8Ty(*context);
        case TypeKind::UInt16: return llvm::Type::getInt16Ty(*context);
        case TypeKind::UInt32: return llvm::Type::getInt32Ty(*context);
        case TypeKind::UInt64: return llvm::Type::getInt64Ty(*context);
        case TypeKind::UInt128:return llvm::Type::getInt128Ty(*context);
        case TypeKind::ISize:  return llvm::Type::getInt64Ty(*context);
        case TypeKind::USize:  return llvm::Type::getInt64Ty(*context);
        case TypeKind::Float32:return llvm::Type::getFloatTy(*context);
        case TypeKind::Float64:return llvm::Type::getDoubleTy(*context);
        case TypeKind::Pointer: {
            auto* pointee = getLLVMType(type->base);
            return llvm::PointerType::get(*context, 0);
        }
        case TypeKind::Struct: {
            auto* st = static_cast<StructType*>(type);
            // Reuse existing struct type if one with this name already exists
            if (auto* existing = llvm::StructType::getTypeByName(*context, st->name)) {
                return existing;
            }
            std::vector<llvm::Type*> fieldTypes;
            for (auto& f : st->fields) {
                fieldTypes.push_back(getLLVMType(f.second));
            }
            return llvm::StructType::create(*context, fieldTypes, st->name);
        }
        case TypeKind::Union: {
            // A union is laid out as a chunk at least as large as its largest
            // member and aligned like its most-aligned member. All members
            // start at offset 0, so member access is a GEP to field 0.
            auto* ut = static_cast<UnionType*>(type);
            if (auto* existing = llvm::StructType::getTypeByName(*context, ut->name)) {
                return existing;
            }
            const llvm::DataLayout& dl = module->getDataLayout();
            uint64_t maxSize = 0;
            uint64_t maxAlign = 0;
            llvm::Type* alignType = nullptr;
            for (auto& m : ut->members) {
                llvm::Type* mt = getLLVMType(m.second);
                if (!mt) continue;
                uint64_t sz = dl.getTypeAllocSize(mt);
                uint64_t al = dl.getABITypeAlign(mt).value();
                if (sz > maxSize) maxSize = sz;
                if (al > maxAlign) {
                    maxAlign = al;
                    alignType = mt;
                }
            }
            if (maxSize == 0) maxSize = 1;
            std::vector<llvm::Type*> fields;
            if (alignType) {
                fields.push_back(alignType);
                uint64_t alignSize = dl.getTypeAllocSize(alignType);
                if (maxSize > alignSize) {
                    fields.push_back(llvm::ArrayType::get(
                        llvm::Type::getInt8Ty(*context), maxSize - alignSize));
                }
            } else {
                fields.push_back(llvm::ArrayType::get(
                    llvm::Type::getInt8Ty(*context), maxSize));
            }
            return llvm::StructType::create(*context, fields, ut->name);
        }
        case TypeKind::Class: {
            auto* ct = static_cast<ClassType*>(type);
            // Reuse existing struct type if one with this name already exists
            if (auto* existing = llvm::StructType::getTypeByName(*context, ct->name)) {
                return existing;
            }
            std::vector<llvm::Type*> fieldTypes;
            // For inheritance, add base class struct as first field
            if (!ct->baseClass.empty()) {
                if (auto* baseType = llvm::StructType::getTypeByName(*context, ct->baseClass)) {
                    fieldTypes.push_back(baseType);
                }
            }
            for (auto& f : ct->fields) {
                fieldTypes.push_back(getLLVMType(f.second));
            }
            return llvm::StructType::create(*context, fieldTypes, ct->name);
        }
        case TypeKind::Array: {
            auto* at = static_cast<ArrayType*>(type);
            return llvm::ArrayType::get(getLLVMType(at->elementType), at->size);
        }
        case TypeKind::Enum: {
            // TYP-09/TYP-25: use the explicit underlying type when present.
            auto* et = static_cast<EnumType*>(type);
            if (et->underlyingType) {
                return getLLVMType(et->underlyingType);
            }
            return llvm::Type::getInt32Ty(*context);
        }
        case TypeKind::Typedef: {
            auto* td = static_cast<TypedefType*>(type);
            return getLLVMType(td->aliasedType);
        }
        // 新增类型
        case TypeKind::Slice: {
            // 切片类型表示为 { pointer, length } 结构体
            std::vector<llvm::Type*> fieldTypes;
            fieldTypes.push_back(llvm::PointerType::get(*context, 0)); // pointer
            fieldTypes.push_back(llvm::Type::getInt64Ty(*context));    // length
            return llvm::StructType::create(*context, fieldTypes, "Slice");
        }
        case TypeKind::Optional: {
            // 可选类型表示为 { value, has_value } 结构体
            auto* sliceType = static_cast<SliceType*>(type);
            std::vector<llvm::Type*> fieldTypes;
            fieldTypes.push_back(getLLVMType(sliceType->elementType)); // value
            fieldTypes.push_back(llvm::Type::getInt1Ty(*context));    // has_value
            return llvm::StructType::create(*context, fieldTypes, "Optional");
        }
        case TypeKind::Result: {
            // 结果类型表示为 { value, error } 结构体
            auto* resultType = static_cast<ResultType*>(type);
            std::vector<llvm::Type*> fieldTypes;
            fieldTypes.push_back(getLLVMType(resultType->successType)); // value
            fieldTypes.push_back(getLLVMType(resultType->errorType));   // error
            return llvm::StructType::create(*context, fieldTypes, "Result");
        }
        default:               return llvm::Type::getInt32Ty(*context);
    }
}
