#pragma once

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "ast/Symbol.h"
#include <map>

class Type;

class CodegenContext {
public:
    CodegenContext();

    llvm::LLVMContext& getContext() { return *context; }
    llvm::IRBuilder<>& getBuilder() { return builder; }
    llvm::Module& getModule() { return *module; }
    llvm::DIBuilder& getDIBuilder() { return *diBuilder; }
    llvm::DIFile* getDIFile() { return diFile; }

    std::unique_ptr<llvm::Module> takeModule() { return std::move(module); }
    std::unique_ptr<llvm::LLVMContext> takeContext() { return std::move(context); }

    void pushScope();
    void popScope();
    Scope* currentScope();

    llvm::Value* lookupVariable(const std::string& name);
    llvm::Value* lookupVariableAddr(const std::string& name);
    void declareVariable(const std::string& name, llvm::Value* alloca, Type* type);

    llvm::Type* getLLVMType(Type* type);

    void setSourceFile(const std::string& file);
    void emitFunctionDebug(llvm::Function* func, Type* returnType,
                           const std::string& name, unsigned line);
    void emitVariableDebug(llvm::AllocaInst* alloca, const std::string& name,
                           Type* type, unsigned line);
    void setDebugLocation(unsigned line, unsigned col = 0);
    void finalizeDebugInfo();

    void pushBreakBlock(llvm::BasicBlock* bb);
    void popBreakBlock();
    llvm::BasicBlock* getBreakBlock() const;

    void pushContinueBlock(llvm::BasicBlock* bb);
    void popContinueBlock();
    llvm::BasicBlock* getContinueBlock() const;

    void addLabel(const std::string& label, llvm::BasicBlock* bb);
    llvm::BasicBlock* getLabel(const std::string& label) const;

    llvm::Value* coerceToBool(llvm::Value* val);

private:
    std::unique_ptr<llvm::LLVMContext> context;
    llvm::IRBuilder<> builder;
    std::unique_ptr<llvm::Module> module;
    std::vector<std::unique_ptr<Scope>> scopes;

    std::unique_ptr<llvm::DIBuilder> diBuilder;
    llvm::DIFile* diFile{nullptr};
    llvm::DICompileUnit* diCompileUnit{nullptr};
    llvm::DIScope* diCurrentScope{nullptr};
    std::string sourceFileName;

    std::vector<llvm::BasicBlock*> breakBlocks;
    std::vector<llvm::BasicBlock*> continueBlocks;
    std::map<std::string, llvm::BasicBlock*> labels;
};
