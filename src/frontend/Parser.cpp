#include "Parser.h"
#include <memory>
#include "ast/Decl.h"
#include "frontend/Token.h"

const std::vector<Diagnostic>& Parser::getErrors() const {
    return m_errors;
}

void Parser::error(const std::string& msg, const Token& token) {
    m_errors.emplace_back(Diagnostic::Level::Error, msg, token.filename, token.line, token.column);
}

void Parser::errorUnexpected(const std::string& expected) {
    auto token = peek();
    if (token) {
        std::string msg = "unexpected token '" + token->lexeme + "', " + expected;
        error(msg, *token);
    } else {
        errorUnexpectedEOF(expected);
    }
}

void Parser::errorUnexpectedEOF(const std::string& expected) {
    Token dummy;
    dummy.filename = m_tokens.empty() ? "" : m_tokens.back().filename;
    dummy.line = m_tokens.empty() ? 0 : m_tokens.back().line;
    dummy.column = m_tokens.empty() ? 0 : m_tokens.back().column;
    error("unexpected end of file, " + expected, dummy);
}

bool Parser::expect(TokenType type, const std::string& msg) {
    if (match(type)) {
        return true;
    }
    auto token = peek();
    if (token) {
        error(msg, *token);
    } else {
        errorUnexpectedEOF(msg);
    }
    return false;
}

std::string Parser::tokenTypeName(TokenType type) const {
    switch (type) {
        case TokenType::TOKEN_IDENTIFIER:    return "identifier";
        case TokenType::TOKEN_NUMBER:        return "number";
        case TokenType::TOKEN_STRING:        return "string literal";
        case TokenType::TOKEN_CHAR:          return "character literal";
        case TokenType::TOKEN_INT:           return "'int'";
        case TokenType::TOKEN_FLOAT:         return "'float'";
        case TokenType::TOKEN_DOUBLE:        return "'double'";
        case TokenType::TOKEN_CHAR_KW:       return "'char'";
        case TokenType::TOKEN_VOID:          return "'void'";
        case TokenType::TOKEN_IF:            return "'if'";
        case TokenType::TOKEN_ELSE:          return "'else'";
        case TokenType::TOKEN_FOR:           return "'for'";
        case TokenType::TOKEN_WHILE:         return "'while'";
        case TokenType::TOKEN_DO:            return "'do'";
        case TokenType::TOKEN_RETURN:        return "'return'";
        case TokenType::TOKEN_BREAK:         return "'break'";
        case TokenType::TOKEN_CONTINUE:      return "'continue'";
        case TokenType::TOKEN_STRUCT:        return "'struct'";
        case TokenType::TOKEN_UNION:         return "'union'";
        case TokenType::TOKEN_ENUM:          return "'enum'";
        case TokenType::TOKEN_TYPEDEF:       return "'typedef'";
        case TokenType::TOKEN_SIZEOF:        return "'sizeof'";
        case TokenType::TOKEN_SEMICOLON:     return "';'";
        case TokenType::TOKEN_COMMA:         return "','";
        case TokenType::TOKEN_LBRACE:        return "'{'";
        case TokenType::TOKEN_RBRACE:        return "'}'";
        case TokenType::TOKEN_LPAREN:        return "'('";
        case TokenType::TOKEN_RPAREN:        return "')'";
        case TokenType::TOKEN_LBRACKET:      return "'['";
        case TokenType::TOKEN_RBRACKET:      return "']'";
        case TokenType::TOKEN_ASSIGN:        return "'='";
        case TokenType::TOKEN_COLON:         return "':'";
        case TokenType::TOKEN_EOS:           return "end of file";
        default:                             return "token";
    }
}

