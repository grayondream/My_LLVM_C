#include "Stmt.h"
#include "codegen/CodegenContext.h"

llvm::Value* ExprStmtAST::codegen(CodegenContext& ctx) {
    if (expr) {
        expr->codegen(ctx);
    }
    return nullptr;
}

llvm::Value* CompoundStmtAST::codegen(CodegenContext& ctx) {
    llvm::Value* last = nullptr;
    for (auto& stmt : stmts) {
        last = stmt->codegen(ctx);
    }
    return last;
}

llvm::Value* ReturnStmtAST::codegen(CodegenContext& ctx) {
    if (!value) {
        ctx.getBuilder().CreateRetVoid();
        return nullptr;
    }
    llvm::Value* v = value->codegen(ctx);
    if (v) {
        ctx.getBuilder().CreateRet(v);
    }
    return v;
}


