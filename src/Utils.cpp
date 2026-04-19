#include "Utils.h"
#include "AST.h"
#include "Token.h"
#include <sstream>

static std::string TokenTypeToString(TokenType type) {
    switch (type) {
        case TokenType::TOKEN_UNKNOWN:    return "UNKNOWN";
        case TokenType::TOKEN_IDENTIFIER: return "IDENTIFIER";
        case TokenType::TOKEN_NUMBER:     return "NUMBER";
        case TokenType::TOKEN_STRING:     return "STRING";
        case TokenType::TOKEN_CHAR:       return "CHAR";
        case TokenType::TOKEN_INT:        return "int";
        case TokenType::TOKEN_FLOAT:      return "float";
        case TokenType::TOKEN_DOUBLE:     return "double";
        case TokenType::TOKEN_CHAR_KW:    return "char";
        case TokenType::TOKEN_VOID:       return "void";
        case TokenType::TOKEN_IF:         return "if";
        case TokenType::TOKEN_ELSE:       return "else";
        case TokenType::TOKEN_FOR:        return "for";
        case TokenType::TOKEN_WHILE:      return "while";
        case TokenType::TOKEN_DO:         return "do";
        case TokenType::TOKEN_RETURN:     return "return";
        case TokenType::TOKEN_BREAK:      return "break";
        case TokenType::TOKEN_CONTINUE:   return "continue";
        case TokenType::TOKEN_STRUCT:     return "struct";
        case TokenType::TOKEN_UNION:      return "union";
        case TokenType::TOKEN_ENUM:       return "enum";
        case TokenType::TOKEN_CONST:      return "const";
        case TokenType::TOKEN_STATIC:     return "static";
        case TokenType::TOKEN_EXTERN:     return "extern";
        case TokenType::TOKEN_VOLATILE:   return "volatile";
        case TokenType::TOKEN_REGISTER:   return "register";
        case TokenType::TOKEN_SIZEOF:     return "sizeof";
        case TokenType::TOKEN_TYPEDEF:    return "typedef";
        case TokenType::TOKEN_INLINE:     return "inline";
        case TokenType::TOKEN_RESTRICT:   return "restrict";
        case TokenType::TOKEN_BOOL:       return "bool";
        case TokenType::TOKEN_GOTO:       return "goto";
        case TokenType::TOKEN_SWITCH:     return "switch";
        case TokenType::TOKEN_CASE:       return "case";
        case TokenType::TOKEN_DEFAULT:    return "default";
        case TokenType::TOKEN_PLUS:       return "+";
        case TokenType::TOKEN_MINUS:      return "-";
        case TokenType::TOKEN_STAR:       return "*";
        case TokenType::TOKEN_SLASH:      return "/";
        case TokenType::TOKEN_PERCENT:    return "%";
        case TokenType::TOKEN_EQ:         return "==";
        case TokenType::TOKEN_NOT_EQ:     return "!=";
        case TokenType::TOKEN_ASSIGN:     return "=";
        case TokenType::TOKEN_PLUS_EQ:    return "+=";
        case TokenType::TOKEN_MINUS_EQ:   return "-=";
        case TokenType::TOKEN_STAR_EQ:    return "*=";
        case TokenType::TOKEN_SLASH_EQ:   return "/=";
        case TokenType::TOKEN_LT:         return "<";
        case TokenType::TOKEN_GT:         return ">";
        case TokenType::TOKEN_LE:         return "<=";
        case TokenType::TOKEN_GE:         return ">=";
        case TokenType::TOKEN_AND:        return "&&";
        case TokenType::TOKEN_OR:         return "||";
        case TokenType::TOKEN_NOT:        return "!";
        case TokenType::TOKEN_BIT_AND:    return "&";
        case TokenType::TOKEN_BIT_OR:     return "|";
        case TokenType::TOKEN_SEMICOLON:  return ";";
        case TokenType::TOKEN_COMMA:      return ",";
        case TokenType::TOKEN_DOT:        return ".";
        case TokenType::TOKEN_ARROW:      return "->";
        case TokenType::TOKEN_LPAREN:     return "(";
        case TokenType::TOKEN_RPAREN:     return ")";
        case TokenType::TOKEN_LBRACE:     return "{";
        case TokenType::TOKEN_RBRACE:     return "}";
        case TokenType::TOKEN_LBRACKET:   return "[";
        case TokenType::TOKEN_RBRACKET:   return "]";
        case TokenType::TOKEN_EOS:        return "EOS";
        default:                          return "UNKNOWN(" + std::to_string(static_cast<int>(type)) + ")";
    }
}

