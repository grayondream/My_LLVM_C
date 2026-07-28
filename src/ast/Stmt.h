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

class IfStmtAST : public StmtAST {
public:
    std::unique_ptr<ExprAST> cond;
    std::unique_ptr<StmtAST> thenStmt;
    std::unique_ptr<StmtAST> elseStmt;

    IfStmtAST(std::unique_ptr<ExprAST> condition,
              std::unique_ptr<StmtAST> then,
              std::unique_ptr<StmtAST> elseStmt = nullptr)
        : cond(std::move(condition)), thenStmt(std::move(then)), elseStmt(std::move(elseStmt)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class WhileStmtAST : public StmtAST {
public:
    std::unique_ptr<ExprAST> cond;
    std::unique_ptr<StmtAST> body;

    WhileStmtAST(std::unique_ptr<ExprAST> condition, std::unique_ptr<StmtAST> bodyStmt)
        : cond(std::move(condition)), body(std::move(bodyStmt)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class DoWhileStmtAST : public StmtAST {
public:
    std::unique_ptr<ExprAST> cond;
    std::unique_ptr<StmtAST> body;

    DoWhileStmtAST(std::unique_ptr<ExprAST> condition, std::unique_ptr<StmtAST> bodyStmt)
        : cond(std::move(condition)), body(std::move(bodyStmt)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class ForStmtAST : public StmtAST {
public:
    std::unique_ptr<StmtAST> init;
    std::unique_ptr<ExprAST> cond;
    std::unique_ptr<ExprAST> inc;
    std::unique_ptr<StmtAST> body;

    ForStmtAST(std::unique_ptr<StmtAST> initStmt,
               std::unique_ptr<ExprAST> condition,
               std::unique_ptr<ExprAST> increment,
               std::unique_ptr<StmtAST> bodyStmt)
        : init(std::move(initStmt)), cond(std::move(condition)), inc(std::move(increment)), body(std::move(bodyStmt)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class SwitchStmtAST : public StmtAST {
public:
    std::unique_ptr<ExprAST> cond;
    std::vector<std::unique_ptr<StmtAST>> cases;
    std::vector<llvm::Value*> caseValues;

    SwitchStmtAST(std::unique_ptr<ExprAST> condition, std::vector<std::unique_ptr<StmtAST>> caseStmts)
        : cond(std::move(condition)), cases(std::move(caseStmts)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class BreakStmtAST : public StmtAST {
public:
    BreakStmtAST() = default;
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class ContinueStmtAST : public StmtAST {
public:
    ContinueStmtAST() = default;
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class GotoStmtAST : public StmtAST {
public:
    std::string label;

    explicit GotoStmtAST(const std::string& l) : label(l) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class LabelStmtAST : public StmtAST {
public:
    std::string label;
    std::unique_ptr<StmtAST> stmt;

    LabelStmtAST(const std::string& l, std::unique_ptr<StmtAST> s)
        : label(l), stmt(std::move(s)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class NullStmtAST : public StmtAST {
public:
    NullStmtAST() = default;
    llvm::Value* codegen(CodegenContext& ctx) override;
};
