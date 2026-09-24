#include "Stmt.h"
#include "Expr.h"
#include "codegen/CodegenContext.h"
#include "support/Log.h"

namespace {

// Evaluate a controlling expression. Variable references codegen to their
// address, so load lvalues before turning the value into an i1 condition.
llvm::Value* evalCondition(CodegenContext& ctx, ExprAST& expr) {
    llvm::Value* v = expr.codegen(ctx);
    if (!v) return nullptr;
    if (expr.isLValue && expr.type && expr.type->kind != TypeKind::Array) {
        v = ctx.loadValue(v, expr.type);
    }
    return ctx.coerceToBool(v);
}

} // namespace

llvm::Value* ExprStmtAST::codegen(CodegenContext& ctx) {
    if (expr) {
        expr->codegen(ctx);
    }
    return nullptr;
}

llvm::Value* CompoundStmtAST::codegen(CodegenContext& ctx) {
    llvm::Value* last = nullptr;
    ctx.pushDeferScope();
    for (auto& stmt : stmts) {
        // Stop generating the (unreachable) rest of the block once a
        // statement terminated it, e.g. after a return or break.
        if (ctx.getBuilder().GetInsertBlock()->getTerminator()) {
            last = nullptr;
            break;
        }
        last = stmt->codegen(ctx);
    }

    if (ctx.getBuilder().GetInsertBlock()->getTerminator()) {
        // A leaving statement (return/break/continue) already emitted the
        // pending defers, so this scope must not emit them a second time.
        ctx.discardDeferScope();
    } else {
        ctx.popDeferScope();
    }
    return last;
}

llvm::Value* ReturnStmtAST::codegen(CodegenContext& ctx) {
    if (!value) {
        ctx.emitAllDefers();
        ctx.getBuilder().CreateRetVoid();
        return nullptr;
    }
    llvm::Value* v = value->codegen(ctx);
    if (!v) return nullptr;
    // An lvalue return operand denotes a location; load its value (a pointer
    // rvalue such as `"str"` or a call result is already the value).
    if (value->isLValue) {
        Type* valType = value->type;
        if (!valType) {
            auto* varExpr = dynamic_cast<VariableExprAST*>(value.get());
            if (varExpr) {
                Symbol* sym = ctx.currentScope()->lookup(varExpr->name);
                if (sym) valType = sym->type;
            }
        }
        if (valType) {
            v = ctx.loadValue(v, valType);
        }
    }
    llvm::Function* func = ctx.getBuilder().GetInsertBlock()->getParent();
    if (func) {
        v = ctx.castValue(v, func->getReturnType());
    }
    // Run all deferred calls (innermost scope first) before leaving the function.
    ctx.emitAllDefers();
    ctx.getBuilder().CreateRet(v);
    return v;
}

llvm::Value* IfStmtAST::codegen(CodegenContext& ctx) {
    llvm::Value* condVal = evalCondition(ctx, *cond);
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

    ctx.pushBreakBlock(endBB);
    ctx.pushContinueBlock(condBB);

    ctx.getBuilder().CreateBr(condBB);
    ctx.getBuilder().SetInsertPoint(condBB);

    llvm::Value* condVal = evalCondition(ctx, *cond);
    if (!condVal) return nullptr;
    ctx.getBuilder().CreateCondBr(condVal, bodyBB, endBB);

    ctx.getBuilder().SetInsertPoint(bodyBB);
    body->codegen(ctx);
    if (!ctx.getBuilder().GetInsertBlock()->getTerminator()) {
        ctx.getBuilder().CreateBr(condBB);
    }

    ctx.popContinueBlock();
    ctx.popBreakBlock();

    ctx.getBuilder().SetInsertPoint(endBB);
    return nullptr;
}

llvm::Value* DoWhileStmtAST::codegen(CodegenContext& ctx) {
    llvm::Function* func = ctx.getBuilder().GetInsertBlock()->getParent();
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(ctx.getContext(), "dowhile.body", func);
    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(ctx.getContext(), "dowhile.cond", func);
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(ctx.getContext(), "dowhile.end", func);

    ctx.pushBreakBlock(endBB);
    ctx.pushContinueBlock(condBB);

    ctx.getBuilder().CreateBr(bodyBB);
    ctx.getBuilder().SetInsertPoint(bodyBB);
    body->codegen(ctx);
    if (!ctx.getBuilder().GetInsertBlock()->getTerminator()) {
        ctx.getBuilder().CreateBr(condBB);
    }

    ctx.getBuilder().SetInsertPoint(condBB);
    llvm::Value* condVal = evalCondition(ctx, *cond);
    if (!condVal) return nullptr;
    ctx.getBuilder().CreateCondBr(condVal, bodyBB, endBB);

    ctx.popContinueBlock();
    ctx.popBreakBlock();

    ctx.getBuilder().SetInsertPoint(endBB);
    return nullptr;
}

llvm::Value* ForStmtAST::codegen(CodegenContext& ctx) {
    llvm::Function* func = ctx.getBuilder().GetInsertBlock()->getParent();
    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(ctx.getContext(), "for.cond", func);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(ctx.getContext(), "for.body", func);
    llvm::BasicBlock* incBB = llvm::BasicBlock::Create(ctx.getContext(), "for.inc", func);
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(ctx.getContext(), "for.end", func);

    ctx.pushBreakBlock(endBB);
    ctx.pushContinueBlock(incBB);

    if (init) init->codegen(ctx);
    ctx.getBuilder().CreateBr(condBB);

    ctx.getBuilder().SetInsertPoint(condBB);
    if (cond) {
        llvm::Value* condVal = evalCondition(ctx, *cond);
        if (!condVal) return nullptr;
        ctx.getBuilder().CreateCondBr(condVal, bodyBB, endBB);
    } else {
        ctx.getBuilder().CreateBr(bodyBB);
    }

    ctx.getBuilder().SetInsertPoint(bodyBB);
    body->codegen(ctx);
    if (!ctx.getBuilder().GetInsertBlock()->getTerminator()) {
        ctx.getBuilder().CreateBr(incBB);
    }

    ctx.getBuilder().SetInsertPoint(incBB);
    if (inc) inc->codegen(ctx);
    ctx.getBuilder().CreateBr(condBB);

    ctx.popContinueBlock();
    ctx.popBreakBlock();

    ctx.getBuilder().SetInsertPoint(endBB);
    return nullptr;
}

