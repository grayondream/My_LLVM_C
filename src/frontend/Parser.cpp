#include "Parser.h"
#include <memory>
#include "ast/Decl.h"
#include "frontend/Token.h"

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
        match(TokenType::TOKEN_RPAREN);
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
            match(TokenType::TOKEN_RPAREN);
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

            match(TokenType::TOKEN_RPAREN);
            return std::make_unique<CallExprAST>(name, std::move(args));
        }

        return std::make_unique<VariableExprAST>(name);
    }

    // Parenthesized expression
    if (token->type == TokenType::TOKEN_LPAREN) {
        advance();
        auto expr = parseExpr();
        match(TokenType::TOKEN_RPAREN);
        return expr;
    }

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
                match(TokenType::TOKEN_RPAREN);
                // This would need a different AST node for indirect calls
                // For now, return the lhs as-is (the call args are parsed but unused)
                break;
            }
            case TokenType::TOKEN_LBRACKET: {
                advance(); // consume '['
                auto index = parseExpr();
                if (!index) return nullptr;
                match(TokenType::TOKEN_RBRACKET);
                lhs = std::make_unique<ArrayAccessExprAST>(std::move(lhs), std::move(index));
                break;
            }
            case TokenType::TOKEN_DOT: {
                advance(); // consume '.'
                if (auto member = match(TokenType::TOKEN_IDENTIFIER)) {
                    lhs = std::make_unique<MemberAccessExprAST>(
                        MemberAccessKind::Dot, std::move(lhs), member->lexeme);
                }
                break;
            }
            case TokenType::TOKEN_ARROW: {
                advance(); // consume '->'
                if (auto member = match(TokenType::TOKEN_IDENTIFIER)) {
                    lhs = std::make_unique<MemberAccessExprAST>(
                        MemberAccessKind::Arrow, std::move(lhs), member->lexeme);
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
    auto func = std::make_unique<FunctionDeclAST>(id.lexeme, TypeContext::instance().getInt(), params, body);
    func->setLocation(id.filename, id.line, id.column);
    return func;
}

std::unique_ptr<ReturnStmtAST> Parser::parseReturnStmt() {
    auto retToken = match(TokenType::TOKEN_RETURN);
    if (!retToken) {
        return nullptr;
    }

    auto value = parseExpr();
    if (!value) {
        return nullptr;
    }

    match(TokenType::TOKEN_SEMICOLON);
    auto stmt = std::make_unique<ReturnStmtAST>(std::move(value));
    stmt->setLocation(retToken->filename, retToken->line, retToken->column);
    return stmt;
}

std::unique_ptr<CompoundStmtAST> Parser::parseCompoundStmt() {
    if (!match(TokenType::TOKEN_LBRACE)) {
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
    match(TokenType::TOKEN_RBRACE);

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
            return nullptr;
        }

        match(TokenType::TOKEN_SEMICOLON);
        auto stmt = std::make_unique<ReturnStmtAST>(std::move(value));
        stmt->setLocation(retToken->filename, retToken->line, retToken->column);
        return stmt;
    }

    // compound statement (block)
    if (peek() && peek()->type == TokenType::TOKEN_LBRACE) {
        return parseCompoundStmt();
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

    match(TokenType::TOKEN_LPAREN);
    auto cond = parseExpr();
    match(TokenType::TOKEN_RPAREN);

    auto thenStmt = parseStmt();

    std::unique_ptr<StmtAST> elseStmt;
    if (match(TokenType::TOKEN_ELSE)) {
        elseStmt = parseStmt();
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

    match(TokenType::TOKEN_LPAREN);
    auto cond = parseExpr();
    match(TokenType::TOKEN_RPAREN);

    auto body = parseStmt();

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

    match(TokenType::TOKEN_WHILE);
    match(TokenType::TOKEN_LPAREN);
    auto cond = parseExpr();
    match(TokenType::TOKEN_RPAREN);
    match(TokenType::TOKEN_SEMICOLON);

    auto stmt = std::make_unique<DoWhileStmtAST>(std::move(cond), std::move(body));
    stmt->setLocation(doToken->filename, doToken->line, doToken->column);
    return stmt;
}

std::unique_ptr<StmtAST> Parser::parseForStmt() {
    auto forToken = match(TokenType::TOKEN_FOR);
    if (!forToken) {
        return nullptr;
    }

    match(TokenType::TOKEN_LPAREN);

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
    match(TokenType::TOKEN_SEMICOLON);

    // increment
    std::unique_ptr<ExprAST> inc;
    if (peek() && peek()->type != TokenType::TOKEN_RPAREN) {
        inc = parseExpr();
    }
    match(TokenType::TOKEN_RPAREN);

    auto body = parseStmt();

    auto stmt = std::make_unique<ForStmtAST>(std::move(init), std::move(cond), std::move(inc), std::move(body));
    stmt->setLocation(forToken->filename, forToken->line, forToken->column);
    return stmt;
}

std::unique_ptr<StmtAST> Parser::parseBreakStmt() {
    auto breakToken = match(TokenType::TOKEN_BREAK);
    if (!breakToken) {
        return nullptr;
    }

    match(TokenType::TOKEN_SEMICOLON);

    auto stmt = std::make_unique<BreakStmtAST>();
    stmt->setLocation(breakToken->filename, breakToken->line, breakToken->column);
    return stmt;
}

std::unique_ptr<StmtAST> Parser::parseContinueStmt() {
    auto continueToken = match(TokenType::TOKEN_CONTINUE);
    if (!continueToken) {
        return nullptr;
    }

    match(TokenType::TOKEN_SEMICOLON);

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
    match(TokenType::TOKEN_SEMICOLON);

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
    match(TokenType::TOKEN_SEMICOLON);
    return std::make_unique<ExprStmtAST>(std::move(expr));
}

std::unique_ptr<TranslationUnitAST> Parser::parse() {
    std::vector<std::unique_ptr<DeclAST>> decls{};
    decls.push_back(parseFunctionDecl());
    return std::make_unique<TranslationUnitAST>(std::move(decls));
}
