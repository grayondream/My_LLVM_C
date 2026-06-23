#include "CodegenContext.h"
#include "ast/Type.h"
#include "support/Log.h"

CodegenContext::CodegenContext()
    : builder(context), module("my_llvm_c", context) {
    pushScope();
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

llvm::Value* CodegenContext::lookupVariable(const std::string& name) {
    Symbol* sym = currentScope()->lookup(name);
    if (!sym) return nullptr;
    return builder.CreateLoad(getLLVMType(sym->type), sym->type->base ? nullptr : nullptr, name);
}

void CodegenContext::declareVariable(const std::string& name, llvm::Value* alloca, Type* type) {
    currentScope()->symbols[name] = new Symbol(name, type);
}

llvm::Type* CodegenContext::getLLVMType(Type* type) {
    if (!type) return llvm::Type::getVoidTy(context);

    switch (type->kind) {
        case TypeKind::Int:    return llvm::Type::getInt32Ty(context);
        case TypeKind::Float:  return llvm::Type::getFloatTy(context);
        case TypeKind::Double: return llvm::Type::getDoubleTy(context);
        case TypeKind::Char:   return llvm::Type::getInt8Ty(context);
        case TypeKind::Void:   return llvm::Type::getVoidTy(context);
        case TypeKind::Pointer:
            return llvm::PointerType::get(getLLVMType(type->base), 0);
        default:               return llvm::Type::getInt32Ty(context);
    }
}