llvm::Value* SwitchStmtAST::codegen(CodegenContext& ctx) {
    llvm::Value* condVal = cond->codegen(ctx);
    if (!condVal) return nullptr;
    if (cond->isLValue) {
        condVal = ctx.loadValue(condVal, cond->type);
    }

    auto& builder = ctx.getBuilder();
    llvm::Function* func = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(ctx.getContext(), "switch.end", func);

    ctx.pushBreakBlock(endBB);

    std::vector<llvm::BasicBlock*> caseBBs;
    caseBBs.reserve(cases.size());
    for (size_t i = 0; i < cases.size(); ++i) {
        caseBBs.push_back(llvm::BasicBlock::Create(ctx.getContext(), "switch.case", func));
    }

    // Evaluate labels before creating the switch (they must be constants and
    // must not be inserted after the terminator).
    std::vector<llvm::ConstantInt*> caseConsts(cases.size(), nullptr);
    for (size_t i = 0; i < cases.size(); ++i) {
        if (i >= caseLabels.size() || !caseLabels[i]) continue;  // default
        llvm::Value* labelVal = caseLabels[i]->codegen(ctx);
        auto* constInt = llvm::dyn_cast_or_null<llvm::ConstantInt>(labelVal);
        if (!constInt) continue;
        if (constInt->getType() != condVal->getType()) {
            constInt = llvm::dyn_cast<llvm::ConstantInt>(
                llvm::ConstantInt::get(condVal->getType(), constInt->getValue()));
        }
        caseConsts[i] = constInt;
    }

    llvm::SwitchInst* switchInst = builder.CreateSwitch(condVal, endBB, cases.size());
    for (size_t i = 0; i < cases.size(); ++i) {
        if (caseConsts[i]) {
            switchInst->addCase(caseConsts[i], caseBBs[i]);
        } else {
            switchInst->setDefaultDest(caseBBs[i]);
        }
    }

    // Emit the bodies in source order, falling through to the next case (C
    // semantics) unless the body already terminated.
    for (size_t i = 0; i < cases.size(); ++i) {
        builder.SetInsertPoint(caseBBs[i]);
        cases[i]->codegen(ctx);
        if (!builder.GetInsertBlock()->getTerminator()) {
            llvm::BasicBlock* next = (i + 1 < cases.size()) ? caseBBs[i + 1] : endBB;
            builder.CreateBr(next);
        }
    }

    ctx.popBreakBlock();
    builder.SetInsertPoint(endBB);
    return nullptr;
}

llvm::Value* BreakStmtAST::codegen(CodegenContext& ctx) {
    llvm::BasicBlock* breakBB = ctx.getBreakBlock();
    if (!breakBB) {
        LOGE("break statement outside of loop/switch");
        return ctx.getBuilder().CreateUnreachable();
    }
    // Run defers registered inside the loop/switch body before leaving it.
    ctx.emitDefersFrom(ctx.getBreakDeferBoundary());
    return ctx.getBuilder().CreateBr(breakBB);
}

llvm::Value* ContinueStmtAST::codegen(CodegenContext& ctx) {
    llvm::BasicBlock* continueBB = ctx.getContinueBlock();
    if (!continueBB) {
        LOGE("continue statement outside of loop");
        return ctx.getBuilder().CreateUnreachable();
    }
    // Run defers registered inside the loop body before the next iteration.
    ctx.emitDefersFrom(ctx.getContinueDeferBoundary());
    return ctx.getBuilder().CreateBr(continueBB);
}

llvm::Value* GotoStmtAST::codegen(CodegenContext& ctx) {
    llvm::BasicBlock* labelBB = ctx.getLabel(label);
    if (!labelBB) {
        llvm::Function* func = ctx.getBuilder().GetInsertBlock()->getParent();
        labelBB = llvm::BasicBlock::Create(ctx.getContext(), "label." + label, func);
        ctx.addLabel(label, labelBB);
    }
    return ctx.getBuilder().CreateBr(labelBB);
}

llvm::Value* LabelStmtAST::codegen(CodegenContext& ctx) {
    llvm::BasicBlock* existingBB = ctx.getLabel(label);
    llvm::BasicBlock* labelBB;
    if (existingBB) {
        labelBB = existingBB;
    } else {
        llvm::Function* func = ctx.getBuilder().GetInsertBlock()->getParent();
        labelBB = llvm::BasicBlock::Create(ctx.getContext(), "label." + label, func);
        ctx.addLabel(label, labelBB);
    }

    if (!ctx.getBuilder().GetInsertBlock()->getTerminator()) {
        ctx.getBuilder().CreateBr(labelBB);
    }
    ctx.getBuilder().SetInsertPoint(labelBB);

    if (stmt) {
        return stmt->codegen(ctx);
    }
    return nullptr;
}

llvm::Value* NullStmtAST::codegen(CodegenContext& ctx) {
    return nullptr;
}


