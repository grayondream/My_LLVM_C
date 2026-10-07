#include "CodegenContext.h"
#include "ast/Type.h"
#include "ast/Mangle.h"
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

llvm::Value* CodegenContext::emitArrayToSliceDecay(ArrayType* arrayType,
                                                   llvm::Value* arrayAddr) {
    // TYP-12: build the {ptr, len} view over a statically-sized array
    // (zero-copy; the elements are never duplicated). Array operands already
    // yield the address of their first element.
    auto& builder = getBuilder();
    llvm::Type* sliceLLVM = getLLVMType(
        TypeContext::instance().getSliceType(arrayType->elementType));
    llvm::Value* v = llvm::Constant::getNullValue(sliceLLVM);
    v = builder.CreateInsertValue(v, arrayAddr, {0});
    return builder.CreateInsertValue(
        v, llvm::ConstantInt::get(llvm::Type::getInt64Ty(getContext()),
                                  static_cast<uint64_t>(arrayType->size)), {1});
}

// P1-02 评审 C1: injective layout-identity key. The LLVM named-struct name
// IS the layout identity (getTypeByName reuse), so it must never collide:
// length-prefixed components + '.' separators (user identifiers cannot
// contain '.'). Covers: two enums with different underlying types, the
// Result<My_Err, x> / Result<My, Err_x> ambiguity, and user structs named
// like the old "Optional_int32" prefix.
static std::string layoutKindName(TypeKind k) {
    switch (k) {
        case TypeKind::Void: return "void";
        case TypeKind::Bool: return "bool";
        case TypeKind::Char: return "char";
        case TypeKind::Int8: return "i8";
        case TypeKind::Int16: return "i16";
        case TypeKind::Int32: return "i32";
        case TypeKind::Int64: return "i64";
        case TypeKind::Int128: return "i128";
        case TypeKind::UInt8: return "u8";
        case TypeKind::UInt16: return "u16";
        case TypeKind::UInt32: return "u32";
        case TypeKind::UInt64: return "u64";
        case TypeKind::UInt128: return "u128";
        case TypeKind::ISize: return "isize";
        case TypeKind::USize: return "usize";
        case TypeKind::Float16: return "f16";
        case TypeKind::Float32: return "f32";
        case TypeKind::Float64: return "f64";
        case TypeKind::Float128: return "f128";
        default: return "unk";
    }
}

static std::string layoutKey(Type* t) {
    if (!t) return "unk";
    switch (t->kind) {
        case TypeKind::Typedef:
            return layoutKey(static_cast<TypedefType*>(t)->aliasedType);
        case TypeKind::Pointer:
            return "P" + layoutKey(t->base);
        case TypeKind::Array: {
            auto* at = static_cast<ArrayType*>(t);
            return "A" + std::to_string(at->size) + "." + layoutKey(at->elementType);
        }
        case TypeKind::Slice:
            return "L" + layoutKey(static_cast<SliceType*>(t)->elementType);
        case TypeKind::Optional:
            return "O" + layoutKey(static_cast<OptionalType*>(t)->elementType);
        case TypeKind::Result: {
            auto* r = static_cast<ResultType*>(t);
            return "R" + layoutKey(r->successType) + "." + layoutKey(r->errorType);
        }
        case TypeKind::Struct: {
            auto* s = static_cast<StructType*>(t);
            return "S" + std::to_string(s->name.size()) + "." + s->name;
        }
        case TypeKind::Class: {
            auto* c = static_cast<ClassType*>(t);
            return "C" + std::to_string(c->name.size()) + "." + c->name;
        }
        case TypeKind::Union: {
            auto* u = static_cast<UnionType*>(t);
            return "U" + std::to_string(u->name.size()) + "." + u->name;
        }
        case TypeKind::Enum: {
            auto* e = static_cast<EnumType*>(t);
            std::string underlying =
                e->underlyingType ? layoutKindName(e->underlyingType->kind) : "i32";
            return "E" + std::to_string(e->name.size()) + "." + e->name + "." + underlying;
        }
        default:
            return layoutKindName(t->kind);
    }
}

llvm::Type* CodegenContext::getLLVMType(Type* type) {
    if (!type) return llvm::Type::getVoidTy(*context);

    switch (type->kind) {
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
        case TypeKind::Float16:return llvm::Type::getHalfTy(*context);
        case TypeKind::Float128:return llvm::Type::getFP128Ty(*context);
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
            // INH-01: base sub-object occupies field slot 0 (mirror of the
            // Class case below; StructDeclAST::codegen normally pre-creates
            // this layout — this is the lazy-creation fallback).
            if (!st->baseClass.empty()) {
                if (auto* baseLLVM = llvm::StructType::getTypeByName(*context, st->baseClass)) {
                    fieldTypes.push_back(baseLLVM);
                }
            }
            for (auto& f : st->fields) {
                fieldTypes.push_back(getLLVMType(f.type));
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
                llvm::Type* mt = getLLVMType(m.type);
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
                fieldTypes.push_back(getLLVMType(f.type));
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
            // TYP-12: all slices share ONE canonical named struct {ptr, i64};
            // the element type does not affect the LLVM shape. Per-call
            // create() split the type into "Slice", "Slice.0", ... breaking
            // type identity when slices cross function boundaries.
            auto* st = llvm::StructType::getTypeByName(*context, "Slice");
            return st ? st
                      : llvm::StructType::create(*context,
                            {llvm::PointerType::get(*context, 0),
                             llvm::Type::getInt64Ty(*context)},
                            "Slice");
        }
        case TypeKind::Optional: {
            // P1-02 (TYP-13) DS4: { i1 valid, T value } — 判别标志在前。
            // 原实现误转换 SliceType* 且字段序为 {T, i1}。
            auto* optType = static_cast<OptionalType*>(type);
            // 评审 C1: injective identity key (layoutKey) — 用户可控的名字
            // （枚举底层类型、struct 名下划线）不得影响布局身份。
            std::string name = "opt." + layoutKey(optType->elementType);
            if (auto* st = llvm::StructType::getTypeByName(*context, name))
                return st; // idempotent: named struct identity is unique
            std::vector<llvm::Type*> fieldTypes;
            fieldTypes.push_back(llvm::Type::getInt1Ty(*context));    // valid
            fieldTypes.push_back(getLLVMType(optType->elementType));  // value
            return llvm::StructType::create(*context, fieldTypes, name);
        }
        case TypeKind::Result: {
            // P1-02 (TYP-14) DS4: { i1 ok, T value, E error } — 原实现无
            // 判别标志（DEC-03 裁决随此轮）。
            auto* resultType = static_cast<ResultType*>(type);
            std::string name = "res." + layoutKey(resultType->successType) +
                               "." + layoutKey(resultType->errorType);
            if (auto* st = llvm::StructType::getTypeByName(*context, name))
                return st; // idempotent: named struct identity is unique
            std::vector<llvm::Type*> fieldTypes;
            fieldTypes.push_back(llvm::Type::getInt1Ty(*context));     // ok
            fieldTypes.push_back(getLLVMType(resultType->successType)); // value
            fieldTypes.push_back(getLLVMType(resultType->errorType));   // error
            return llvm::StructType::create(*context, fieldTypes, name);
        }
        default:               return llvm::Type::getInt32Ty(*context);
    }
}
