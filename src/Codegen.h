#pragma once

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "Symbol.h"

class CodegenContext {
public:
    CodegenContext();

    llvm::LLVMContext& getContext() { return context; }
    llvm::IRBuilder<>& getBuilder() { return builder; }
    llvm::Module& getModule() { return module; }

    void pushScope();
    void popScope();
    Scope* currentScope();

    llvm::Value* lookupVariable(const std::string& name);
    void declareVariable(const std::string& name, llvm::Value* alloca, Type* type);

    llvm::Type* getLLVMType(Type* type);

private:
    llvm::LLVMContext context;
    llvm::IRBuilder<> builder;
    llvm::Module module;
    std::vector<std::unique_ptr<Scope>> scopes;
};
