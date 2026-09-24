#pragma once

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "ast/Symbol.h"
#include <map>

class Type;
class ExprAST;

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
    bool isGlobalScope() const;

    llvm::Value* lookupVariable(const std::string& name);
    llvm::Value* lookupVariableAddr(const std::string& name);
    void declareVariable(const std::string& name, llvm::Value* alloca, Type* type);

    // Load the value denoted by an lvalue pointer. Arrays decay to a pointer to
    // their first element instead of being loaded by value.
    llvm::Value* loadValue(llvm::Value* ptr, Type* type);

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


    // Defer statement support. Deferred expressions are registered while a
    // compound statement is generated and emitted when its scope exits, or
    // before a return/break/continue that leaves the scope.
    void pushDeferScope();
    void popDeferScope();       // emit and drop the innermost scope
    void discardDeferScope();   // drop the innermost scope without emitting
    void addDefer(ExprAST* expr);
    void emitDefersFrom(size_t depth);  // emit scopes [size-1 .. depth], innermost first
    void emitAllDefers();
    size_t getBreakDeferBoundary() const;
    size_t getContinueDeferBoundary() const;

    llvm::Value* coerceToBool(llvm::Value* val);

    // Insert an implicit conversion of `val` to `targetLLVMType`, following the
    // usual C arithmetic conversion rules. Returns `val` unchanged when no
    // conversion is needed or possible.
    llvm::Value* castValue(llvm::Value* val, llvm::Type* targetLLVMType);

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

    std::vector<std::vector<ExprAST*>> deferScopes;
    std::vector<size_t> breakDeferBoundaries;
    std::vector<size_t> continueDeferBoundaries;
};