std::optional<Token> Parser::peek() const {
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

// ========== Precedence & Associativity ==========

int Parser::getPrecedence(TokenType op) const {
    switch (op) {
        case TokenType::TOKEN_COMMA:        return 1;   // ,
        case TokenType::TOKEN_ASSIGN:
        case TokenType::TOKEN_PLUS_EQ:
        case TokenType::TOKEN_MINUS_EQ:
        case TokenType::TOKEN_STAR_EQ:
        case TokenType::TOKEN_SLASH_EQ:
        case TokenType::TOKEN_PERCENT_EQ:
        case TokenType::TOKEN_AMP_EQ:
        case TokenType::TOKEN_PIPE_EQ:
        case TokenType::TOKEN_CARET_EQ:
        case TokenType::TOKEN_LSHIFT_EQ:
        case TokenType::TOKEN_RSHIFT_EQ:    return 2;   // = += -= *= /= %= &= |= ^= <<= >>=
        case TokenType::TOKEN_QUESTION:     return 3;   // ?:
        case TokenType::TOKEN_OR:           return 4;   // ||
        case TokenType::TOKEN_AND:          return 5;   // &&
        case TokenType::TOKEN_BIT_OR:       return 6;   // |
        case TokenType::TOKEN_CARET:        return 7;   // ^
        case TokenType::TOKEN_BIT_AND:      return 8;   // &
        case TokenType::TOKEN_EQ:
        case TokenType::TOKEN_NOT_EQ:       return 9;   // == !=
        case TokenType::TOKEN_LT:
        case TokenType::TOKEN_GT:
        case TokenType::TOKEN_LE:
        case TokenType::TOKEN_GE:           return 10;  // < > <= >=
        case TokenType::TOKEN_LSHIFT:
        case TokenType::TOKEN_RSHIFT:       return 11;  // << >>
        case TokenType::TOKEN_PLUS:
        case TokenType::TOKEN_MINUS:        return 12;  // + -
        case TokenType::TOKEN_STAR:
        case TokenType::TOKEN_SLASH:
        case TokenType::TOKEN_PERCENT:      return 13;  // * / %
        default:                            return 0;   // not an infix operator
    }
}

bool Parser::isRightAssociative(TokenType op) const {
    switch (op) {
        case TokenType::TOKEN_ASSIGN:
        case TokenType::TOKEN_PLUS_EQ:
        case TokenType::TOKEN_MINUS_EQ:
        case TokenType::TOKEN_STAR_EQ:
        case TokenType::TOKEN_SLASH_EQ:
        case TokenType::TOKEN_PERCENT_EQ:
        case TokenType::TOKEN_AMP_EQ:
        case TokenType::TOKEN_PIPE_EQ:
        case TokenType::TOKEN_CARET_EQ:
        case TokenType::TOKEN_LSHIFT_EQ:
        case TokenType::TOKEN_RSHIFT_EQ:
            return true;
        default:
            return false;
    }
}

// ========== Token to Operator Conversion ==========

BinaryOp Parser::tokenTypeToBinaryOp(TokenType type) const {
    switch (type) {
        case TokenType::TOKEN_PLUS:     return BinaryOp::Add;
        case TokenType::TOKEN_MINUS:    return BinaryOp::Sub;
        case TokenType::TOKEN_STAR:     return BinaryOp::Mul;
        case TokenType::TOKEN_SLASH:    return BinaryOp::Div;
        case TokenType::TOKEN_PERCENT:  return BinaryOp::Mod;
        case TokenType::TOKEN_EQ:       return BinaryOp::Eq;
        case TokenType::TOKEN_NOT_EQ:   return BinaryOp::NotEq;
        case TokenType::TOKEN_LT:       return BinaryOp::Lt;
        case TokenType::TOKEN_GT:       return BinaryOp::Gt;
        case TokenType::TOKEN_LE:       return BinaryOp::Le;
        case TokenType::TOKEN_GE:       return BinaryOp::Ge;
        case TokenType::TOKEN_AND:      return BinaryOp::And;
        case TokenType::TOKEN_OR:       return BinaryOp::Or;
        case TokenType::TOKEN_BIT_AND:  return BinaryOp::BitAnd;
        case TokenType::TOKEN_BIT_OR:   return BinaryOp::BitOr;
        case TokenType::TOKEN_CARET:    return BinaryOp::BitXor;
        case TokenType::TOKEN_LSHIFT:   return BinaryOp::LShift;
        case TokenType::TOKEN_RSHIFT:   return BinaryOp::RShift;
        default:                        return BinaryOp::Invalid;
    }
}

AssignOp Parser::tokenTypeToAssignOp(TokenType type) const {
    switch (type) {
        case TokenType::TOKEN_ASSIGN:       return AssignOp::Assign;
        case TokenType::TOKEN_PLUS_EQ:      return AssignOp::AddAssign;
        case TokenType::TOKEN_MINUS_EQ:     return AssignOp::SubAssign;
        case TokenType::TOKEN_STAR_EQ:      return AssignOp::MulAssign;
        case TokenType::TOKEN_SLASH_EQ:     return AssignOp::DivAssign;
        case TokenType::TOKEN_PERCENT_EQ:   return AssignOp::ModAssign;
        case TokenType::TOKEN_AMP_EQ:       return AssignOp::BitAndAssign;
        case TokenType::TOKEN_PIPE_EQ:      return AssignOp::BitOrAssign;
        case TokenType::TOKEN_CARET_EQ:     return AssignOp::BitXorAssign;
        case TokenType::TOKEN_LSHIFT_EQ:    return AssignOp::LShiftAssign;
        case TokenType::TOKEN_RSHIFT_EQ:    return AssignOp::RShiftAssign;
        default:                            return AssignOp::Assign;
    }
}

// ========== Unary Operators ==========

static bool isUnaryOp(TokenType type) {
    switch (type) {
        case TokenType::TOKEN_MINUS:
        case TokenType::TOKEN_PLUS:
        case TokenType::TOKEN_NOT:
        case TokenType::TOKEN_TILDE:
        case TokenType::TOKEN_STAR:
        case TokenType::TOKEN_BIT_AND:
        case TokenType::TOKEN_PLUS_PLUS:
        case TokenType::TOKEN_MINUS_MINUS:
        case TokenType::TOKEN_SIZEOF:
            return true;
        default:
            return false;
    }
}

static UnaryOp tokenToUnaryOp(TokenType type) {
    switch (type) {
        case TokenType::TOKEN_MINUS:    return UnaryOp::Minus;
        case TokenType::TOKEN_PLUS:     return UnaryOp::Plus;
        case TokenType::TOKEN_NOT:      return UnaryOp::Not;
        case TokenType::TOKEN_TILDE:    return UnaryOp::Not;  // TODO: add BitNot to UnaryOp
        case TokenType::TOKEN_STAR:     return UnaryOp::Deref;
        case TokenType::TOKEN_BIT_AND:  return UnaryOp::AddressOf;
        case TokenType::TOKEN_PLUS_PLUS: return UnaryOp::PreInc;
        case TokenType::TOKEN_MINUS_MINUS: return UnaryOp::PreDec;
        default:                        return UnaryOp::Plus;
    }
}

// ========== Postfix Operators ==========

static bool isPostfixOp(TokenType type) {
    switch (type) {
        case TokenType::TOKEN_PLUS_PLUS:
        case TokenType::TOKEN_MINUS_MINUS:
        case TokenType::TOKEN_LPAREN:
        case TokenType::TOKEN_LBRACKET:
        case TokenType::TOKEN_DOT:
        case TokenType::TOKEN_ARROW:
            return true;
        default:
            return false;
    }
}

// ========== Parsing ==========

std::unique_ptr<ExprAST> Parser::parseUnary() {
    auto token = peek();
    if (!token) {
        return nullptr;
    }

    // Handle parenthesized expressions and cast expressions
    if (token->type == TokenType::TOKEN_LPAREN) {
        advance(); // consume '('

        // Try to detect cast: (type) expr
        // Check if next token is a type keyword
        bool isCast = false;
        Type* castType = nullptr;
        if (auto typeTok = peek()) {
            switch (typeTok->type) {
                case TokenType::TOKEN_INT:
                    castType = TypeContext::instance().getInt();
                    isCast = true;
                    break;
                case TokenType::TOKEN_FLOAT:
                    castType = TypeContext::instance().getFloat();
                    isCast = true;
                    break;
                case TokenType::TOKEN_DOUBLE:
                    castType = TypeContext::instance().getDouble();
                    isCast = true;
                    break;
                case TokenType::TOKEN_CHAR_KW:
                    castType = TypeContext::instance().getChar();
                    isCast = true;
                    break;
                case TokenType::TOKEN_VOID:
                    castType = TypeContext::instance().getVoid();
                    isCast = true;
                    break;
                default:
                    break;
            }
        }

        if (isCast) {
            advance(); // consume type keyword
            if (!match(TokenType::TOKEN_RPAREN)) {
                errorUnexpected("expected ')' after cast type");
                return nullptr;
            }
            auto operand = parseUnary();
            if (!operand) {
                return nullptr;
            }
            return std::make_unique<CastExprAST>(castType, std::move(operand));
        }

        // Not a cast, parse as parenthesized expression
        auto expr = parseExpr();
        if (!expr) {
            return nullptr;
        }
        expect(TokenType::TOKEN_RPAREN, "expected ')' after expression");
        return expr;
    }

    // Handle sizeof
    if (token->type == TokenType::TOKEN_SIZEOF) {
        advance(); // consume sizeof

        // sizeof(type)
        if (match(TokenType::TOKEN_LPAREN)) {
            Type* sizeofType = nullptr;
            if (auto typeTok = peek()) {
                switch (typeTok->type) {
                    case TokenType::TOKEN_INT:
                        sizeofType = TypeContext::instance().getInt();
                        break;
                    case TokenType::TOKEN_FLOAT:
                        sizeofType = TypeContext::instance().getFloat();
                        break;
                    case TokenType::TOKEN_DOUBLE:
                        sizeofType = TypeContext::instance().getDouble();
                        break;
                    case TokenType::TOKEN_CHAR_KW:
                        sizeofType = TypeContext::instance().getChar();
                        break;
                    case TokenType::TOKEN_VOID:
                        sizeofType = TypeContext::instance().getVoid();
                        break;
                    default:
                        break;
                }
            }
            if (sizeofType) {
                advance(); // consume type keyword
            }
            expect(TokenType::TOKEN_RPAREN, "expected ')' after sizeof operand");
            return std::make_unique<SizeofExprAST>(sizeofType);
        }

        // sizeof expr
        auto operand = parseUnary();
        if (!operand) {
            return nullptr;
        }
        return std::make_unique<SizeofExprAST>(nullptr, std::move(operand));
    }

    // Handle prefix unary operators
    if (isUnaryOp(token->type)) {
        auto op = token->type;
        advance(); // consume operator
        auto operand = parseUnary();
        if (!operand) {
            return nullptr;
        }
        auto unaryOp = tokenToUnaryOp(op);
        return std::make_unique<UnaryExprAST>(unaryOp, std::move(operand));
    }

    return parsePrimary();
}

std::unique_ptr<ExprAST> Parser::parsePrimary() {
    auto token = peek();
    if (!token) {
        return nullptr;
    }

    // Number literal
    if (token->type == TokenType::TOKEN_NUMBER) {
        advance();
        int val = std::get<int>(token->value);
        return std::make_unique<NumberExprAST>(val);
    }

    // Float literal
    if (token->type == TokenType::TOKEN_FLOAT) {
        advance();
        double val = std::get<double>(token->value);
        return std::make_unique<FloatExprAST>(val);
    }

    // Char literal
    if (token->type == TokenType::TOKEN_CHAR) {
        advance();
        // Char literal value parsing - take the character between quotes
        char val = token->lexeme.size() > 2 ? token->lexeme[1] : 0;
        return std::make_unique<CharExprAST>(val);
    }

    // String literal
    if (token->type == TokenType::TOKEN_STRING) {
        advance();
        // Strip surrounding quotes
        std::string val = token->lexeme.substr(1, token->lexeme.size() - 2);
        return std::make_unique<StringExprAST>(val);
    }

    // Identifier or function call
    if (token->type == TokenType::TOKEN_IDENTIFIER) {
        std::string name = token->lexeme;
        advance();

        // Function call: identifier(args)
        if (peek() && peek()->type == TokenType::TOKEN_LPAREN) {
            advance(); // consume '('
            std::vector<std::unique_ptr<ExprAST>> args;

            if (!peek() || peek()->type != TokenType::TOKEN_RPAREN) {
                while (true) {
                    auto arg = parseExpr();
                    if (!arg) {
                        return nullptr;
                    }
                    args.push_back(std::move(arg));

                    if (!match(TokenType::TOKEN_COMMA)) {
                        break;
                    }
                }
            }

            expect(TokenType::TOKEN_RPAREN, "expected ')' after function arguments");
            return std::make_unique<CallExprAST>(name, std::move(args));
        }

        return std::make_unique<VariableExprAST>(name);
    }

    // Parenthesized expression
    if (token->type == TokenType::TOKEN_LPAREN) {
        advance();
        auto expr = parseExpr();
        expect(TokenType::TOKEN_RPAREN, "expected ')' after expression");
        return expr;
    }

    // Initializer list: {expr, expr, ...}
    if (token->type == TokenType::TOKEN_LBRACE) {
        advance();
        std::vector<std::unique_ptr<ExprAST>> initList;

        if (!check(TokenType::TOKEN_RBRACE)) {
            while (true) {
                auto expr = parseExpr();
                if (expr) {
                    initList.push_back(std::move(expr));
                }
                if (!match(TokenType::TOKEN_COMMA)) break;
            }
        }

        expect(TokenType::TOKEN_RBRACE, "expected '}' to end initializer list");
        return std::make_unique<InitializerListExprAST>(std::move(initList));
    }

    errorUnexpected("expected expression");
    return nullptr;
}

std::unique_ptr<ExprAST> Parser::parsePostfix(std::unique_ptr<ExprAST> lhs) {
    if (!lhs) {
        return nullptr;
    }

    while (auto token = peek()) {
        switch (token->type) {
            case TokenType::TOKEN_PLUS_PLUS:
            case TokenType::TOKEN_MINUS_MINUS: {
                bool isInc = (token->type == TokenType::TOKEN_PLUS_PLUS);
                advance();
                lhs = std::make_unique<PostfixIncDecExprAST>(std::move(lhs), isInc);
                break;
            }
            case TokenType::TOKEN_LPAREN: {
                // Function call on expression result - not supported in standard C
                // but we handle it for completeness
                advance(); // consume '('
                std::vector<std::unique_ptr<ExprAST>> args;
                if (!peek() || peek()->type != TokenType::TOKEN_RPAREN) {
                    while (true) {
                        auto arg = parseExpr();
                        if (!arg) return nullptr;
                        args.push_back(std::move(arg));
                        if (!match(TokenType::TOKEN_COMMA)) break;
                    }
                }
                expect(TokenType::TOKEN_RPAREN, "expected ')' after function arguments");
                // This would need a different AST node for indirect calls
                // For now, return the lhs as-is (the call args are parsed but unused)
                break;
            }
            case TokenType::TOKEN_LBRACKET: {
                advance(); // consume '['
                auto index = parseExpr();
                if (!index) return nullptr;
                expect(TokenType::TOKEN_RBRACKET, "expected ']' after array index");
                lhs = std::make_unique<ArrayAccessExprAST>(std::move(lhs), std::move(index));
                break;
            }
            case TokenType::TOKEN_DOT: {
                advance(); // consume '.'
                if (auto member = match(TokenType::TOKEN_IDENTIFIER)) {
                    lhs = std::make_unique<MemberAccessExprAST>(
                        MemberAccessKind::Dot, std::move(lhs), member->lexeme);
                } else {
                    errorUnexpected("expected member name after '.'");
                }
                break;
            }
            case TokenType::TOKEN_ARROW: {
                advance(); // consume '->'
                if (auto member = match(TokenType::TOKEN_IDENTIFIER)) {
                    lhs = std::make_unique<MemberAccessExprAST>(
                        MemberAccessKind::Arrow, std::move(lhs), member->lexeme);
                } else {
                    errorUnexpected("expected member name after '->'");
                }
                break;
            }
            default:
                return lhs;
        }
    }

    return lhs;
}

std::unique_ptr<ExprAST> Parser::parseExpr(int minPrec) {
    auto token = peek();
    if (!token) {
        return nullptr;
    }

    std::unique_ptr<ExprAST> lhs;

    lhs = parseUnary();

    if (!lhs) {
        return nullptr;
    }

    lhs = parsePostfix(std::move(lhs));

    // Pratt climbing loop
    while (!eof()) {
        auto opToken = peek();
        if (!opToken) break;

        int prec = getPrecedence(opToken->type);
        if (prec < minPrec) break;

        // Handle ternary operator specially (right-to-left, needs ? and :)
        if (opToken->type == TokenType::TOKEN_QUESTION) {
            advance(); // consume '?'
            auto thenExpr = parseExpr(); // parse then-branch (full expression)
            if (!thenExpr) return nullptr;
            match(TokenType::TOKEN_COLON);
            auto elseExpr = parseExpr(); // parse else-branch
            if (!elseExpr) return nullptr;
            lhs = std::make_unique<TernaryExprAST>(std::move(lhs), std::move(thenExpr), std::move(elseExpr));
            continue;
        }

        // Handle assignment operators (right-to-left)
        if (opToken->type == TokenType::TOKEN_ASSIGN ||
            opToken->type == TokenType::TOKEN_PLUS_EQ ||
            opToken->type == TokenType::TOKEN_MINUS_EQ ||
            opToken->type == TokenType::TOKEN_STAR_EQ ||
            opToken->type == TokenType::TOKEN_SLASH_EQ ||
            opToken->type == TokenType::TOKEN_PERCENT_EQ ||
            opToken->type == TokenType::TOKEN_AMP_EQ ||
            opToken->type == TokenType::TOKEN_PIPE_EQ ||
            opToken->type == TokenType::TOKEN_CARET_EQ ||
            opToken->type == TokenType::TOKEN_LSHIFT_EQ ||
            opToken->type == TokenType::TOKEN_RSHIFT_EQ) {
            auto assignOp = tokenTypeToAssignOp(opToken->type);
            advance(); // consume assignment operator
            auto rhs = parseExpr(prec); // right-to-left: use same prec
            if (!rhs) return nullptr;
            lhs = std::make_unique<AssignmentExprAST>(assignOp, std::move(lhs), std::move(rhs));
            continue;
        }

        // Binary operators
        if (prec > 0) {
            auto binOp = tokenTypeToBinaryOp(opToken->type);
            if (binOp == BinaryOp::Invalid) {
                break;
            }
            advance(); // consume operator
            int nextPrec = isRightAssociative(opToken->type) ? prec : prec + 1;
            auto rhs = parseExpr(nextPrec);
            if (!rhs) return nullptr;
            lhs = std::make_unique<BinaryExprAST>(binOp, std::move(lhs), std::move(rhs));
            continue;
        }

        break;
    }

    return lhs;
}

// ========== Declaration Parsing ==========

std::unique_ptr<ReturnStmtAST> Parser::parseReturnStmt() {
    auto retToken = match(TokenType::TOKEN_RETURN);
    if (!retToken) {
        return nullptr;
    }

    auto value = parseExpr();
    if (!value) {
        errorUnexpected("expected expression after 'return'");
        return nullptr;
    }

    expect(TokenType::TOKEN_SEMICOLON, "expected ';' after return statement");
    auto stmt = std::make_unique<ReturnStmtAST>(std::move(value));
    stmt->setLocation(retToken->filename, retToken->line, retToken->column);
    return stmt;
}

std::unique_ptr<CompoundStmtAST> Parser::parseCompoundStmt() {
    if (!expect(TokenType::TOKEN_LBRACE, "expected '{' to begin compound statement")) {
        return nullptr;
    }

    std::vector<std::unique_ptr<StmtAST>> stmts{};
    while(!eof() && peek()->type != TokenType::TOKEN_RBRACE) {
        auto stmt = parseStmt();
        if (!stmt) {
            advance();
            continue;
        }
        stmts.push_back(std::move(stmt));
    }
    expect(TokenType::TOKEN_RBRACE, "expected '}' to end compound statement");

    return std::make_unique<CompoundStmtAST>(std::move(stmts));
}

std::unique_ptr<StmtAST> Parser::parseStmt() {
    // if statement
    if (peek() && peek()->type == TokenType::TOKEN_IF) {
        return parseIfStmt();
    }

    // while statement
    if (peek() && peek()->type == TokenType::TOKEN_WHILE) {
        return parseWhileStmt();
    }

    // do-while statement
    if (peek() && peek()->type == TokenType::TOKEN_DO) {
        return parseDoWhileStmt();
    }

    // for statement
    if (peek() && peek()->type == TokenType::TOKEN_FOR) {
        return parseForStmt();
    }

    // break statement
    if (peek() && peek()->type == TokenType::TOKEN_BREAK) {
        return parseBreakStmt();
    }

    // continue statement
    if (peek() && peek()->type == TokenType::TOKEN_CONTINUE) {
        return parseContinueStmt();
    }

    // goto statement
    if (peek() && peek()->type == TokenType::TOKEN_GOTO) {
        return parseGotoStmt();
    }

    // return statement
    if (auto retToken = match(TokenType::TOKEN_RETURN)) {
        auto value = parseExpr();
        if (!value) {
            errorUnexpected("expected expression after 'return'");
            return nullptr;
        }

        expect(TokenType::TOKEN_SEMICOLON, "expected ';' after return statement");
        auto stmt = std::make_unique<ReturnStmtAST>(std::move(value));
        stmt->setLocation(retToken->filename, retToken->line, retToken->column);
        return stmt;
    }

    // compound statement (block)
    if (peek() && peek()->type == TokenType::TOKEN_LBRACE) {
        return parseCompoundStmt();
    }

    // local declaration (type keyword or typedef name)
    if (isTypeStart() || check(TokenType::TOKEN_CONST) || check(TokenType::TOKEN_VOLATILE)) {
        size_t savedPos = m_currentTokenPos;
        Type* type = parseType();
        if (type && check(TokenType::TOKEN_IDENTIFIER)) {
            auto nameTok = advance();
            if (check(TokenType::TOKEN_LPAREN)) {
                // This is a function declaration inside a block - restore and parse as declaration
                m_currentTokenPos = savedPos;
                auto decl = parseDeclaration();
                if (decl) {
                    return std::make_unique<DeclStmtAST>(std::move(decl));
                }
            }
            auto varDecl = parseVariableDecl(type, nameTok->lexeme);
            if (varDecl) {
                return std::make_unique<DeclStmtAST>(std::move(varDecl));
            }
        }
        // Not a declaration, restore position
        m_currentTokenPos = savedPos;
    }

    // label statement: identifier ':'
    if (peek() && peek()->type == TokenType::TOKEN_IDENTIFIER) {
        // Look ahead to see if this is a label
        size_t savedPos = m_currentTokenPos;
        auto id = advance();
        if (peek() && peek()->type == TokenType::TOKEN_COLON) {
            advance(); // consume ':'
            auto stmt = parseStmt();
            return std::make_unique<LabelStmtAST>(id->lexeme, std::move(stmt));
        }
        // Not a label, restore position
        m_currentTokenPos = savedPos;
    }

    // expression statement
    return parseExprStmt();
}

std::unique_ptr<StmtAST> Parser::parseIfStmt() {
    auto ifToken = match(TokenType::TOKEN_IF);
    if (!ifToken) {
        return nullptr;
    }

    expect(TokenType::TOKEN_LPAREN, "expected '(' after 'if'");
    auto cond = parseExpr();
    if (!cond) {
        errorUnexpected("expected condition expression");
    }
    expect(TokenType::TOKEN_RPAREN, "expected ')' after condition");

    auto thenStmt = parseStmt();
    if (!thenStmt) {
        errorUnexpected("expected statement after 'if' condition");
    }

    std::unique_ptr<StmtAST> elseStmt;
    if (match(TokenType::TOKEN_ELSE)) {
        elseStmt = parseStmt();
        if (!elseStmt) {
            errorUnexpected("expected statement after 'else'");
        }
    }

    auto stmt = std::make_unique<IfStmtAST>(std::move(cond), std::move(thenStmt), std::move(elseStmt));
    stmt->setLocation(ifToken->filename, ifToken->line, ifToken->column);
    return stmt;
}

std::unique_ptr<StmtAST> Parser::parseWhileStmt() {
    auto whileToken = match(TokenType::TOKEN_WHILE);
    if (!whileToken) {
        return nullptr;
    }

    expect(TokenType::TOKEN_LPAREN, "expected '(' after 'while'");
    auto cond = parseExpr();
    if (!cond) {
        errorUnexpected("expected condition expression");
    }
    expect(TokenType::TOKEN_RPAREN, "expected ')' after condition");

    auto body = parseStmt();
    if (!body) {
        errorUnexpected("expected statement after 'while' condition");
    }

    auto stmt = std::make_unique<WhileStmtAST>(std::move(cond), std::move(body));
    stmt->setLocation(whileToken->filename, whileToken->line, whileToken->column);
    return stmt;
}

std::unique_ptr<StmtAST> Parser::parseDoWhileStmt() {
    auto doToken = match(TokenType::TOKEN_DO);
    if (!doToken) {
        return nullptr;
    }

    auto body = parseStmt();
    if (!body) {
        errorUnexpected("expected statement after 'do'");
    }

    expect(TokenType::TOKEN_WHILE, "expected 'while' after 'do' body");
    expect(TokenType::TOKEN_LPAREN, "expected '(' after 'while'");
    auto cond = parseExpr();
    if (!cond) {
        errorUnexpected("expected condition expression");
    }
    expect(TokenType::TOKEN_RPAREN, "expected ')' after condition");
    expect(TokenType::TOKEN_SEMICOLON, "expected ';' after do-while statement");

    auto stmt = std::make_unique<DoWhileStmtAST>(std::move(cond), std::move(body));
    stmt->setLocation(doToken->filename, doToken->line, doToken->column);
    return stmt;
}

std::unique_ptr<StmtAST> Parser::parseForStmt() {
    auto forToken = match(TokenType::TOKEN_FOR);
    if (!forToken) {
        return nullptr;
    }

    expect(TokenType::TOKEN_LPAREN, "expected '(' after 'for'");

    // init
    std::unique_ptr<StmtAST> init;
    if (peek() && peek()->type == TokenType::TOKEN_SEMICOLON) {
        advance(); // empty init
    } else {
        init = parseStmt(); // could be declaration or expression stmt
    }

    // condition
    std::unique_ptr<ExprAST> cond;
    if (peek() && peek()->type != TokenType::TOKEN_SEMICOLON) {
        cond = parseExpr();
    }
    expect(TokenType::TOKEN_SEMICOLON, "expected ';' after for condition");

    // increment
    std::unique_ptr<ExprAST> inc;
    if (peek() && peek()->type != TokenType::TOKEN_RPAREN) {
        inc = parseExpr();
    }
    expect(TokenType::TOKEN_RPAREN, "expected ')' after for increment");

    auto body = parseStmt();
    if (!body) {
        errorUnexpected("expected statement after 'for' header");
    }

    auto stmt = std::make_unique<ForStmtAST>(std::move(init), std::move(cond), std::move(inc), std::move(body));
    stmt->setLocation(forToken->filename, forToken->line, forToken->column);
    return stmt;
}

std::unique_ptr<StmtAST> Parser::parseBreakStmt() {
    auto breakToken = match(TokenType::TOKEN_BREAK);
    if (!breakToken) {
        return nullptr;
    }

    expect(TokenType::TOKEN_SEMICOLON, "expected ';' after 'break'");

    auto stmt = std::make_unique<BreakStmtAST>();
    stmt->setLocation(breakToken->filename, breakToken->line, breakToken->column);
    return stmt;
}

std::unique_ptr<StmtAST> Parser::parseContinueStmt() {
    auto continueToken = match(TokenType::TOKEN_CONTINUE);
    if (!continueToken) {
        return nullptr;
    }

    expect(TokenType::TOKEN_SEMICOLON, "expected ';' after 'continue'");

    auto stmt = std::make_unique<ContinueStmtAST>();
    stmt->setLocation(continueToken->filename, continueToken->line, continueToken->column);
    return stmt;
}

std::unique_ptr<StmtAST> Parser::parseGotoStmt() {
    auto gotoToken = match(TokenType::TOKEN_GOTO);
    if (!gotoToken) {
        return nullptr;
    }

    auto label = match(TokenType::TOKEN_IDENTIFIER);
    if (!label) {
        errorUnexpected("expected label name after 'goto'");
    }
    expect(TokenType::TOKEN_SEMICOLON, "expected ';' after 'goto' statement");

    auto stmt = std::make_unique<GotoStmtAST>(label ? label->lexeme : "");
    stmt->setLocation(gotoToken->filename, gotoToken->line, gotoToken->column);
    return stmt;
}

std::unique_ptr<StmtAST> Parser::parseLabelStmt(const std::string& label) {
    auto stmt = parseStmt();
    return std::make_unique<LabelStmtAST>(label, std::move(stmt));
}

std::unique_ptr<StmtAST> Parser::parseExprStmt() {
    auto expr = parseExpr();
    if (!expr) {
        return nullptr;
    }
    expect(TokenType::TOKEN_SEMICOLON, "expected ';' after expression statement");
    return std::make_unique<ExprStmtAST>(std::move(expr));
}

std::unique_ptr<TranslationUnitAST> Parser::parse() {
    std::vector<std::unique_ptr<DeclAST>> decls{};
    while (!eof()) {
        auto decl = parseDeclaration();
        if (decl) {
            decls.push_back(std::move(decl));
        } else {
            advance();
        }
    }
    return std::make_unique<TranslationUnitAST>(std::move(decls));
}

bool Parser::check(TokenType type) const {
    if (eof()) return false;
    return m_tokens[m_currentTokenPos].type == type;
}

bool Parser::isTypeStart() const {
    if (eof()) return false;
    switch (peek()->type) {
        case TokenType::TOKEN_INT:
        case TokenType::TOKEN_FLOAT:
        case TokenType::TOKEN_DOUBLE:
        case TokenType::TOKEN_CHAR_KW:
        case TokenType::TOKEN_VOID:
        case TokenType::TOKEN_STRUCT:
        case TokenType::TOKEN_UNION:
        case TokenType::TOKEN_ENUM:
        case TokenType::TOKEN_TYPEDEF:
            return true;
        case TokenType::TOKEN_IDENTIFIER: {
            // Check if identifier is a typedef name
            Type* t = TypeContext::instance().getTypedef(peek()->lexeme);
            return t != nullptr;
        }
        default:
            return false;
    }
}

Type* Parser::parseBaseType() {
    auto tok = peek();
    if (!tok) return nullptr;

    switch (tok->type) {
        case TokenType::TOKEN_INT: {
            advance();
            return TypeContext::instance().getInt();
        }
        case TokenType::TOKEN_FLOAT: {
            advance();
            return TypeContext::instance().getFloat();
        }
        case TokenType::TOKEN_DOUBLE: {
            advance();
            return TypeContext::instance().getDouble();
        }
        case TokenType::TOKEN_CHAR_KW: {
            advance();
            return TypeContext::instance().getChar();
        }
        case TokenType::TOKEN_VOID: {
            advance();
            return TypeContext::instance().getVoid();
        }
        case TokenType::TOKEN_STRUCT: {
            advance(); // consume 'struct'
            std::string name;
            if (check(TokenType::TOKEN_IDENTIFIER)) {
                name = advance()->lexeme;
            }

            // Check if this is a definition or just a reference
            if (check(TokenType::TOKEN_LBRACE)) {
                advance(); // consume '{'
                auto* structType = new StructType(name);
                while (!eof() && !check(TokenType::TOKEN_RBRACE)) {
                    Type* fieldType = parseType();
                    if (!fieldType) break;
                    if (!check(TokenType::TOKEN_IDENTIFIER)) break;
                    std::string fieldName = advance()->lexeme;
                    structType->addField(fieldName, fieldType);
                    match(TokenType::TOKEN_SEMICOLON);
                }
                match(TokenType::TOKEN_RBRACE);
                match(TokenType::TOKEN_SEMICOLON);
                TypeContext::instance().addStruct(name, structType);
                return structType;
            }

            // Just a reference to existing struct type
            auto existing = TypeContext::instance().getStruct(name);
            if (existing) return existing;

            // Forward reference - create placeholder
            auto* structType = new StructType(name);
            TypeContext::instance().addStruct(name, structType);
            return structType;
        }
        case TokenType::TOKEN_UNION: {
            advance(); // consume 'union'
            std::string name;
            if (check(TokenType::TOKEN_IDENTIFIER)) {
                name = advance()->lexeme;
            }

            if (check(TokenType::TOKEN_LBRACE)) {
                advance(); // consume '{'
                auto* unionType = new UnionType(name);
                while (!eof() && !check(TokenType::TOKEN_RBRACE)) {
                    Type* memberType = parseType();
                    if (!memberType) break;
                    if (!check(TokenType::TOKEN_IDENTIFIER)) break;
                    std::string memberName = advance()->lexeme;
                    unionType->addMember(memberName, memberType);
                    match(TokenType::TOKEN_SEMICOLON);
                }
                match(TokenType::TOKEN_RBRACE);
                match(TokenType::TOKEN_SEMICOLON);
                TypeContext::instance().addUnion(name, unionType);
                return unionType;
            }

            auto existing = TypeContext::instance().getUnion(name);
            if (existing) return existing;

            auto* unionType = new UnionType(name);
            TypeContext::instance().addUnion(name, unionType);
            return unionType;
        }
        case TokenType::TOKEN_ENUM: {
            advance(); // consume 'enum'
            std::string name;
            if (check(TokenType::TOKEN_IDENTIFIER)) {
                name = advance()->lexeme;
            }

            if (check(TokenType::TOKEN_LBRACE)) {
                advance(); // consume '{'
                auto* enumType = new EnumType(name);
                int currentVal = 0;
                while (!eof() && !check(TokenType::TOKEN_RBRACE)) {
                    if (!check(TokenType::TOKEN_IDENTIFIER)) break;
                    std::string valueName = advance()->lexeme;
                    int val = currentVal;
                    if (check(TokenType::TOKEN_ASSIGN)) {
                        advance();
                        auto expr = parseExpr();
                        if (auto num = dynamic_cast<NumberExprAST*>(expr.get())) {
                            val = num->value;
                        }
                    }
                    enumType->addValue(valueName, val);
                    currentVal = val + 1;
                    if (!check(TokenType::TOKEN_COMMA)) break;
                    advance(); // consume ','
                }
                match(TokenType::TOKEN_RBRACE);
                TypeContext::instance().addEnum(name, enumType);
                return enumType;
            }

            auto existing = TypeContext::instance().getEnum(name);
            if (existing) return existing;

            auto* enumType = new EnumType(name);
            TypeContext::instance().addEnum(name, enumType);
            return enumType;
        }
        case TokenType::TOKEN_IDENTIFIER: {
            // Check if it's a typedef name
            Type* typedefType = TypeContext::instance().getTypedef(tok->lexeme);
            if (typedefType) {
                advance();
                return typedefType;
            }
            return nullptr;
        }
        default:
            return nullptr;
    }
}

Type* Parser::parseType() {
    bool isConst = false;
    bool isVolatile = false;

    // Handle qualifiers before base type
    while (!eof()) {
        if (check(TokenType::TOKEN_CONST)) {
            advance();
            isConst = true;
        } else if (check(TokenType::TOKEN_VOLATILE)) {
            advance();
            isVolatile = true;
        } else {
            break;
        }
    }

    Type* baseType = parseBaseType();
    if (!baseType) return nullptr;

    baseType->isConst = isConst;
    baseType->isVolatile = isVolatile;

    // Check for pointer types with qualifiers
    while (check(TokenType::TOKEN_STAR)) {
        advance();
        auto* ptrType = new Type(TypeKind::Pointer, baseType);

        // Check for qualifiers after pointer star (but before next star or identifier)
        while (!eof()) {
            if (check(TokenType::TOKEN_CONST)) {
                advance();
                ptrType->isConst = true;
            } else if (check(TokenType::TOKEN_VOLATILE)) {
                advance();
                ptrType->isVolatile = true;
            } else {
                break;
            }
        }

        baseType = ptrType;
    }

    return baseType;
}

std::unique_ptr<DeclAST> Parser::parseDeclaration() {
    if (check(TokenType::TOKEN_TYPEDEF)) {
        return parseTypedefDecl();
    }

    if (check(TokenType::TOKEN_STRUCT)) {
        auto structDecl = parseStructDecl();
        if (!structDecl) return nullptr;

        // Register struct type in TypeContext if it has fields
        if (!structDecl->fields.empty()) {
            auto structType = new StructType(structDecl->name);
            for (auto& field : structDecl->fields) {
                structType->addField(field.first, field.second);
            }
            TypeContext::instance().addStruct(structDecl->name, structType);
        }

        // Check if there's a variable name after struct declaration
        if (check(TokenType::TOKEN_IDENTIFIER)) {
            auto nameTok = advance();
            auto type = TypeContext::instance().getStruct(structDecl->name);
            if (!type) {
                // Create a placeholder struct type for forward-declared structs
                type = new StructType(structDecl->name);
                TypeContext::instance().addStruct(structDecl->name, static_cast<StructType*>(type));
            }
            return parseVariableDecl(type, nameTok->lexeme);
        }
        return structDecl;
    }

    if (check(TokenType::TOKEN_UNION)) {
        auto unionDecl = parseUnionDecl();
        if (!unionDecl) return nullptr;

        // Register union type in TypeContext if it has members
        if (!unionDecl->members.empty()) {
            auto unionType = new UnionType(unionDecl->name);
            for (auto& member : unionDecl->members) {
                unionType->addMember(member.first, member.second);
            }
            TypeContext::instance().addUnion(unionDecl->name, unionType);
        }

        if (check(TokenType::TOKEN_IDENTIFIER)) {
            auto nameTok = advance();
            auto type = TypeContext::instance().getUnion(unionDecl->name);
            if (!type) {
                type = new UnionType(unionDecl->name);
                TypeContext::instance().addUnion(unionDecl->name, static_cast<UnionType*>(type));
            }
            return parseVariableDecl(type, nameTok->lexeme);
        }
        return unionDecl;
    }

    if (check(TokenType::TOKEN_ENUM)) {
        auto enumDecl = parseEnumDecl();
        if (!enumDecl) return nullptr;

        // Register enum type in TypeContext if it has values
        if (!enumDecl->values.empty()) {
            auto enumType = new EnumType(enumDecl->name);
            for (auto& val : enumDecl->values) {
                enumType->addValue(val.first, val.second);
            }
            TypeContext::instance().addEnum(enumDecl->name, enumType);
        }

        if (check(TokenType::TOKEN_IDENTIFIER)) {
            auto nameTok = advance();
            auto type = TypeContext::instance().getEnum(enumDecl->name);
            if (!type) {
                type = new EnumType(enumDecl->name);
                TypeContext::instance().addEnum(enumDecl->name, static_cast<EnumType*>(type));
            }
            return parseVariableDecl(type, nameTok->lexeme);
        }
        return enumDecl;
    }

    // Handle constexpr declaration specifier
    bool isConstexpr = false;
    if (check(TokenType::TOKEN_CONSTEXPR)) {
        advance();
        isConstexpr = true;
    }

    Type* type = parseType();
    if (!type) return nullptr;

    if (!check(TokenType::TOKEN_IDENTIFIER)) return nullptr;
    auto nameTok = advance();

    // Function declaration/definition: type name '('
    if (check(TokenType::TOKEN_LPAREN)) {
        return parseFunctionDecl(type, nameTok->lexeme);
    }

    // Variable declaration
    return parseVariableDecl(type, nameTok->lexeme, isConstexpr);
}

std::unique_ptr<FunctionDeclAST> Parser::parseFunctionDecl(Type* returnType, const std::string& name) {
    if (!expect(TokenType::TOKEN_LPAREN, "expected '(' after function name")) return nullptr;

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    bool isVarArg = false;

    if (!check(TokenType::TOKEN_RPAREN)) {
        while (true) {
            if (check(TokenType::TOKEN_VOID)) {
                advance();
                break;
            }

            auto param = parseParamDecl();
            if (!param) return nullptr;
            params.push_back(std::move(param));

            if (!match(TokenType::TOKEN_COMMA)) break;

            if (check(TokenType::TOKEN_ELLIPSIS)) {
                isVarArg = true;
                advance();
                break;
            }
        }
    }

    if (!expect(TokenType::TOKEN_RPAREN, "expected ')' after parameter list")) return nullptr;

    // Check for function body
    std::unique_ptr<CompoundStmtAST> body;
    if (check(TokenType::TOKEN_LBRACE)) {
        body = parseCompoundStmt();
    } else if (!check(TokenType::TOKEN_SEMICOLON) && !eof()) {
        errorUnexpected("expected '{' or ';' after function declaration");
    }

    return std::make_unique<FunctionDeclAST>(name, returnType, params, body);
}

std::unique_ptr<DeclAST> Parser::parseVariableDecl(Type* type, const std::string& name, bool isConstexpr) {
    // constexpr implies const
    if (isConstexpr) {
        type->isConst = true;
    }

    std::unique_ptr<ExprAST> init;

    // Check for array declaration: name[size]
    if (check(TokenType::TOKEN_LBRACKET)) {
        advance();
        int size = 0;
        if (auto numTok = match(TokenType::TOKEN_NUMBER)) {
            size = std::get<int>(numTok->value);
        }
        if (!expect(TokenType::TOKEN_RBRACKET, "expected ']' after array size")) {
            return nullptr;
        }

        // Check for initializer
        if (check(TokenType::TOKEN_ASSIGN)) {
            advance();
            init = parseExpr();
        }

        expect(TokenType::TOKEN_SEMICOLON, "expected ';' after array declaration");
        return std::make_unique<ArrayDeclAST>(name, type, size, std::move(init));
    }

    // Check for initializer: = expr
    if (check(TokenType::TOKEN_ASSIGN)) {
        advance();
        init = parseExpr();
    }

    expect(TokenType::TOKEN_SEMICOLON, "expected ';' after variable declaration");
    return std::make_unique<VarDeclAST>(name, type, std::move(init), isConstexpr);
}

std::unique_ptr<ParamDeclAST> Parser::parseParamDecl() {
    Type* type = parseType();
    if (!type) return nullptr;

    std::string name;
    if (check(TokenType::TOKEN_IDENTIFIER)) {
        name = advance()->lexeme;
    }

    return std::make_unique<ParamDeclAST>(name, type);
}

std::unique_ptr<StructDeclAST> Parser::parseStructDecl() {
    if (!match(TokenType::TOKEN_STRUCT)) return nullptr;

    std::string name;
    if (check(TokenType::TOKEN_IDENTIFIER)) {
        name = advance()->lexeme;
    }

    if (!check(TokenType::TOKEN_LBRACE)) {
        // Forward declaration
        return std::make_unique<StructDeclAST>(name, std::vector<std::pair<std::string, Type*>>{});
    }

    advance(); // consume '{'

    std::vector<std::pair<std::string, Type*>> fields;
    while (!eof() && !check(TokenType::TOKEN_RBRACE)) {
        Type* fieldType = parseType();
        if (!fieldType) break;

        if (!check(TokenType::TOKEN_IDENTIFIER)) break;
        std::string fieldName = advance()->lexeme;

        fields.push_back({fieldName, fieldType});

        match(TokenType::TOKEN_SEMICOLON);
    }

    match(TokenType::TOKEN_RBRACE);
    match(TokenType::TOKEN_SEMICOLON);

    return std::make_unique<StructDeclAST>(name, std::move(fields));
}

std::unique_ptr<UnionDeclAST> Parser::parseUnionDecl() {
    if (!match(TokenType::TOKEN_UNION)) return nullptr;

    std::string name;
    if (check(TokenType::TOKEN_IDENTIFIER)) {
        name = advance()->lexeme;
    }

    if (!check(TokenType::TOKEN_LBRACE)) {
        // Forward declaration
        return std::make_unique<UnionDeclAST>(name, std::vector<std::pair<std::string, Type*>>{});
    }

    advance(); // consume '{'

    std::vector<std::pair<std::string, Type*>> members;
    while (!eof() && !check(TokenType::TOKEN_RBRACE)) {
        Type* memberType = parseType();
        if (!memberType) break;

        if (!check(TokenType::TOKEN_IDENTIFIER)) break;
        std::string memberName = advance()->lexeme;

        members.push_back({memberName, memberType});

        match(TokenType::TOKEN_SEMICOLON);
    }

    match(TokenType::TOKEN_RBRACE);
    match(TokenType::TOKEN_SEMICOLON);

    return std::make_unique<UnionDeclAST>(name, std::move(members));
}

std::unique_ptr<EnumDeclAST> Parser::parseEnumDecl() {
    if (!match(TokenType::TOKEN_ENUM)) return nullptr;

    std::string name;
    if (check(TokenType::TOKEN_IDENTIFIER)) {
        name = advance()->lexeme;
    }

    if (!check(TokenType::TOKEN_LBRACE)) {
        // Forward declaration
        return std::make_unique<EnumDeclAST>(name, std::vector<std::pair<std::string, int>>{});
    }

    advance(); // consume '{'

    std::vector<std::pair<std::string, int>> values;
    int currentVal = 0;

    while (!eof() && !check(TokenType::TOKEN_RBRACE)) {
        if (!check(TokenType::TOKEN_IDENTIFIER)) break;
        std::string valueName = advance()->lexeme;

        int val = currentVal;
        if (check(TokenType::TOKEN_ASSIGN)) {
            advance();
            auto expr = parseExpr();
            if (auto num = dynamic_cast<NumberExprAST*>(expr.get())) {
                val = num->value;
            }
        }

        values.push_back({valueName, val});
        currentVal = val + 1;

        if (!check(TokenType::TOKEN_COMMA)) break;
        advance(); // consume ','
    }

    match(TokenType::TOKEN_RBRACE);

    return std::make_unique<EnumDeclAST>(name, std::move(values));
}

std::unique_ptr<TypedefDeclAST> Parser::parseTypedefDecl() {
    if (!match(TokenType::TOKEN_TYPEDEF)) return nullptr;

    Type* type = parseType();
    if (!type) return nullptr;

    if (!check(TokenType::TOKEN_IDENTIFIER)) return nullptr;
    std::string name = advance()->lexeme;

    match(TokenType::TOKEN_SEMICOLON);

    TypeContext::instance().addTypedef(name, type);
    return std::make_unique<TypedefDeclAST>(name, type);
}
