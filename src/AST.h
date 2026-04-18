#pragma once

#include <string>
#include <memory>
#include <vector>
#include "Type.h"

enum class BinaryOp {
    Invalid,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Eq,
    NotEq,
    Lt,
    Gt,
    Le,
    Ge,
    And,
    Or,
    BitAnd,
    BitOr,
};

enum class UnaryOp {
    Plus,
    Minus,
    Not,
    Deref,
    AddressOf,
};

class ASTNode {
public:
    virtual ~ASTNode() = default;
};

class ExprAST : public ASTNode {
public:
    Type* type;
    bool isLValue;
};

class StmtAST : public ASTNode {
public:
};

class DeclAST : public ASTNode {
public:
};

class NumberExprAST : public ExprAST {
public:
    int value;
    explicit NumberExprAST(int val) : value(val) {}
};

class FloatExprAST : public ExprAST {
public:
    double value;
    explicit FloatExprAST(double val) : value(val) {}
};

class CharExprAST : public ExprAST {
public:
    char value;
    explicit CharExprAST(char val) : value(val) {}
};

class StringExprAST : public ExprAST {
public:
    std::string value;
    explicit StringExprAST(const std::string& val) : value(val) {}
};

class VariableExprAST : public ExprAST {
public:
    std::string name;
    explicit VariableExprAST(const std::string& n) : name(n) {}
};

class BinaryExprAST : public ExprAST {
public:
    BinaryOp op;
    std::unique_ptr<ExprAST> left;
    std::unique_ptr<ExprAST> right;

    BinaryExprAST(BinaryOp oper, std::unique_ptr<ExprAST> l, std::unique_ptr<ExprAST> r)
        : op(oper), left(std::move(l)), right(std::move(r)) {}
};

class UnaryExprAST : public ExprAST {
public:
    UnaryOp op;
    std::unique_ptr<ExprAST> operand;

    UnaryExprAST(UnaryOp oper, std::unique_ptr<ExprAST> expr)
        : op(oper), operand(std::move(expr)) {}
};

class CallExprAST : public ExprAST {
public:
    std::string callee;
    std::vector<std::unique_ptr<ExprAST>> args;

    CallExprAST(const std::string& name, std::vector<std::unique_ptr<ExprAST>> arguments)
        : callee(name), args(std::move(arguments)) {}
};

class ExprStmtAST : public StmtAST {
public:
    std::unique_ptr<ExprAST> expr;

    explicit ExprStmtAST(std::unique_ptr<ExprAST> e) : expr(std::move(e)) {}
};

class CompoundStmtAST : public StmtAST {
public:
    std::vector<std::unique_ptr<StmtAST>> stmts;

    explicit CompoundStmtAST(std::vector<std::unique_ptr<StmtAST>> statements)
        : stmts(std::move(statements)) {}
};

class ReturnStmtAST : public StmtAST {
public:
    std::unique_ptr<ExprAST> value;

    explicit ReturnStmtAST(std::unique_ptr<ExprAST> val) : value(std::move(val)) {}
};

class DeclStmtAST : public StmtAST {
public:
    std::unique_ptr<DeclAST> decl;

    explicit DeclStmtAST(std::unique_ptr<DeclAST> d) : decl(std::move(d)) {}
};

class VarDeclAST : public DeclAST {
public:
    std::string name;
    Type* type;
    std::unique_ptr<ExprAST> initExpr;

    VarDeclAST(const std::string& n, Type* t, std::unique_ptr<ExprAST> init = nullptr)
        : name(n), type(t), initExpr(std::move(init)) {}
};

class ParamDeclAST : public ASTNode {
public:
    std::string name;
    Type* type;

    ParamDeclAST(const std::string& n, Type* t)
        : name(n), type(t) {}
};

class FunctionDeclAST : public DeclAST {
public:
    std::string name;
    Type* returnType;
    std::vector<std::unique_ptr<ParamDeclAST>> params;
    std::unique_ptr<CompoundStmtAST> body;

    FunctionDeclAST(const std::string& n, Type* ret,
                    std::vector<std::unique_ptr<ParamDeclAST>>& parameters,
                    std::unique_ptr<CompoundStmtAST>& b)
        : name(n), returnType(ret), params(std::move(parameters)), body(std::move(b)) {}
};

class TranslationUnitAST : public ASTNode {
public:
    std::vector<std::unique_ptr<DeclAST>> declarations;

    explicit TranslationUnitAST(std::vector<std::unique_ptr<DeclAST>> decls)
        : declarations(std::move(decls)) {}
};
