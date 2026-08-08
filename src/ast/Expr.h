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
    BitXor,
    LShift,
    RShift,
};

enum class UnaryOp {
    Plus,
    Minus,
    Not,
    Deref,
    AddressOf,
    PreInc,
    PreDec,
    Sizeof,
};

enum class AssignOp {
    Assign,
    AddAssign,
    SubAssign,
    MulAssign,
    DivAssign,
    ModAssign,
    BitAndAssign,
    BitOrAssign,
    BitXorAssign,
    LShiftAssign,
    RShiftAssign,
};

enum class MemberAccessKind {
    Dot,
    Arrow,
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
    Type* type{nullptr};
    bool isLValue{false};
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
    std::string mangledCallee;  // Set by sema for operator overloading

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

class AssignmentExprAST : public ExprAST {
public:
    AssignOp op;
    std::unique_ptr<ExprAST> lhs;
    std::unique_ptr<ExprAST> rhs;

    AssignmentExprAST(AssignOp oper, std::unique_ptr<ExprAST> left, std::unique_ptr<ExprAST> right)
        : op(oper), lhs(std::move(left)), rhs(std::move(right)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class TernaryExprAST : public ExprAST {
public:
    std::unique_ptr<ExprAST> cond;
    std::unique_ptr<ExprAST> then;
    std::unique_ptr<ExprAST> elseExpr;

    TernaryExprAST(std::unique_ptr<ExprAST> condition,
                   std::unique_ptr<ExprAST> thenExpr,
                   std::unique_ptr<ExprAST> elseExpr)
        : cond(std::move(condition)), then(std::move(thenExpr)), elseExpr(std::move(elseExpr)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class CastExprAST : public ExprAST {
public:
    Type* castType;
    std::unique_ptr<ExprAST> expr;

    CastExprAST(Type* targetType, std::unique_ptr<ExprAST> e)
        : castType(targetType), expr(std::move(e)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class CommaExprAST : public ExprAST {
public:
    std::unique_ptr<ExprAST> left;
    std::unique_ptr<ExprAST> right;

    CommaExprAST(std::unique_ptr<ExprAST> l, std::unique_ptr<ExprAST> r)
        : left(std::move(l)), right(std::move(r)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class PostfixIncDecExprAST : public ExprAST {
public:
    std::unique_ptr<ExprAST> operand;
    bool isIncrement;

    PostfixIncDecExprAST(std::unique_ptr<ExprAST> expr, bool inc)
        : operand(std::move(expr)), isIncrement(inc) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class ArrayAccessExprAST : public ExprAST {
public:
    std::unique_ptr<ExprAST> array;
    std::unique_ptr<ExprAST> index;

    ArrayAccessExprAST(std::unique_ptr<ExprAST> arr, std::unique_ptr<ExprAST> idx)
        : array(std::move(arr)), index(std::move(idx)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class MemberAccessExprAST : public ExprAST {
public:
    MemberAccessKind accessKind;
    std::unique_ptr<ExprAST> object;
    std::string memberName;

    MemberAccessExprAST(MemberAccessKind kind, std::unique_ptr<ExprAST> obj, const std::string& member)
        : accessKind(kind), object(std::move(obj)), memberName(member) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class MethodCallExprAST : public ExprAST {
public:
    std::unique_ptr<ExprAST> object;
    std::string methodName;
    std::vector<std::unique_ptr<ExprAST>> args;

    MethodCallExprAST(std::unique_ptr<ExprAST> obj, const std::string& method,
                      std::vector<std::unique_ptr<ExprAST>> arguments)
        : object(std::move(obj)), methodName(method), args(std::move(arguments)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class SizeofExprAST : public ExprAST {
public:
    Type* sizeofType;
    std::unique_ptr<ExprAST> expr;

    SizeofExprAST(Type* type, std::unique_ptr<ExprAST> e = nullptr)
        : sizeofType(type), expr(std::move(e)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class InitializerListExprAST : public ExprAST {
public:
    std::vector<std::unique_ptr<ExprAST>> initializers;

    InitializerListExprAST(std::vector<std::unique_ptr<ExprAST>> initList)
        : initializers(std::move(initList)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};
