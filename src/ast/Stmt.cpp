#include "Stmt.h"
#include "codegen/CodegenContext.h"
#include "support/Log.h"

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

llvm::Value* IfStmtAST::codegen(CodegenContext& ctx) {
    llvm::Value* condVal = cond->codegen(ctx);
    if (!condVal) return nullptr;

    auto& builder = ctx.getBuilder();
    llvm::Function* func = builder.GetInsertBlock()->getParent();

    llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(ctx.getContext(), "if.then", func);
    llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(ctx.getContext(), "if.else", func);
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx.getContext(), "if.end", func);

    builder.CreateCondBr(condVal, thenBB, elseBB);

    builder.SetInsertPoint(thenBB);
    thenStmt->codegen(ctx);
    if (!builder.GetInsertBlock()->getTerminator()) {
        builder.CreateBr(mergeBB);
    }

    builder.SetInsertPoint(elseBB);
    if (elseStmt) {
        elseStmt->codegen(ctx);
    }
    if (!builder.GetInsertBlock()->getTerminator()) {
        builder.CreateBr(mergeBB);
    }

    builder.SetInsertPoint(mergeBB);
    return nullptr;
}

llvm::Value* WhileStmtAST::codegen(CodegenContext& ctx) {
    llvm::Function* func = ctx.getBuilder().GetInsertBlock()->getParent();
    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(ctx.getContext(), "while.cond", func);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(ctx.getContext(), "while.body", func);
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(ctx.getContext(), "while.end", func);

    ctx.getBuilder().CreateBr(condBB);
    ctx.getBuilder().SetInsertPoint(condBB);

    llvm::Value* condVal = cond->codegen(ctx);
    if (!condVal) return nullptr;
    ctx.getBuilder().CreateCondBr(condVal, bodyBB, endBB);

    ctx.getBuilder().SetInsertPoint(bodyBB);
    body->codegen(ctx);
    if (!ctx.getBuilder().GetInsertBlock()->getTerminator()) {
        ctx.getBuilder().CreateBr(condBB);
    }

    ctx.getBuilder().SetInsertPoint(endBB);
    return nullptr;
}

llvm::Value* DoWhileStmtAST::codegen(CodegenContext& ctx) {
    llvm::Function* func = ctx.getBuilder().GetInsertBlock()->getParent();
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(ctx.getContext(), "dowhile.body", func);
    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(ctx.getContext(), "dowhile.cond", func);
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(ctx.getContext(), "dowhile.end", func);

    ctx.getBuilder().CreateBr(bodyBB);
    ctx.getBuilder().SetInsertPoint(bodyBB);
    body->codegen(ctx);

    ctx.getBuilder().SetInsertPoint(condBB);
    llvm::Value* condVal = cond->codegen(ctx);
    if (!condVal) return nullptr;
    ctx.getBuilder().CreateCondBr(condVal, bodyBB, endBB);

    ctx.getBuilder().SetInsertPoint(endBB);
    return nullptr;
}

llvm::Value* ForStmtAST::codegen(CodegenContext& ctx) {
    llvm::Function* func = ctx.getBuilder().GetInsertBlock()->getParent();
    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(ctx.getContext(), "for.cond", func);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(ctx.getContext(), "for.body", func);
    llvm::BasicBlock* incBB = llvm::BasicBlock::Create(ctx.getContext(), "for.inc", func);
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(ctx.getContext(), "for.end", func);

    if (init) init->codegen(ctx);
    ctx.getBuilder().CreateBr(condBB);

    ctx.getBuilder().SetInsertPoint(condBB);
    if (cond) {
        llvm::Value* condVal = cond->codegen(ctx);
        if (!condVal) return nullptr;
        ctx.getBuilder().CreateCondBr(condVal, bodyBB, endBB);
    } else {
        ctx.getBuilder().CreateBr(bodyBB);
    }

    ctx.getBuilder().SetInsertPoint(bodyBB);
    body->codegen(ctx);
    ctx.getBuilder().CreateBr(incBB);

    ctx.getBuilder().SetInsertPoint(incBB);
    if (inc) inc->codegen(ctx);
    ctx.getBuilder().CreateBr(condBB);

    ctx.getBuilder().SetInsertPoint(endBB);
    return nullptr;
}

llvm::Value* SwitchStmtAST::codegen(CodegenContext& ctx) {
    llvm::Value* condVal = cond->codegen(ctx);
    if (!condVal) return nullptr;

    auto& builder = ctx.getBuilder();
    llvm::Function* func = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(ctx.getContext(), "switch.end", func);

    llvm::SwitchInst* switchInst = builder.CreateSwitch(condVal, endBB, cases.size());
    for (auto& caseStmt : cases) {
        llvm::BasicBlock* caseBB = llvm::BasicBlock::Create(ctx.getContext(), "switch.case", func);
        builder.SetInsertPoint(caseBB);
        caseStmt->codegen(ctx);
        if (!builder.GetInsertBlock()->getTerminator()) {
            builder.CreateBr(endBB);
        }
    }

    builder.SetInsertPoint(endBB);
    return nullptr;
}

llvm::Value* BreakStmtAST::codegen(CodegenContext& ctx) {
    return ctx.getBuilder().CreateUnreachable();
}

llvm::Value* ContinueStmtAST::codegen(CodegenContext& ctx) {
    return ctx.getBuilder().CreateUnreachable();
}

llvm::Value* GotoStmtAST::codegen(CodegenContext& ctx) {
    return ctx.getBuilder().CreateUnreachable();
}

llvm::Value* LabelStmtAST::codegen(CodegenContext& ctx) {
    if (stmt) {
        return stmt->codegen(ctx);
    }
    return nullptr;
}

llvm::Value* NullStmtAST::codegen(CodegenContext& ctx) {
    return nullptr;
}


