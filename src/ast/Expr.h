#pragma once

#include <string>
#include <memory>
#include <vector>
#include "Type.h"
#include "PrintFormat.h"
#include "support/LiteralKind.h"

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
    BitNot,
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
    long long value;
    // LEX-15: suffix/default kind of the literal, used by sema/codegen to pick
    // the fixed-width type instead of a fixed `int`/`i32`.
    LiteralKind literalKind{LiteralKind::Int};
    explicit NumberExprAST(int val) : value(val) {}
    NumberExprAST(long long val, LiteralKind kind) : value(val), literalKind(kind) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class FloatExprAST : public ExprAST {
public:
    double value;
    LiteralKind literalKind{LiteralKind::Float64};
    explicit FloatExprAST(double val) : value(val) {}
    FloatExprAST(double val, LiteralKind kind) : value(val), literalKind(kind) {}
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
    // Set by semantic analysis when the name refers to a function used as a
    // value: codegen then yields the function instead of a variable address.
    bool isFunctionRef = false;
    std::string resolvedFunctionName;
    // Set by semantic analysis when the name is an enumerator: codegen then
    // yields the integer constant instead of a variable address.
    bool isEnumConstant = false;
    int enumValue = 0;
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
    // P1-03 / GEN-03: 显式模板实参 `max<int32>(3, 4)`——类型与整型值按
    // 出现顺序分组；空且 hasTemplateArgs 时走纯推导。
    bool hasTemplateArgs = false;
    std::vector<Type*> explicitTemplateArgs;
    std::vector<long long> explicitTemplateValues;
    // Parameter types of the overload chosen by semantic analysis. Empty when
    // unresolved (e.g. codegen is run without semantic analysis).
    std::vector<Type*> resolvedParamTypes;
    // Set by semantic analysis for calls through a function-pointer variable.
    bool isIndirect = false;
    // Set by semantic analysis when this is the builtin `print`/`println`.
    bool isPrint = false;
    bool printNewline = false;
    std::string printCFormat;                 // final printf format string
    std::vector<PrintArgKind> printArgKinds;  // one per `{}` slot
    // Set by semantic analysis when this is the builtin terminator `assert` or
    // `panic` (STD-01 / STD-27 / DEC-21). Lowered to a stderr message + abort(),
    // with the call site's file:line embedded at compile time.
    bool isAssert = false;
    bool isPanic = false;

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

// How a cast was written. `CStyle` is the legacy `(T)x`; `Static`/`Reinterpret`
// are the explicit operators from LEX-11 / PAR-18 (DEC-18).
enum class CastKind {
    CStyle,
    Static,
    Reinterpret,
};

class CastExprAST : public ExprAST {
public:
    Type* castType;
    std::unique_ptr<ExprAST> expr;
    CastKind castKind;

    CastExprAST(Type* targetType, std::unique_ptr<ExprAST> e,
                CastKind kind = CastKind::CStyle)
        : castType(targetType), expr(std::move(e)), castKind(kind) {}
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

    // Declared parameter types (incl. this at index 0) filled by sema so
    // codegen mangles against the definition site. Mirror of CallExprAST.
    std::vector<Type*> resolvedParamTypes;

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