static std::string TokenValueToString(const TokenValue& value) {
    if (std::holds_alternative<std::monostate>(value)) return "null";
    if (std::holds_alternative<int>(value)) return std::to_string(std::get<int>(value));
    if (std::holds_alternative<double>(value)) return std::to_string(std::get<double>(value));
    if (std::holds_alternative<char>(value)) return std::string(1, std::get<char>(value));
    if (std::holds_alternative<std::string>(value)) return std::get<std::string>(value);
    return "unknown";
}

std::string to_string(const Token& token) {
    return "Token(type=" + TokenTypeToString(token.type) +
           ", lexeme=\"" + token.lexeme +
           "\", value=" + TokenValueToString(token.value) +
           ", file=\"" + token.filename +
           "\", line=" + std::to_string(token.line) +
           ", column=" + std::to_string(token.column) +
           ")";
}

static std::string indent(int level) {
    return std::string(level * 2, ' ');
}

static std::string BinaryOpToString(BinaryOp op) {
    switch (op) {
        case BinaryOp::Add:    return "+";
        case BinaryOp::Sub:    return "-";
        case BinaryOp::Mul:    return "*";
        case BinaryOp::Div:    return "/";
        case BinaryOp::Mod:    return "%";
        case BinaryOp::Eq:     return "==";
        case BinaryOp::NotEq:  return "!=";
        case BinaryOp::Lt:     return "<";
        case BinaryOp::Gt:     return ">";
        case BinaryOp::Le:     return "<=";
        case BinaryOp::Ge:     return ">=";
        case BinaryOp::And:    return "&&";
        case BinaryOp::Or:     return "||";
        case BinaryOp::BitAnd: return "&";
        case BinaryOp::BitOr:  return "|";
        default:               return "?";
    }
}

static std::string UnaryOpToString(UnaryOp op) {
    switch (op) {
        case UnaryOp::Plus:      return "+";
        case UnaryOp::Minus:     return "-";
        case UnaryOp::Not:       return "!";
        case UnaryOp::Deref:     return "*";
        case UnaryOp::AddressOf: return "&";
        default:                 return "?";
    }
}

static std::string TypeToString(Type* type) {
    if (!type) return "null";
    switch (type->kind) {
        case TypeKind::Void:    return "void";
        case TypeKind::Int:     return "int";
        case TypeKind::Float:   return "float";
        case TypeKind::Double:  return "double";
        case TypeKind::Char:    return "char";
        case TypeKind::Pointer: return TypeToString(type->base) + "*";
        default:                return "unknown";
    }
}

class ASTPrinter {
public:
    std::ostringstream oss;
    int indentLevel = 0;

    void printIndent() {
        oss << indent(indentLevel);
    }

    void printExpr(const ExprAST* expr);
    void printStmt(const StmtAST* stmt);
    void printDecl(const DeclAST* decl);
};

