#pragma once

#include <vector>
#include <memory>
#include <optional>
#include "frontend/Token.h"
#include "ast/Decl.h"

class Parser{
public:
    Parser(const std::vector<Token>& tokens) : m_tokens(std::move(tokens)) {}

    std::unique_ptr<TranslationUnitAST> parse();

private:
    bool eof() const;

    std::optional<Token> match(TokenType type);

    std::optional<Token> peek();

    std::optional<Token> advance();

    std::unique_ptr<FunctionDeclAST> parseFunctionDecl();

    std::unique_ptr<ReturnStmtAST> parseReturnStmt();

    std::unique_ptr<CompoundStmtAST> parseCompoundStmt();

    std::unique_ptr<StmtAST> parseStmt();

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
};