#include "Decl.h"
#include "codegen/CodegenContext.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"

llvm::Value* VarDeclAST::codegen(CodegenContext& ctx) {
    llvm::Type* llvmType = ctx.getLLVMType(type);
    llvm::AllocaInst* alloca = ctx.getBuilder().CreateAlloca(llvmType, nullptr, name);
    if (initExpr) {
        llvm::Value* initVal = initExpr->codegen(ctx);
        if (initVal) {
            ctx.getBuilder().CreateStore(initVal, alloca);
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

    llvm::FunctionType* funcType = llvm::FunctionType::get(retType, paramTypes, false);
    llvm::Function* function = llvm::Function::Create(
        funcType, llvm::Function::ExternalLinkage, name, ctx.getModule());

    if (!sourceFile.empty()) {
        ctx.emitFunctionDebug(function, returnType, name, sourceLine);
    }

    llvm::BasicBlock* bb = llvm::BasicBlock::Create(ctx.getContext(), "entry", function);
    ctx.getBuilder().SetInsertPoint(bb);

    if (!sourceFile.empty()) {
        ctx.setDebugLocation(sourceLine);
    }

    ctx.pushScope();
    if (body) {
        body->codegen(ctx);
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