void ASTPrinter::printExpr(const ExprAST* expr) {
    if (!expr) {
        printIndent();
        oss << "null\n";
        return;
    }

    if (auto* e = dynamic_cast<const NumberExprAST*>(expr)) {
        printIndent();
        oss << "NumberExpr(" << e->value << ")\n";
    }
    else if (auto* e = dynamic_cast<const FloatExprAST*>(expr)) {
        printIndent();
        oss << "FloatExpr(" << e->value << ")\n";
    }
    else if (auto* e = dynamic_cast<const CharExprAST*>(expr)) {
        printIndent();
        oss << "CharExpr('" << e->value << "')\n";
    }
    else if (auto* e = dynamic_cast<const StringExprAST*>(expr)) {
        printIndent();
        oss << "StringExpr(\"" << e->value << "\")\n";
    }
    else if (auto* e = dynamic_cast<const VariableExprAST*>(expr)) {
        printIndent();
        oss << "VariableExpr(" << e->name << ")\n";
    }
    else if (auto* e = dynamic_cast<const BinaryExprAST*>(expr)) {
        printIndent();
        oss << "BinaryExpr(" << BinaryOpToString(e->op) << ")\n";
        indentLevel++;
        printIndent(); oss << "left:\n";
        indentLevel++;
        printExpr(e->left.get());
        indentLevel--;
        printIndent(); oss << "right:\n";
        indentLevel++;
        printExpr(e->right.get());
        indentLevel--;
        indentLevel--;
    }
    else if (auto* e = dynamic_cast<const UnaryExprAST*>(expr)) {
        printIndent();
        oss << "UnaryExpr(" << UnaryOpToString(e->op) << ")\n";
        indentLevel++;
        printIndent(); oss << "operand:\n";
        indentLevel++;
        printExpr(e->operand.get());
        indentLevel--;
        indentLevel--;
    }
    else if (auto* e = dynamic_cast<const CallExprAST*>(expr)) {
        printIndent();
        oss << "CallExpr(" << e->callee << ")\n";
        indentLevel++;
        printIndent(); oss << "args:\n";
        indentLevel++;
        for (const auto& arg : e->args) {
            printExpr(arg.get());
        }
        indentLevel -= 2;
    }
    else {
        printIndent();
        oss << "UnknownExpr\n";
    }
}

void ASTPrinter::printStmt(const StmtAST* stmt) {
    if (!stmt) {
        printIndent();
        oss << "null\n";
        return;
    }

    if (auto* s = dynamic_cast<const ExprStmtAST*>(stmt)) {
        printIndent();
        oss << "ExprStmt\n";
        indentLevel++;
        printExpr(s->expr.get());
        indentLevel--;
    }
    else if (auto* s = dynamic_cast<const CompoundStmtAST*>(stmt)) {
        printIndent();
        oss << "CompoundStmt\n";
        indentLevel++;
        for (const auto& st : s->stmts) {
            printStmt(st.get());
        }
        indentLevel--;
    }
    else if (auto* s = dynamic_cast<const ReturnStmtAST*>(stmt)) {
        printIndent();
        oss << "ReturnStmt\n";
        indentLevel++;
        printExpr(s->value.get());
        indentLevel--;
    }
    else if (auto* s = dynamic_cast<const DeclStmtAST*>(stmt)) {
        printIndent();
        oss << "DeclStmt\n";
        indentLevel++;
        printDecl(s->decl.get());
        indentLevel--;
    }
    else {
        printIndent();
        oss << "UnknownStmt\n";
    }
}

void ASTPrinter::printDecl(const DeclAST* decl) {
    if (!decl) {
        printIndent();
        oss << "null\n";
        return;
    }

    if (auto* d = dynamic_cast<const VarDeclAST*>(decl)) {
        printIndent();
        oss << "VarDecl(" << d->name << ", " << TypeToString(d->type) << ")\n";
        if (d->initExpr) {
            indentLevel++;
            printIndent(); oss << "init:\n";
            indentLevel++;
            printExpr(d->initExpr.get());
            indentLevel -= 2;
        }
    }
    else if (auto* d = dynamic_cast<const FunctionDeclAST*>(decl)) {
        printIndent();
        oss << "FunctionDecl(" << d->name << ", " << TypeToString(d->returnType) << ")\n";
        indentLevel++;
        printIndent(); oss << "params:\n";
        indentLevel++;
        for (const auto& param : d->params) {
            printIndent();
            oss << "Param(" << param->name << ", " << TypeToString(param->type) << ")\n";
        }
        indentLevel--;
        printIndent(); oss << "body:\n";
        indentLevel++;
        printStmt(d->body.get());
        indentLevel -= 2;
    }
    else {
        printIndent();
        oss << "UnknownDecl\n";
    }
}

std::string to_string(const TranslationUnitAST& ast) {
    ASTPrinter printer;
    printer.oss << "TranslationUnit\n";
    printer.indentLevel++;
    for (const auto& decl : ast.declarations) {
        printer.printDecl(decl.get());
    }

    return printer.oss.str();
}
