#pragma once

#include <vector>
#include <memory>
#include <optional>
#include "frontend/Token.h"
#include "ast/Decl.h"
#include "sema/Diagnostic.h"

class Parser{
public:
    Parser(const std::vector<Token>& tokens) : m_tokens(std::move(tokens)) {}

    std::unique_ptr<TranslationUnitAST> parse();
    const std::vector<Diagnostic>& getErrors() const;

private:
    void error(const std::string& msg, const Token& token);
    void errorUnexpected(const std::string& expected);
    void errorUnexpectedEOF(const std::string& expected);
    bool expect(TokenType type, const std::string& msg);
    std::string tokenTypeName(TokenType type) const;
    bool eof() const;

    std::optional<Token> match(TokenType type);

    std::optional<Token> peek() const;

    std::optional<Token> advance();

    bool check(TokenType type) const;

    bool isTypeStart() const;

    Type* parseType();

    Type* parseBaseType();

    std::unique_ptr<DeclAST> parseDeclaration();

    std::unique_ptr<FunctionDeclAST> parseFunctionDecl(Type* returnType, const std::string& name);

    std::unique_ptr<DeclAST> parseVariableDecl(Type* type, const std::string& name);

    std::unique_ptr<ParamDeclAST> parseParamDecl();

    std::unique_ptr<StructDeclAST> parseStructDecl();

    std::unique_ptr<UnionDeclAST> parseUnionDecl();

    std::unique_ptr<EnumDeclAST> parseEnumDecl();

    std::unique_ptr<TypedefDeclAST> parseTypedefDecl();

    std::unique_ptr<ReturnStmtAST> parseReturnStmt();

    std::unique_ptr<CompoundStmtAST> parseCompoundStmt();

    std::unique_ptr<StmtAST> parseStmt();

    std::unique_ptr<StmtAST> parseIfStmt();

    std::unique_ptr<StmtAST> parseWhileStmt();

    std::unique_ptr<StmtAST> parseDoWhileStmt();

    std::unique_ptr<StmtAST> parseForStmt();

    std::unique_ptr<StmtAST> parseBreakStmt();

    std::unique_ptr<StmtAST> parseContinueStmt();

    std::unique_ptr<StmtAST> parseGotoStmt();

    std::unique_ptr<StmtAST> parseLabelStmt(const std::string& label);

    std::unique_ptr<StmtAST> parseExprStmt();

    std::unique_ptr<ExprAST> parseExpr(int minPrec = 0);

    std::unique_ptr<ExprAST> parsePrimary();

    std::unique_ptr<ExprAST> parseUnary();

    std::unique_ptr<ExprAST> parsePostfix(std::unique_ptr<ExprAST> lhs);

    int getPrecedence(TokenType op) const;

    bool isRightAssociative(TokenType op) const;

    BinaryOp tokenTypeToBinaryOp(TokenType type) const;

    AssignOp tokenTypeToAssignOp(TokenType type) const;

private:
    std::vector<Token> m_tokens;
    size_t m_currentTokenPos{0};
    std::vector<Diagnostic> m_errors;
};