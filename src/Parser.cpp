#include "Parser.h"
#include <memory>
#include "AST.h"
#include "Token.h"

BinaryOp TokenType2BinaryOp(TokenType type) {
    switch (type) {
        case TokenType::TOKEN_PLUS:
            return BinaryOp::Add;
        case TokenType::TOKEN_MINUS:
            return BinaryOp::Sub;
        default:
            return BinaryOp::Invalid;
    }
}

std::optional<Token> Parser::peek() {
    if (eof()) {
        return {};
    }

    return std::make_optional(m_tokens[m_currentTokenPos]);
}

std::optional<Token> Parser::advance() {
    if (eof()) {
        return {};
    }

    return std::make_optional(m_tokens[m_currentTokenPos++]);
}

bool Parser::eof() const {
    return m_currentTokenPos >= m_tokens.size();
}

std::optional<Token> Parser::match(TokenType type) {
    if (eof()) {
        return {};
    }

    auto token = peek();
    if (token->type == type) {
        advance();
        return token;
    }

    return {};
}   

std::unique_ptr<FunctionDeclAST> Parser::parseFunctionDecl() {
    if (!match(TokenType::TOKEN_INT)) {
        return nullptr;
    }

    Token id;
    if (auto token = match(TokenType::TOKEN_IDENTIFIER); token.has_value()) {
        id = token.value();
    } else {
        return nullptr;
    }

    if (!match(TokenType::TOKEN_LPAREN)) {
        return nullptr;
    }

    if (!match(TokenType::TOKEN_RPAREN)) {
        return nullptr;
    }
    
    auto body = parseCompoundStmt();
    if (!body) {
        return nullptr;
    }

    std::vector<std::unique_ptr<ParamDeclAST>> params{};
    return std::make_unique<FunctionDeclAST>(id.lexeme, TypeContext::instance().getInt(), params, body);
}

std::unique_ptr<ReturnStmtAST> Parser::parseReturnStmt() {
    if (!match(TokenType::TOKEN_RETURN)) {
        return nullptr;
    }

    auto value = parseExpr();
    if (!value) {
        return nullptr;
    }

    match(TokenType::TOKEN_SEMICOLON);
    return std::make_unique<ReturnStmtAST>(std::move(value));
}

std::unique_ptr<CompoundStmtAST> Parser::parseCompoundStmt() {
    if (!match(TokenType::TOKEN_LBRACE)) {
        return nullptr;
    }

    std::vector<std::unique_ptr<StmtAST>> stmts{};
    while(!match(TokenType::TOKEN_RBRACE)) {
        stmts.push_back(parseStmt());
    }

    return std::make_unique<CompoundStmtAST>(std::move(stmts));
}

std::unique_ptr<StmtAST> Parser::parseStmt() {
    if (match(TokenType::TOKEN_RETURN)) {
        auto value = parseExpr();
        match(TokenType::TOKEN_SEMICOLON);
        return std::make_unique<ReturnStmtAST>(std::move(value));
    }

    return nullptr;
}

std::unique_ptr<ExprAST> Parser::parseExpr() {
    return parseBinaryExpr();
}

std::unique_ptr<BinaryExprAST> Parser::parseBinaryExpr() {
    auto number1 = parseNumberExpr();
    if (!number1) {
        return nullptr;
    }

    auto op = match(TokenType::TOKEN_PLUS);
    if (!op) {
        return nullptr;
    }

    auto number2 = parseNumberExpr();
    if (!number2) {
        return nullptr;
    }
    
    return std::make_unique<BinaryExprAST>(TokenType2BinaryOp(op.value().type), std::move(number1), std::move(number2));
}

std::unique_ptr<NumberExprAST> Parser::parseNumberExpr() {
    if (auto token = match(TokenType::TOKEN_NUMBER); token.has_value()) {
        int value = std::get<int>(token.value().value);
        advance();
        return std::make_unique<NumberExprAST>(value);
    }

    return nullptr;
}



std::unique_ptr<TranslationUnitAST> Parser::parse() {
    std::vector<std::unique_ptr<DeclAST>> decls{};
    decls.push_back(parseFunctionDecl());
    return std::make_unique<TranslationUnitAST>(std::move(decls));
}




