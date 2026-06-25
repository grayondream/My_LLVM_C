#pragma once

#include <string>
#include <memory>
#include "Type.h"

#include "llvm/IR/Value.h"

class CodegenContext;

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
    std::string sourceFile;
    int sourceLine{0};
    int sourceColumn{0};

    virtual ~ASTNode() = default;

    void setLocation(const std::string& file, int line, int col = 0) {
        sourceFile = file;
        sourceLine = line;
        sourceColumn = col;
    }
};

class ExprAST : public ASTNode {
public:
    Type* type;
    bool isLValue;
    virtual llvm::Value* codegen(CodegenContext& ctx) = 0;
};

class NumberExprAST : public ExprAST {
public:
    int value;
    explicit NumberExprAST(int val) : value(val) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class FloatExprAST : public ExprAST {
public:
    double value;
    explicit FloatExprAST(double val) : value(val) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class CharExprAST : public ExprAST {
public:
    char value;
    explicit CharExprAST(char val) : value(val) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class StringExprAST : public ExprAST {
public:
    std::string value;
    explicit StringExprAST(const std::string& val) : value(val) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class VariableExprAST : public ExprAST {
public:
    std::string name;
    explicit VariableExprAST(const std::string& n) : name(n) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class BinaryExprAST : public ExprAST {
public:
    BinaryOp op;
    std::unique_ptr<ExprAST> left;
    std::unique_ptr<ExprAST> right;

    BinaryExprAST(BinaryOp oper, std::unique_ptr<ExprAST> l, std::unique_ptr<ExprAST> r)
        : op(oper), left(std::move(l)), right(std::move(r)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class UnaryExprAST : public ExprAST {
public:
    UnaryOp op;
    std::unique_ptr<ExprAST> operand;

    UnaryExprAST(UnaryOp oper, std::unique_ptr<ExprAST> expr)
        : op(oper), operand(std::move(expr)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class CallExprAST : public ExprAST {
public:
    std::string callee;
    std::vector<std::unique_ptr<ExprAST>> args;

    CallExprAST(const std::string& name, std::vector<std::unique_ptr<ExprAST>> arguments)
        : callee(name), args(std::move(arguments)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};
