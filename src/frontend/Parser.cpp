#include "Parser.h"
#include <memory>
#include "ast/Decl.h"
#include "ast/Mangle.h"
#include "frontend/OperatorPrecedence.h"
#include "frontend/Token.h"

namespace {

// Mangle a (possibly qualified) type name to a symbol-table key:
// `A::B::T` / `A.B.T` -> `A_B_T`.
std::string mangleQualifiedTypeName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (size_t i = 0; i < name.size(); ++i) {
        if (name[i] == ':' && i + 1 < name.size() && name[i + 1] == ':') {
            out.push_back('_');
            ++i;
        } else if (name[i] == '.') {
            out.push_back('_');
        } else {
            out.push_back(name[i]);
        }
    }
    return out;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void appendUtf8(std::string& out, unsigned int cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Decode C escape sequences from a string literal body (quotes already removed).
std::string decodeEscapes(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c != '\\' || i + 1 >= s.size()) {
            out.push_back(c);
            continue;
        }
        char e = s[++i];
        switch (e) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': out.push_back('\r'); break;
            case '0': out.push_back('\0'); break;
            case 'a': out.push_back('\a'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'v': out.push_back('\v'); break;
            case '\\': out.push_back('\\'); break;
            case '\'': out.push_back('\''); break;
            case '"': out.push_back('"'); break;
            case '?': out.push_back('?'); break;
            case 'x': {
                int v = 0, n = 0;
                while (i + 1 < s.size() && n < 2 && hexDigit(s[i + 1]) >= 0) {
                    v = v * 16 + hexDigit(s[++i]);
                    ++n;
                }
                out.push_back(static_cast<char>(v));
                break;
            }
            case 'u': {
                unsigned int cp = 0;
                if (i + 1 < s.size() && s[i + 1] == '{') {
                    ++i; // consume '{'
                    while (i + 1 < s.size() && s[i + 1] != '}') {
                        int d = hexDigit(s[++i]);
                        if (d >= 0) cp = cp * 16 + static_cast<unsigned int>(d);
                    }
                    if (i + 1 < s.size() && s[i + 1] == '}') ++i; // consume '}'
                }
                appendUtf8(out, cp);
                break;
            }
            default:
                out.push_back(e);
                break;
        }
    }
    return out;
}

} // namespace

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
        case TokenType::TOKEN_OPERATOR:      return "'operator'";
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

// Precedence/associativity live in the normative table (smc::getOperatorInfo),
// which mirrors docs/spec/grammar.ebnf §9 and is pinned by unit tests.
int Parser::getPrecedence(TokenType op) const {
    return smc::getOperatorInfo(op).precedence;
}

