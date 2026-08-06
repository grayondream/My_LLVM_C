#include "CodegenContext.h"
#include "ast/Type.h"
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

void CodegenContext::declareVariable(const std::string& name, llvm::Value* alloca, Type* type) {
    currentScope()->declare(name, new Symbol(name, type, alloca));
}

void CodegenContext::pushBreakBlock(llvm::BasicBlock* bb) {
    breakBlocks.push_back(bb);
}

void CodegenContext::popBreakBlock() {
    if (!breakBlocks.empty()) {
        breakBlocks.pop_back();
    }
}

llvm::BasicBlock* CodegenContext::getBreakBlock() const {
    if (breakBlocks.empty()) return nullptr;
    return breakBlocks.back();
}

void CodegenContext::pushContinueBlock(llvm::BasicBlock* bb) {
    continueBlocks.push_back(bb);
}

void CodegenContext::popContinueBlock() {
    if (!continueBlocks.empty()) {
        continueBlocks.pop_back();
    }
}

llvm::BasicBlock* CodegenContext::getContinueBlock() const {
    if (continueBlocks.empty()) return nullptr;
    return continueBlocks.back();
}

void CodegenContext::addLabel(const std::string& label, llvm::BasicBlock* bb) {
    labels[label] = bb;
}

llvm::BasicBlock* CodegenContext::getLabel(const std::string& label) const {
    auto it = labels.find(label);
    if (it != labels.end()) return it->second;
    return nullptr;
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
        return builder.CreatePtrToInt(val, llvm::Type::getInt1Ty(*context), "ptrtobool");
    }
    return val;
}

llvm::Type* CodegenContext::getLLVMType(Type* type) {
    if (!type) return llvm::Type::getVoidTy(*context);

    switch (type->kind) {
        case TypeKind::Int:    return llvm::Type::getInt32Ty(*context);
        case TypeKind::Float:  return llvm::Type::getFloatTy(*context);
        case TypeKind::Double: return llvm::Type::getDoubleTy(*context);
        case TypeKind::Char:   return llvm::Type::getInt8Ty(*context);
        case TypeKind::Void:   return llvm::Type::getVoidTy(*context);
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
        case TypeKind::Array: {
            auto* at = static_cast<ArrayType*>(type);
            return llvm::ArrayType::get(getLLVMType(at->elementType), at->size);
        }
        case TypeKind::Enum:   return llvm::Type::getInt32Ty(*context);
        case TypeKind::Typedef: {
            auto* td = static_cast<TypedefType*>(type);
            return getLLVMType(td->aliasedType);
        }
        default:               return llvm::Type::getInt32Ty(*context);
    }
}
