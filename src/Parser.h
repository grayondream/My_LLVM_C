#pragma once

#include <vector>
#include <memory>
#include <optional>
#include "Token.h"
#include "AST.h"

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

    std::unique_ptr<ExprAST> parseExpr();

    std::unique_ptr<NumberExprAST> parseNumberExpr();

    std::unique_ptr<BinaryExprAST> parseBinaryExpr();

private:
    std::vector<Token> m_tokens;
    size_t m_currentTokenPos{0};
};