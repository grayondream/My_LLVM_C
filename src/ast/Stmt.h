#pragma once

#include <memory>
#include <vector>
#include "Expr.h"

class StmtAST : public ASTNode {
public:
    virtual llvm::Value* codegen(CodegenContext& ctx) = 0;
};

class ExprStmtAST : public StmtAST {
public:
    std::unique_ptr<ExprAST> expr;

    explicit ExprStmtAST(std::unique_ptr<ExprAST> e) : expr(std::move(e)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class CompoundStmtAST : public StmtAST {
public:
    std::vector<std::unique_ptr<StmtAST>> stmts;

    explicit CompoundStmtAST(std::vector<std::unique_ptr<StmtAST>> statements)
        : stmts(std::move(statements)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class ReturnStmtAST : public StmtAST {
public:
    std::unique_ptr<ExprAST> value;

    explicit ReturnStmtAST(std::unique_ptr<ExprAST> val) : value(std::move(val)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};