bool Parser::isRightAssociative(TokenType op) const {
    return smc::getOperatorInfo(op).rightAssociative;
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
        case TokenType::TOKEN_TILDE:    return UnaryOp::BitNot;
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

void Parser::applyLocation(ASTNode* node, const Token* tok) {
    if (node && tok) {
        node->setLocation(tok->filename, tok->line, tok->column);
    }
}

std::unique_ptr<ExprAST> Parser::parseUnary() {
    auto startTok = peek();
    auto expr = parseUnaryImpl();
    if (expr) {
        applyLocation(expr.get(), startTok ? &*startTok : nullptr);
    }
    return expr;
}

std::unique_ptr<ExprAST> Parser::parseUnaryImpl() {
    auto token = peek();
    if (!token) {
        return nullptr;
    }

    // Handle parenthesized expressions and cast expressions
    if (token->type == TokenType::TOKEN_LPAREN) {
        advance(); // consume '('

        // Try to detect a cast: (type) expr. Speculatively parse a full type and
        // require the closing ')'; otherwise reparse as a parenthesized
        // expression. Using parseType (rather than a fixed keyword list) allows
        // pointers, qualifiers, and named types: (int*)p, (struct S*)p, (void*)
        // handles, etc. (MEM-10 / P0-02).
        if (isTypeStart() || check(TokenType::TOKEN_CONST) ||
            check(TokenType::TOKEN_VOLATILE)) {
            size_t saved = m_currentTokenPos;
            Type* castType = parseType();
            if (castType && match(TokenType::TOKEN_RPAREN)) {
                auto operand = parseUnary();
                if (!operand) {
                    return nullptr;
                }
                return std::make_unique<CastExprAST>(castType, std::move(operand));
            }
            m_currentTokenPos = saved; // not a cast; reparse as an expression
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

        if (match(TokenType::TOKEN_LPAREN)) {
            // Try `sizeof(type)` first, then fall back to `sizeof(expr)`.
            if (isTypeStart() || check(TokenType::TOKEN_CONST) ||
                check(TokenType::TOKEN_VOLATILE)) {
                size_t savedPos = m_currentTokenPos;
                Type* sizeofType = parseType();
                if (sizeofType && check(TokenType::TOKEN_RPAREN)) {
                    advance(); // consume ')'
                    return std::make_unique<SizeofExprAST>(sizeofType);
                }
                // Not actually a type (e.g. a variable sharing a type name);
                // rewind and parse it as an expression instead.
                m_currentTokenPos = savedPos;
            }

            auto operand = parseExpr();
            if (!operand) {
                return nullptr;
            }
            expect(TokenType::TOKEN_RPAREN, "expected ')' after sizeof operand");
            return std::make_unique<SizeofExprAST>(nullptr, std::move(operand));
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
    auto startTok = peek();
    auto expr = parsePrimaryImpl();
    if (expr) {
        applyLocation(expr.get(), startTok ? &*startTok : nullptr);
    }
    return expr;
}

std::unique_ptr<ExprAST> Parser::parsePrimaryImpl() {
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
        std::string lex = token->lexeme;
        char val = 0;
        if (lex.size() >= 2 && lex.front() == '\'' && lex.back() == '\'') {
            std::string decoded = decodeEscapes(lex.substr(1, lex.size() - 2));
            if (!decoded.empty()) val = decoded[0];
        }
        return std::make_unique<CharExprAST>(val);
    }

    // String literal
    if (token->type == TokenType::TOKEN_STRING) {
        advance();
        std::string lex = token->lexeme;
        std::string val;
        if (lex.size() >= 2 && lex[0] == 'r' && lex[1] == '"') {
            // Raw string: r"..." — no escape processing.
            if (lex.size() >= 3) val = lex.substr(2, lex.size() - 3);
        } else if (lex.size() >= 2 && lex.front() == '"' && lex.back() == '"') {
            val = decodeEscapes(lex.substr(1, lex.size() - 2));
        }
        return std::make_unique<StringExprAST>(val);
    }

    // Boolean literals
    if (token->type == TokenType::TOKEN_TRUE) {
        advance();
        return std::make_unique<NumberExprAST>(1);
    }
    if (token->type == TokenType::TOKEN_FALSE) {
        advance();
        return std::make_unique<NumberExprAST>(0);
    }
    // null literal
    if (token->type == TokenType::TOKEN_NULL) {
        advance();
        return std::make_unique<NumberExprAST>(0);
    }

    // Identifier or function call
    if (token->type == TokenType::TOKEN_IDENTIFIER) {
        std::string name = token->lexeme;
        advance();

        // Qualified name: A::B::name (namespace member access).
        while (check(TokenType::TOKEN_COLON_COLON)) {
            advance(); // consume '::'
            auto part = match(TokenType::TOKEN_IDENTIFIER);
            if (!part) {
                errorUnexpected("expected identifier after '::'");
                break;
            }
            name += "::" + part->lexeme;
        }

        // Function call: identifier(args)
        if (peek() && peek()->type == TokenType::TOKEN_LPAREN) {
            advance(); // consume '('
            std::vector<std::unique_ptr<ExprAST>> args;

            if (!peek() || peek()->type != TokenType::TOKEN_RPAREN) {
                while (true) {
                    auto arg = parseExpr(2); // minPrec=2 to avoid consuming comma operator
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
                auto expr = parseExpr(2); // minPrec=2: ',' separates elements
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
                advance(); // consume '('
                std::vector<std::unique_ptr<ExprAST>> args;
                if (!peek() || peek()->type != TokenType::TOKEN_RPAREN) {
                    while (true) {
                        auto arg = parseExpr(2); // minPrec=2: ',' separates args
                        if (!arg) return nullptr;
                        args.push_back(std::move(arg));
                        if (!match(TokenType::TOKEN_COMMA)) break;
                    }
                }
                expect(TokenType::TOKEN_RPAREN, "expected ')' after function arguments");
                // For member access expressions, create a method call
                if (auto* memberAccess = dynamic_cast<MemberAccessExprAST*>(lhs.get())) {
                    auto object = std::move(memberAccess->object);
                    lhs = std::make_unique<MethodCallExprAST>(
                        std::move(object), memberAccess->memberName, std::move(args));
                }
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
    // The first token of this expression; every node built at this precedence
    // level is stamped with it so diagnostics can point at the expression
    // (INF-03 / P0-06). Sub-expressions are stamped by their own recursive call.
    const Token* startTok = &*token;

    std::unique_ptr<ExprAST> lhs;

    lhs = parseUnary();

    if (!lhs) {
        return nullptr;
    }
    applyLocation(lhs.get(), startTok);

    lhs = parsePostfix(std::move(lhs));
    applyLocation(lhs.get(), startTok);

    // Pratt climbing loop
    while (!eof()) {
        auto opToken = peek();
        if (!opToken) break;

        int prec = getPrecedence(opToken->type);
        if (prec < minPrec) break;

        // Comma operator: lowest precedence, left-associative, represented by
        // its own AST node (CommaExprAST).
        if (opToken->type == TokenType::TOKEN_COMMA) {
            advance(); // consume ','
            auto rhs = parseExpr(prec + 1); // left-associative
            if (!rhs) return nullptr;
            lhs = std::make_unique<CommaExprAST>(std::move(lhs), std::move(rhs));
            applyLocation(lhs.get(), startTok);
            continue;
        }

        // Handle ternary operator specially (right-to-left, needs ? and :)
        if (opToken->type == TokenType::TOKEN_QUESTION) {
            advance(); // consume '?'
            auto thenExpr = parseExpr(); // then-branch is a full expression (may contain ',')
            if (!thenExpr) return nullptr;
            match(TokenType::TOKEN_COLON);
            auto elseExpr = parseExpr(2); // else-branch: ',' separates, not an operator
            if (!elseExpr) return nullptr;
            lhs = std::make_unique<TernaryExprAST>(std::move(lhs), std::move(thenExpr), std::move(elseExpr));
            applyLocation(lhs.get(), startTok);
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
            applyLocation(lhs.get(), startTok);
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
            applyLocation(lhs.get(), startTok);
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

bool Parser::rejectPreprocessorDirective() {
    if (!check(TokenType::TOKEN_HASH)) return false;
    auto hash = advance();
    error("preprocessor directives are not supported (SafeModern C has no preprocessor)", *hash);
    // Skip the remainder of the directive line to avoid cascading errors.
    while (peek() && peek()->type != TokenType::TOKEN_EOS && peek()->line == hash->line) {
        advance();
    }
    return true;
}

std::unique_ptr<StmtAST> Parser::parseStmt() {
    auto stmtStart = peek();
    if (rejectPreprocessorDirective()) {
        return std::make_unique<NullStmtAST>();
    }

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

    // switch statement
    if (peek() && peek()->type == TokenType::TOKEN_SWITCH) {
        return parseSwitchStmt();
    }

    // break statement
    if (peek() && peek()->type == TokenType::TOKEN_BREAK) {
        return parseBreakStmt();
    }

    // continue statement
    if (peek() && peek()->type == TokenType::TOKEN_CONTINUE) {
        return parseContinueStmt();
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

    // defer statement
    if (peek() && peek()->type == TokenType::TOKEN_DEFER) {
        advance(); // 消耗 "defer"
        auto callExpr = parseExpr();
        if (!callExpr) {
            errorUnexpected("expected expression after 'defer'");
            return nullptr;
        }
        expect(TokenType::TOKEN_SEMICOLON, "expected ';' after defer statement");
        return std::make_unique<DeferStmtAST>(std::move(callExpr));
    }

    // compound statement (block)
    if (peek() && peek()->type == TokenType::TOKEN_LBRACE) {
        return parseCompoundStmt();
    }

    // local constexpr declaration: `constexpr T name = ...;`
    if (check(TokenType::TOKEN_CONSTEXPR)) {
        auto decl = parseDeclaration();
        if (decl) {
            return std::make_unique<DeclStmtAST>(std::move(decl));
        }
    }

    // local declaration (type keyword or typedef name)
    if (isTypeStart() || check(TokenType::TOKEN_CONST) || check(TokenType::TOKEN_VOLATILE)) {
        size_t savedPos = m_currentTokenPos;
        Type* type = parseType();
        // A function-pointer declarator starts with '(' after the type
        // (e.g. `int (*fp)(int)`); let parseDeclaration handle it.
        if (type && check(TokenType::TOKEN_LPAREN)) {
            m_currentTokenPos = savedPos;
            auto decl = parseDeclaration();
            if (decl) {
                return std::make_unique<DeclStmtAST>(std::move(decl));
            }
            m_currentTokenPos = savedPos;
        }
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
            auto varDecl = parseVariableDeclList(type, nameTok->lexeme);
            if (varDecl) {
                applyLocation(varDecl.get(), stmtStart ? &*stmtStart : nullptr);
                return std::make_unique<DeclStmtAST>(std::move(varDecl));
            }
        }
        // Not a declaration, restore position
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

std::unique_ptr<StmtAST> Parser::parseSwitchStmt() {
    auto switchToken = match(TokenType::TOKEN_SWITCH);
    if (!switchToken) {
        return nullptr;
    }

    expect(TokenType::TOKEN_LPAREN, "expected '(' after 'switch'");
    auto cond = parseExpr();
    if (!cond) {
        errorUnexpected("expected expression after 'switch ('");
    }
    expect(TokenType::TOKEN_RPAREN, "expected ')' after switch condition");
    if (!expect(TokenType::TOKEN_LBRACE, "expected '{' after switch condition")) {
        return nullptr;
    }

    std::vector<std::unique_ptr<StmtAST>> cases;
    std::vector<std::unique_ptr<ExprAST>> labels;
    std::vector<std::unique_ptr<StmtAST>> body;
    bool haveGroup = false;

    // Statements belong to the most recent label; push the previous group when
    // a new label starts so `cases` and `caseLabels` stay index-aligned.
    auto flushGroup = [&]() {
        if (!haveGroup) return;
        cases.push_back(std::make_unique<CompoundStmtAST>(std::move(body)));
        body.clear();
        haveGroup = false;
    };

    while (!eof() && !check(TokenType::TOKEN_RBRACE)) {
        if (match(TokenType::TOKEN_CASE)) {
            flushGroup();
            auto label = parseExpr();
            if (!label) {
                errorUnexpected("expected constant expression after 'case'");
                break;
            }
            expect(TokenType::TOKEN_COLON, "expected ':' after case label");
            labels.push_back(std::move(label));
            haveGroup = true;
        } else if (match(TokenType::TOKEN_DEFAULT)) {
            flushGroup();
            expect(TokenType::TOKEN_COLON, "expected ':' after 'default'");
            labels.push_back(nullptr);
            haveGroup = true;
        } else {
            auto inner = parseStmt();
            if (!inner) break;
            body.push_back(std::move(inner));
        }
    }
    flushGroup();
    expect(TokenType::TOKEN_RBRACE, "expected '}' to close switch");

    auto stmt = std::make_unique<SwitchStmtAST>(std::move(cond), std::move(cases));
    stmt->caseLabels = std::move(labels);
    stmt->setLocation(switchToken->filename, switchToken->line, switchToken->column);
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
    
    // 新增：支持模块声明
    std::string moduleName;
    std::vector<std::string> imports;
    std::vector<std::string> exports;
    
    while (!eof()) {
        // namespace declaration
        if (check(TokenType::TOKEN_NAMESPACE)) {
            auto decl = parseNamespaceDecl();
            if (decl) {
                decls.push_back(std::move(decl));
            } else {
                advance();
            }
            continue;
        }

        // 检查模块声明：`module a.b.c;`（点分或 `::` 连接）
        if (check(TokenType::TOKEN_MODULE)) {
            advance(); // 消耗 "module"
            if (check(TokenType::TOKEN_IDENTIFIER)) {
                moduleName = advance()->lexeme;
                while (check(TokenType::TOKEN_DOT) || check(TokenType::TOKEN_COLON_COLON)) {
                    advance();
                    auto part = match(TokenType::TOKEN_IDENTIFIER);
                    if (!part) break;
                    moduleName += "." + part->lexeme;
                }
            }
            match(TokenType::TOKEN_SEMICOLON);
            continue;
        }
        
        // import declaration: `import a.b;` or `import "dir/file.smc";`
        if (check(TokenType::TOKEN_IMPORT)) {
            advance(); // consume "import"
            std::string importName;
            if (check(TokenType::TOKEN_STRING)) {
                std::string lex = advance()->lexeme;
                if (lex.size() >= 2 && lex.front() == '"' && lex.back() == '"') {
                    importName = lex.substr(1, lex.size() - 2);
                }
            } else if (check(TokenType::TOKEN_IDENTIFIER)) {
                importName = advance()->lexeme;
                while (check(TokenType::TOKEN_DOT)) {
                    advance();
                    auto part = match(TokenType::TOKEN_IDENTIFIER);
                    if (!part) break;
                    importName += "." + part->lexeme;
                }
            }
            if (!importName.empty()) {
                imports.push_back(importName);
            }
            match(TokenType::TOKEN_SEMICOLON);
            continue;
        }
        
        // 检查导出声明：`export`/`public` 是可见性修饰符，后接一个完整声明
        // （P0-04 / MOD-05）。`export namespace A { ... }` 会导出 A 的全部成员。
        if (check(TokenType::TOKEN_EXPORT) || check(TokenType::TOKEN_PUBLIC)) {
            advance(); // 消耗 "export"/"public"
            // `export namespace A { ... }` exports the whole namespace.
            if (check(TokenType::TOKEN_NAMESPACE)) {
                auto nsDecl = parseNamespaceDecl();
                if (nsDecl) {
                    nsDecl->isExported = true;
                    decls.push_back(std::move(nsDecl));
                } else {
                    advance();
                }
                continue;
            }
            auto decl = parseDeclaration();
            if (decl) {
                decl->isExported = true;
                decls.push_back(std::move(decl));
            } else {
                advance();
            }
            continue;
        }

        // Reject C preprocessor directives at the top level (NG-02).
        if (rejectPreprocessorDirective()) {
            continue;
        }

        auto decl = parseDeclaration();
        if (decl) {
            decls.push_back(std::move(decl));
        } else {
            advance();
        }
    }
    
    auto tu = std::make_unique<TranslationUnitAST>(std::move(decls));
    tu->imports = imports;
    tu->moduleName = moduleName;

    // 如果有模块声明，创建模块声明节点（imports 已记录在 TU 上）
    if (!moduleName.empty()) {
        tu->declarations.push_back(
            std::make_unique<ModuleDeclAST>(moduleName, imports, std::move(exports)));
    }

    // Tag this unit's own declarations with the owning module so the semantic
    // analyzer can apply module visibility (P0-04 / MOD-05/06). Imported
    // modules are tagged separately by the ModuleLoader.
    if (!moduleName.empty()) {
        for (auto& decl : tu->declarations) {
            if (decl && decl->moduleName.empty()) {
                decl->moduleName = moduleName;
            }
        }
    }

    return tu;
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
        case TokenType::TOKEN_BOOL:
        case TokenType::TOKEN_STRUCT:
        case TokenType::TOKEN_UNION:
        case TokenType::TOKEN_ENUM:
        case TokenType::TOKEN_TYPEDEF:
        case TokenType::TOKEN_INT8:
        case TokenType::TOKEN_INT16:
        case TokenType::TOKEN_INT32:
        case TokenType::TOKEN_INT64:
        case TokenType::TOKEN_INT128:
        case TokenType::TOKEN_UINT8:
        case TokenType::TOKEN_UINT16:
        case TokenType::TOKEN_UINT32:
        case TokenType::TOKEN_UINT64:
        case TokenType::TOKEN_UINT128:
        case TokenType::TOKEN_ISIZE:
        case TokenType::TOKEN_USIZE:
        case TokenType::TOKEN_FLOAT32:
        case TokenType::TOKEN_FLOAT64:
            return true;
        case TokenType::TOKEN_IDENTIFIER: {
            // A possibly `::`-qualified typedef/class/struct name (lookahead so
            // the cursor is not disturbed).
            size_t i = m_currentTokenPos;
            if (i >= m_tokens.size()) return false;
            std::string name = m_tokens[i].lexeme;
            while (i + 2 < m_tokens.size() &&
                   m_tokens[i + 1].type == TokenType::TOKEN_COLON_COLON &&
                   m_tokens[i + 2].type == TokenType::TOKEN_IDENTIFIER) {
                name += "::" + m_tokens[i + 2].lexeme;
                i += 2;
            }
            return lookupNamedType(name) != nullptr;
        }
        default:
            return false;
    }
}

std::string Parser::parseQualifiedTypeName() {
    std::string name;
    if (!check(TokenType::TOKEN_IDENTIFIER)) {
        return name;
    }
    name = advance()->lexeme;
    while (check(TokenType::TOKEN_COLON_COLON)) {
        size_t saved = m_currentTokenPos;
        advance(); // consume '::'
        if (!check(TokenType::TOKEN_IDENTIFIER)) {
            m_currentTokenPos = saved;
            break;
        }
        name += "::" + advance()->lexeme;
    }
    return name;
}

std::string Parser::qualifyTypeDeclName(const std::string& name) const {
    if (name.empty()) return name;
    if (name.find("::") != std::string::npos || name.find('.') != std::string::npos) {
        return mangleQualifiedTypeName(name);
    }
    if (!m_typeNamespacePrefix.empty()) {
        return m_typeNamespacePrefix + name;
    }
    return name;
}

Type* Parser::lookupNamedType(const std::string& name) const {
    if (name.empty()) return nullptr;
    auto& tc = TypeContext::instance();
    auto tryKey = [&tc](const std::string& key) -> Type* {
        if (Type* t = tc.getTypedef(key)) return t;
        if (ClassType* c = tc.getClass(key)) return c;
        if (StructType* s = tc.getStruct(key)) return s;
        return nullptr;
    };
    if (name.find("::") != std::string::npos || name.find('.') != std::string::npos) {
        return tryKey(mangleQualifiedTypeName(name));
    }
    // Unqualified: prefer the enclosing namespace, then the global name.
    if (!m_typeNamespacePrefix.empty()) {
        if (Type* t = tryKey(m_typeNamespacePrefix + name)) return t;
    }
    return tryKey(name);
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
        case TokenType::TOKEN_BOOL: {
            advance();
            return TypeContext::instance().getBool();
        }
        case TokenType::TOKEN_INT8: {
            advance();
            return TypeContext::instance().getInt8();
        }
        case TokenType::TOKEN_INT16: {
            advance();
            return TypeContext::instance().getInt16();
        }
        case TokenType::TOKEN_INT32: {
            advance();
            return TypeContext::instance().getInt32();
        }
        case TokenType::TOKEN_INT64: {
            advance();
            return TypeContext::instance().getInt64();
        }
        case TokenType::TOKEN_INT128: {
            advance();
            return TypeContext::instance().getInt128();
        }
        case TokenType::TOKEN_UINT8: {
            advance();
            return TypeContext::instance().getUInt8();
        }
        case TokenType::TOKEN_UINT16: {
            advance();
            return TypeContext::instance().getUInt16();
        }
        case TokenType::TOKEN_UINT32: {
            advance();
            return TypeContext::instance().getUInt32();
        }
        case TokenType::TOKEN_UINT64: {
            advance();
            return TypeContext::instance().getUInt64();
        }
        case TokenType::TOKEN_UINT128: {
            advance();
            return TypeContext::instance().getUInt128();
        }
        case TokenType::TOKEN_ISIZE: {
            advance();
            return TypeContext::instance().getISize();
        }
        case TokenType::TOKEN_USIZE: {
            advance();
            return TypeContext::instance().getUSize();
        }
        case TokenType::TOKEN_FLOAT32: {
            advance();
            return TypeContext::instance().getFloat32();
        }
        case TokenType::TOKEN_FLOAT64: {
            advance();
            return TypeContext::instance().getFloat64();
        }
        case TokenType::TOKEN_STRUCT: {
            advance(); // consume 'struct'
            std::string name = parseQualifiedTypeName();
            std::string key = qualifyTypeDeclName(name);

            // Check if this is a definition or just a reference
            if (check(TokenType::TOKEN_LBRACE)) {
                advance(); // consume '{'
                auto* structType = new StructType(key);
                while (!eof() && !check(TokenType::TOKEN_RBRACE)) {
                    Type* fieldType = parseType();
                    if (!fieldType) break;
                    if (!check(TokenType::TOKEN_IDENTIFIER)) break;
                    std::string fieldName = advance()->lexeme;
                    fieldType = parseMemberArraySuffix(fieldType);
                    structType->addField(fieldName, fieldType);
                    match(TokenType::TOKEN_SEMICOLON);
                }
                match(TokenType::TOKEN_RBRACE);
                match(TokenType::TOKEN_SEMICOLON);
                TypeContext::instance().addStruct(key, structType);
                return structType;
            }

            // Reference to an existing struct (qualified or, inside a
            // namespace, the global name).
            if (!name.empty()) {
                if (auto* existing = TypeContext::instance().getStruct(key)) return existing;
                if (key != name) {
                    if (auto* existing = TypeContext::instance().getStruct(name)) return existing;
                }
            }

            // Forward reference - create placeholder under the qualified key
            auto* structType = new StructType(key);
            TypeContext::instance().addStruct(key, structType);
            return structType;
        }
        case TokenType::TOKEN_UNION: {
            advance(); // consume 'union'
            std::string name = parseQualifiedTypeName();
            std::string key = qualifyTypeDeclName(name);

            if (check(TokenType::TOKEN_LBRACE)) {
                advance(); // consume '{'
                auto* unionType = new UnionType(key);
                while (!eof() && !check(TokenType::TOKEN_RBRACE)) {
                    Type* memberType = parseType();
                    if (!memberType) break;
                    if (!check(TokenType::TOKEN_IDENTIFIER)) break;
                    std::string memberName = advance()->lexeme;
                    memberType = parseMemberArraySuffix(memberType);
                    unionType->addMember(memberName, memberType);
                    match(TokenType::TOKEN_SEMICOLON);
                }
                match(TokenType::TOKEN_RBRACE);
                match(TokenType::TOKEN_SEMICOLON);
                TypeContext::instance().addUnion(key, unionType);
                return unionType;
            }

            if (!name.empty()) {
                if (auto* existing = TypeContext::instance().getUnion(key)) return existing;
                if (key != name) {
                    if (auto* existing = TypeContext::instance().getUnion(name)) return existing;
                }
            }

            auto* unionType = new UnionType(key);
            TypeContext::instance().addUnion(key, unionType);
            return unionType;
        }
        case TokenType::TOKEN_ENUM: {
            advance(); // consume 'enum'
            std::string name = parseQualifiedTypeName();
            std::string key = qualifyTypeDeclName(name);

            if (check(TokenType::TOKEN_LBRACE)) {
                advance(); // consume '{'
                auto* enumType = new EnumType(key);
                int currentVal = 0;
                while (!eof() && !check(TokenType::TOKEN_RBRACE)) {
                    if (!check(TokenType::TOKEN_IDENTIFIER)) break;
                    std::string valueName = advance()->lexeme;
                    int val = currentVal;
                    if (check(TokenType::TOKEN_ASSIGN)) {
                        advance();
                        auto expr = parseExpr(2); // ',' separates enumerators
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
                TypeContext::instance().addEnum(key, enumType);
                return enumType;
            }

            if (!name.empty()) {
                if (auto* existing = TypeContext::instance().getEnum(key)) return existing;
                if (key != name) {
                    if (auto* existing = TypeContext::instance().getEnum(name)) return existing;
                }
            }

            auto* enumType = new EnumType(key);
            TypeContext::instance().addEnum(key, enumType);
            return enumType;
        }
        case TokenType::TOKEN_IDENTIFIER: {
            // A (possibly namespace-qualified) named type: typedef/class/struct.
            // Look ahead without consuming so a non-type identifier leaves the
            // cursor untouched for callers that probe.
            size_t saved = m_currentTokenPos;
            std::string name = parseQualifiedTypeName();
            if (Type* named = lookupNamedType(name)) {
                return named;
            }
            m_currentTokenPos = saved;
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

    // 新增：支持切片类型 T[]
    if (check(TokenType::TOKEN_LBRACKET)) {
        advance(); // 消耗 '['
        if (check(TokenType::TOKEN_RBRACKET)) {
            advance(); // 消耗 ']'
            return TypeContext::instance().getSliceType(baseType);
        }
    }

    // 新增：支持可选类型 T?
    if (check(TokenType::TOKEN_QUESTION)) {
        advance(); // 消耗 '?'
        return TypeContext::instance().getOptionalType(baseType);
    }

    return baseType;
}

std::unique_ptr<DeclAST> Parser::parseDeclaration() {
    auto startTok = peek();
    auto decl = parseDeclarationImpl();
    if (decl) {
        applyLocation(decl.get(), startTok ? &*startTok : nullptr);
    }
    return decl;
}

std::unique_ptr<DeclAST> Parser::parseDeclarationImpl() {
    // C linkage: `extern` declarations keep their plain, unmangled symbol name.
    if (check(TokenType::TOKEN_EXTERN)) {
        advance();
        auto decl = parseDeclaration();
        if (auto* fn = dynamic_cast<FunctionDeclAST*>(decl.get())) {
            markCName(fn->name);
        }
        return decl;
    }

    if (check(TokenType::TOKEN_TYPEDEF)) {
        return parseTypedefDecl();
    }

    // 新增：支持 using 类型别名
    if (check(TokenType::TOKEN_IDENTIFIER)) {
        std::string name = peek()->lexeme;
        if (name == "using") {
            size_t savedPos = m_currentTokenPos;
            advance(); // 消耗 "using"
            // 解析: using Name = Type;
            if (check(TokenType::TOKEN_IDENTIFIER)) {
                std::string aliasName = qualifyTypeDeclName(advance()->lexeme);
                if (match(TokenType::TOKEN_ASSIGN)) {
                    Type* aliasedType = parseType();
                    if (aliasedType) {
                        match(TokenType::TOKEN_SEMICOLON);
                        TypeContext::instance().addTypedef(aliasName, aliasedType);
                        return std::make_unique<UsingDeclAST>(aliasName, aliasedType);
                    }
                }
            }
            // 如果解析失败，回退
            m_currentTokenPos = savedPos;
        }
    }

    // 新增：支持 type 新类型（distinct type）
    if (check(TokenType::TOKEN_IDENTIFIER)) {
        std::string name = peek()->lexeme;
        if (name == "type") {
            size_t savedPos = m_currentTokenPos;
            advance(); // 消耗 "type"
            // 解析: type Name = Type;
            if (check(TokenType::TOKEN_IDENTIFIER)) {
                std::string typeName = qualifyTypeDeclName(advance()->lexeme);
                if (match(TokenType::TOKEN_ASSIGN)) {
                    Type* aliasedType = parseType();
                    if (aliasedType) {
                        match(TokenType::TOKEN_SEMICOLON);
                        TypeContext::instance().addTypedef(typeName, aliasedType);
                        return std::make_unique<TypeDeclAST>(typeName, aliasedType);
                    }
                }
            }
            // 如果解析失败，回退
            m_currentTokenPos = savedPos;
        }
    }

    if (check(TokenType::TOKEN_STRUCT)) {
        size_t savedPos = m_currentTokenPos;
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

        // If this is a forward declaration (empty fields) and not followed by ';',
        // it might be a type reference (e.g., "struct Point operator+(...)")
        // Fall through to normal type parsing
        if (structDecl->fields.empty() && !check(TokenType::TOKEN_SEMICOLON)) {
            m_currentTokenPos = savedPos;
            return parseDeclarationAsType();
        }

        return structDecl;
    }

    if (check(TokenType::TOKEN_CLASS)) {
        auto classDecl = parseClassDecl();
        if (!classDecl) return nullptr;

        // Register class type in TypeContext (methods will be added by semantic analyzer)
        auto existingType = TypeContext::instance().getClass(classDecl->name);
        if (!existingType) {
            auto* classType = new ClassType(classDecl->name);
            for (auto& field : classDecl->fields) {
                classType->addField(field.first, field.second);
            }
            TypeContext::instance().addClass(classDecl->name, classType);
        }

        // Check if there's a variable name after class declaration
        if (check(TokenType::TOKEN_IDENTIFIER)) {
            auto nameTok = advance();
            auto type = TypeContext::instance().getClass(classDecl->name);
            if (!type) {
                type = new ClassType(classDecl->name);
                TypeContext::instance().addClass(classDecl->name, static_cast<ClassType*>(type));
            }
            return parseVariableDecl(type, nameTok->lexeme);
        }

        return classDecl;
    }

    if (check(TokenType::TOKEN_UNION)) {
        size_t savedPos = m_currentTokenPos;
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

        // If this is a forward declaration (empty members) and not followed by ';',
        // it might be a type reference (e.g., "union U operator+(...)")
        // Fall through to normal type parsing
        if (unionDecl->members.empty() && !check(TokenType::TOKEN_SEMICOLON)) {
            m_currentTokenPos = savedPos;
            return parseDeclarationAsType();
        }

        return unionDecl;
    }

    if (check(TokenType::TOKEN_ENUM)) {
        size_t savedPos = m_currentTokenPos;
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

        // If this is a forward declaration (empty values) and not followed by ';',
        // it might be a type reference (e.g., "enum E operator+(...)")
        // Fall through to normal type parsing
        if (enumDecl->values.empty() && !check(TokenType::TOKEN_SEMICOLON)) {
            m_currentTokenPos = savedPos;
            return parseDeclarationAsType();
        }

        return enumDecl;
    }

    return parseDeclarationAsType();
}

// Strip the pointer/array/slice/optional layers introduced by the first
// declarator so subsequent declarators in `int* a, b;` see the base type.
static Type* stripDeclaratorType(Type* type) {
    while (type) {
        switch (type->kind) {
            case TypeKind::Pointer:
                type = type->base;
                break;
            case TypeKind::Array:
                type = static_cast<ArrayType*>(type)->elementType;
                break;
            case TypeKind::Slice:
                type = static_cast<SliceType*>(type)->elementType;
                break;
            case TypeKind::Optional:
                type = static_cast<OptionalType*>(type)->elementType;
                break;
            default:
                return type;
        }
    }
    return type;
}

std::unique_ptr<DeclAST> Parser::parseDeclarationAsType() {
    // Handle constexpr declaration specifier
    bool isConstexpr = false;
    if (check(TokenType::TOKEN_CONSTEXPR)) {
        advance();
        isConstexpr = true;
    }

    Type* type = parseType();
    if (!type) return nullptr;

    // Check for operator keyword (operator overloading)
    if (check(TokenType::TOKEN_OPERATOR)) {
        advance(); // consume 'operator'
        auto opToken = peek();
        if (!opToken) {
            errorUnexpected("expected operator symbol after 'operator'");
            return nullptr;
        }
        // Map token type to operator string (token lexemes may be empty for single-char tokens)
        std::string opSym;
        switch (opToken->type) {
            case TokenType::TOKEN_PLUS:      opSym = "+"; break;
            case TokenType::TOKEN_MINUS:     opSym = "-"; break;
            case TokenType::TOKEN_STAR:      opSym = "*"; break;
            case TokenType::TOKEN_SLASH:     opSym = "/"; break;
            case TokenType::TOKEN_PERCENT:   opSym = "%"; break;
            case TokenType::TOKEN_EQ:        opSym = "=="; break;
            case TokenType::TOKEN_NOT_EQ:    opSym = "!="; break;
            case TokenType::TOKEN_LT:        opSym = "<"; break;
            case TokenType::TOKEN_GT:        opSym = ">"; break;
            case TokenType::TOKEN_LE:        opSym = "<="; break;
            case TokenType::TOKEN_GE:        opSym = ">="; break;
            case TokenType::TOKEN_AND:       opSym = "&&"; break;
            case TokenType::TOKEN_OR:        opSym = "||"; break;
            case TokenType::TOKEN_BIT_AND:   opSym = "&"; break;
            case TokenType::TOKEN_BIT_OR:    opSym = "|"; break;
            case TokenType::TOKEN_CARET:     opSym = "^"; break;
            case TokenType::TOKEN_LSHIFT:    opSym = "<<"; break;
            case TokenType::TOKEN_RSHIFT:    opSym = ">>"; break;
            case TokenType::TOKEN_TILDE:     opSym = "~"; break;
            case TokenType::TOKEN_PLUS_PLUS: opSym = "++"; break;
            case TokenType::TOKEN_MINUS_MINUS: opSym = "--"; break;
            default:
                errorUnexpected("expected operator symbol after 'operator'");
                return nullptr;
        }
        std::string opName = "operator" + opSym;
        advance(); // consume operator symbol
        
        if (!check(TokenType::TOKEN_LPAREN)) {
            errorUnexpected("expected '(' after operator name");
            return nullptr;
        }
        return parseFunctionDecl(type, opName, isConstexpr);
    }

    // Function pointer declarator: returnType (*name)(paramTypes)
    {
        std::string fpName;
        if (Type* ptrType = parseFunctionPointerDeclarator(type, fpName, /*requireName=*/true)) {
            return parseVariableDeclList(ptrType, fpName, isConstexpr);
        }
    }

    if (!check(TokenType::TOKEN_IDENTIFIER)) return nullptr;
    auto nameTok = advance();

    // Function declaration/definition: type name '('
    if (check(TokenType::TOKEN_LPAREN)) {
        return parseFunctionDecl(type, nameTok->lexeme, isConstexpr);
    }

    // Variable declaration, possibly with several comma-separated declarators.
    return parseVariableDeclList(type, nameTok->lexeme, isConstexpr);
}

std::unique_ptr<DeclAST> Parser::parseVariableDeclList(Type* type, const std::string& firstName, bool isConstexpr) {
    auto first = parseVariableDecl(type, firstName, isConstexpr);
    if (!check(TokenType::TOKEN_COMMA)) {
        expect(TokenType::TOKEN_SEMICOLON, "expected ';' after variable declaration");
        return first;
    }

    std::vector<std::unique_ptr<DeclAST>> vars;
    vars.push_back(std::move(first));

    // `int* a, b;` — the pointer belongs to the first declarator only, so
    // subsequent declarators start from the unqualified base type.
    Type* baseType = stripDeclaratorType(type);
    while (match(TokenType::TOKEN_COMMA)) {
        Type* varType = baseType;
        while (check(TokenType::TOKEN_STAR)) {
            advance();
            varType = new Type(TypeKind::Pointer, varType);
        }
        while (check(TokenType::TOKEN_CONST) || check(TokenType::TOKEN_VOLATILE)) {
            advance();
        }
        if (!check(TokenType::TOKEN_IDENTIFIER)) {
            errorUnexpected("expected identifier after ',' in declaration");
            break;
        }
        auto extraName = advance();
        auto extra = parseVariableDecl(varType, extraName->lexeme, isConstexpr);
        if (!extra) break;
        vars.push_back(std::move(extra));
    }

    expect(TokenType::TOKEN_SEMICOLON, "expected ';' after variable declaration");
    return std::make_unique<MultiVarDeclAST>(std::move(vars));
}

Type* Parser::parseFunctionPointerDeclarator(Type* returnType, std::string& outName,
                                             bool requireName) {
    outName.clear();
    if (!check(TokenType::TOKEN_LPAREN)) return nullptr;

    size_t saved = m_currentTokenPos;
    advance(); // '('
    while (check(TokenType::TOKEN_CONST) || check(TokenType::TOKEN_VOLATILE)) advance();
    if (!check(TokenType::TOKEN_STAR)) {
        m_currentTokenPos = saved;
        return nullptr;
    }
    advance(); // '*'
    while (check(TokenType::TOKEN_CONST) || check(TokenType::TOKEN_VOLATILE)) advance();

    if (check(TokenType::TOKEN_IDENTIFIER)) {
        outName = advance()->lexeme;
    } else if (requireName) {
        m_currentTokenPos = saved;
        return nullptr;
    }

    if (!match(TokenType::TOKEN_RPAREN) || !check(TokenType::TOKEN_LPAREN)) {
        m_currentTokenPos = saved;
        outName.clear();
        return nullptr;
    }

    Type* funcType = parseFunctionPointerType(returnType);
    if (!funcType) {
        m_currentTokenPos = saved;
        outName.clear();
        return nullptr;
    }
    return new Type(TypeKind::Pointer, funcType);
}

Type* Parser::parseFunctionPointerType(Type* returnType) {
    if (!expect(TokenType::TOKEN_LPAREN, "expected '(' after function pointer name")) return nullptr;

    std::vector<Type*> paramTypes;
    bool isVarArg = false;

    if (!check(TokenType::TOKEN_RPAREN)) {
        while (true) {
            // See parseFunctionDecl: `void)` means no parameters, `void*` does not.
            if (check(TokenType::TOKEN_VOID)) {
                size_t saved = m_currentTokenPos;
                advance();
                if (check(TokenType::TOKEN_RPAREN)) {
                    break;
                }
                m_currentTokenPos = saved;
            }
            if (check(TokenType::TOKEN_ELLIPSIS)) {
                isVarArg = true;
                advance();
                break;
            }
            // Reuse the parameter parser so nested function-pointer parameters
            // (callbacks taking callbacks) decay the same way.
            auto param = parseParamDecl();
            if (!param) return nullptr;
            paramTypes.push_back(param->type);
            if (!match(TokenType::TOKEN_COMMA)) break;
        }
    }

    if (!expect(TokenType::TOKEN_RPAREN, "expected ')' after function pointer parameters")) return nullptr;
    return new FunctionType(returnType, paramTypes, isVarArg);
}

std::unique_ptr<FunctionDeclAST> Parser::parseFunctionDecl(Type* returnType, const std::string& name, bool isConstexpr) {
    if (!expect(TokenType::TOKEN_LPAREN, "expected '(' after function name")) return nullptr;

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    bool isVarArg = false;

    if (!check(TokenType::TOKEN_RPAREN)) {
        while (true) {
            // `void` denotes an empty parameter list only when followed by ')'.
            // `void*` is an ordinary parameter whose type starts with void.
            if (check(TokenType::TOKEN_VOID)) {
                size_t saved = m_currentTokenPos;
                advance();
                if (check(TokenType::TOKEN_RPAREN)) {
                    break;
                }
                m_currentTokenPos = saved;
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

    return std::make_unique<FunctionDeclAST>(name, returnType, params, body, isConstexpr, isVarArg);
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

        // Check for initializer. Parse an assignment-expression (minPrec 2) so
        // that a following `,` starts the next declarator instead of becoming
        // part of a comma expression.
        if (check(TokenType::TOKEN_ASSIGN)) {
            advance();
            init = parseExpr(2);
        }

        return std::make_unique<ArrayDeclAST>(name, type, size, std::move(init));
    }

    // Check for initializer: = expr
    if (check(TokenType::TOKEN_ASSIGN)) {
        advance();
        init = parseExpr(2);
    }

    return std::make_unique<VarDeclAST>(name, type, std::move(init), isConstexpr);
}

std::unique_ptr<ParamDeclAST> Parser::parseParamDecl() {
    Type* type = parseType();
    if (!type) return nullptr;

    // Parameter may itself be a function pointer: `int (*cb)(int, int)`.
    std::string name;
    if (Type* ptrType = parseFunctionPointerDeclarator(type, name, /*requireName=*/false)) {
        return std::make_unique<ParamDeclAST>(name, ptrType);
    }

    if (check(TokenType::TOKEN_IDENTIFIER)) {
        name = advance()->lexeme;
    }

    return std::make_unique<ParamDeclAST>(name, type);
}

std::unique_ptr<StructDeclAST> Parser::parseStructDecl() {
    if (!match(TokenType::TOKEN_STRUCT)) return nullptr;

    std::string name = qualifyTypeDeclName(parseQualifiedTypeName());

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
        fieldType = parseMemberArraySuffix(fieldType);

        fields.push_back({fieldName, fieldType});

        match(TokenType::TOKEN_SEMICOLON);
    }

    match(TokenType::TOKEN_RBRACE);
    match(TokenType::TOKEN_SEMICOLON);

    return std::make_unique<StructDeclAST>(name, std::move(fields));
}

std::unique_ptr<StructDeclAST> Parser::parseClassDecl() {
    if (!match(TokenType::TOKEN_CLASS)) return nullptr;

    std::string name = qualifyTypeDeclName(parseQualifiedTypeName());

    // Parse optional inheritance: : public BaseName
    std::string baseClass;
    if (match(TokenType::TOKEN_COLON)) {
        // Skip optional access specifier (public/private/protected)
        if (check(TokenType::TOKEN_PUBLIC) || check(TokenType::TOKEN_PRIVATE) || check(TokenType::TOKEN_IDENTIFIER)) {
            std::string access = peek()->lexeme;
            if (access == "public" || access == "private" || access == "protected") {
                advance();
            }
        }
        baseClass = qualifyTypeDeclName(parseQualifiedTypeName());
    }

    if (!check(TokenType::TOKEN_LBRACE)) {
        // Forward declaration
        auto decl = std::make_unique<StructDeclAST>(name, std::vector<std::pair<std::string, Type*>>{});
        decl->baseClass = std::move(baseClass);
        return decl;
    }

    advance(); // consume '{'

    std::vector<std::pair<std::string, Type*>> fields;
    std::vector<std::unique_ptr<FunctionDeclAST>> methods;

    while (!eof() && !check(TokenType::TOKEN_RBRACE)) {
        // Check if this looks like a member function declaration:
        // type identifier '(' ... or operator symbol '('
        size_t savedPos = m_currentTokenPos;

        if (isTypeStart()) {
            Type* declType = parseType();
            if (!declType) break;

            if (check(TokenType::TOKEN_IDENTIFIER)) {
                std::string memberName = advance()->lexeme;

                if (check(TokenType::TOKEN_LPAREN)) {
                    // Member function declaration
                    auto func = parseFunctionDecl(declType, memberName);
                    if (func) {
                        methods.push_back(std::move(func));
                    }
                } else {
                    // Field declaration
                    declType = parseMemberArraySuffix(declType);
                    fields.push_back({memberName, declType});
                    match(TokenType::TOKEN_SEMICOLON);
                }
            } else if (check(TokenType::TOKEN_OPERATOR)) {
                // Operator overload: type operator+(...)
                advance(); // consume 'operator'
                auto opToken = peek();
                if (opToken) {
                    std::string opName = "operator" + opToken->lexeme;
                    advance(); // consume operator symbol
                    auto func = parseFunctionDecl(declType, opName);
                    if (func) {
                        methods.push_back(std::move(func));
                    }
                }
            } else {
                m_currentTokenPos = savedPos;
                break;
            }
        } else {
            break;
        }
    }

    match(TokenType::TOKEN_RBRACE);
    match(TokenType::TOKEN_SEMICOLON);

    auto decl = std::make_unique<StructDeclAST>(name, std::move(fields));
    decl->methods = std::move(methods);
    decl->baseClass = std::move(baseClass);
    return decl;
}

Type* Parser::parseMemberArraySuffix(Type* base) {
    if (!base || !check(TokenType::TOKEN_LBRACKET)) return base;
    advance(); // consume '['
    int size = 1;
    if (check(TokenType::TOKEN_NUMBER)) {
        auto numTok = advance();
        size = std::get<int>(numTok->value);
    }
    expect(TokenType::TOKEN_RBRACKET, "expected ']' after array size");
    return new ArrayType(base, size);
}

std::unique_ptr<UnionDeclAST> Parser::parseUnionDecl() {
    if (!match(TokenType::TOKEN_UNION)) return nullptr;

    std::string name = qualifyTypeDeclName(parseQualifiedTypeName());

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
        memberType = parseMemberArraySuffix(memberType);

        members.push_back({memberName, memberType});

        match(TokenType::TOKEN_SEMICOLON);
    }

    match(TokenType::TOKEN_RBRACE);
    match(TokenType::TOKEN_SEMICOLON);

    return std::make_unique<UnionDeclAST>(name, std::move(members));
}

std::unique_ptr<EnumDeclAST> Parser::parseEnumDecl() {
    if (!match(TokenType::TOKEN_ENUM)) return nullptr;

    std::string name = qualifyTypeDeclName(parseQualifiedTypeName());

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
            auto expr = parseExpr(2); // ',' separates enumerators
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

    std::string name = qualifyTypeDeclName(parseQualifiedTypeName());
    if (name.empty()) return nullptr;

    match(TokenType::TOKEN_SEMICOLON);

    TypeContext::instance().addTypedef(name, type);
    return std::make_unique<TypedefDeclAST>(name, type);
}

// namespace A { ... } / namespace A::B { ... } (PAR-22)
std::unique_ptr<DeclAST> Parser::parseNamespaceDecl() {
    auto startTok = peek();
    auto decl = parseNamespaceDeclImpl();
    if (decl) {
        applyLocation(decl.get(), startTok ? &*startTok : nullptr);
    }
    return decl;
}

std::unique_ptr<DeclAST> Parser::parseNamespaceDeclImpl() {
    if (!match(TokenType::TOKEN_NAMESPACE)) return nullptr;

    std::string name;
    if (check(TokenType::TOKEN_IDENTIFIER)) {
        name = advance()->lexeme;
        // Allow both `A.B` and `A::B` nesting in the declaration name.
        while (check(TokenType::TOKEN_COLON_COLON) || check(TokenType::TOKEN_DOT)) {
            advance();
            auto part = match(TokenType::TOKEN_IDENTIFIER);
            if (!part) break;
            name += "." + part->lexeme;
        }
    }

    if (!expect(TokenType::TOKEN_LBRACE, "expected '{' after namespace name")) {
        return nullptr;
    }

    // Qualify type declarations/references inside this namespace body.
    std::string savedPrefix = m_typeNamespacePrefix;
    if (!name.empty()) {
        m_typeNamespacePrefix += mangleQualifiedTypeName(name) + "_";
    }

    std::vector<std::unique_ptr<DeclAST>> decls;
    while (!eof() && !check(TokenType::TOKEN_RBRACE)) {
        if (check(TokenType::TOKEN_NAMESPACE)) {
            auto nested = parseNamespaceDecl();
            if (nested) decls.push_back(std::move(nested));
            else advance();
            continue;
        }
        auto decl = parseDeclaration();
        if (decl) {
            decls.push_back(std::move(decl));
        } else {
            advance();
        }
    }
    expect(TokenType::TOKEN_RBRACE, "expected '}' to close namespace");
    match(TokenType::TOKEN_SEMICOLON);

    m_typeNamespacePrefix = savedPrefix;

    return std::make_unique<NamespaceDeclAST>(name, std::move(decls));
}
