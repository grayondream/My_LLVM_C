#pragma once

#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <unordered_map>
#include "sema/Diagnostic.h"
#include "ast/Symbol.h"
#include "ast/Type.h"
#include "ast/Expr.h"
#include "ast/Stmt.h"
#include "ast/Decl.h"

class SemanticAnalyzer {
public:
    struct ConstValue {
        enum Type { INT, DOUBLE, CHAR } type;
        union { int intVal; double doubleVal; char charVal; };
    };

    SemanticAnalyzer();

    void analyze(TranslationUnitAST& ast);
    const std::vector<Diagnostic>& getErrors() const;

    void enterScope();
    void exitScope();
    bool declare(const std::string& name, Type* type);
    Symbol* lookup(const std::string& name);

    Type* checkBinaryTypes(BinaryOp op, Type* left, Type* right, ExprAST& node);
    Type* checkAssignmentTypes(Type* lhs, Type* rhs, ExprAST& node);
    Type* checkFunctionCall(const std::string& name, const std::vector<std::unique_ptr<ExprAST>>& args, ExprAST& node);

    Type* getExprType(ExprAST& expr);
    std::optional<ConstValue> evaluateConstexpr(ExprAST* expr);
    const std::unordered_map<std::string, ConstValue>& getConstexprValues() const { return constexprValues; }

private:
    void emitError(const std::string& msg, const ASTNode& node);
    void emitWarning(const std::string& msg, const ASTNode& node);
    bool isIntegerType(Type* type) const;
    bool isFloatType(Type* type) const;
    bool isArithmeticType(Type* type) const;
    bool isPointerOrArray(Type* type) const;
    bool typesCompatible(Type* left, Type* right) const;
    Type* getCommonType(Type* left, Type* right) const;
    std::string typeToString(Type* type) const;
    std::string binaryOpToString(BinaryOp op) const;

    void visit(TranslationUnitAST& node);
    void visit(FunctionDeclAST& node);
    void visit(VarDeclAST& node);
    void visit(ArrayDeclAST& node);
    void visit(StructDeclAST& node);
    void visit(UnionDeclAST& node);
    void visit(EnumDeclAST& node);
    void visit(TypedefDeclAST& node);
    void visit(ForwardDeclAST& node);
    void visit(DeclStmtAST& node);
    void visit(CompoundStmtAST& node);
    void visit(ExprStmtAST& node);
    void visit(ReturnStmtAST& node);
    void visit(IfStmtAST& node);
    void visit(WhileStmtAST& node);
    void visit(DoWhileStmtAST& node);
    void visit(ForStmtAST& node);
    void visit(SwitchStmtAST& node);
    void visit(BreakStmtAST& node);
    void visit(ContinueStmtAST& node);
    void visit(GotoStmtAST& node);
    void visit(LabelStmtAST& node);
    void visit(NullStmtAST& node);

    void visit(NumberExprAST& node);
    void visit(FloatExprAST& node);
    void visit(CharExprAST& node);
    void visit(StringExprAST& node);
    void visit(VariableExprAST& node);
    void visit(BinaryExprAST& node);
    void visit(UnaryExprAST& node);
    void visit(CallExprAST& node);
    void visit(AssignmentExprAST& node);
    void visit(TernaryExprAST& node);
    void visit(CastExprAST& node);
    void visit(CommaExprAST& node);
    void visit(PostfixIncDecExprAST& node);
    void visit(ArrayAccessExprAST& node);
    void visit(MemberAccessExprAST& node);
    void visit(SizeofExprAST& node);
    void visit(InitializerListExprAST& node);

    void visit(ExprAST& expr);
    void visit(StmtAST& stmt);

    std::vector<Diagnostic> errors;
    std::unique_ptr<Scope> globalScope;
    Scope* currentScope;
    FunctionDeclAST* currentFunction;
    TypeContext* typeCtx;
    std::unordered_map<std::string, ConstValue> constexprValues;
};
