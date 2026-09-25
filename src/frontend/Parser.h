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

    // INF-03 / P0-06: stamp an AST node with the source position of the token it
    // originates from, so semantic diagnostics carry file:line:col. A null
    // `node`/`tok` is ignored.
    void applyLocation(ASTNode* node, const Token* tok);

    Type* parseType();

    Type* parseBaseType();

    // Namespace-qualified type names (PAR-22): consume `A::B::name`, returning
    // the joined name ("" when no identifier is present).
    std::string parseQualifiedTypeName();

    // Symbol-table key for a type declared at the current namespace scope.
    std::string qualifyTypeDeclName(const std::string& name) const;

    // Resolve a (possibly qualified) type name, trying the enclosing namespace
    // prefix before the global name. nullptr when not a known named type.
    Type* lookupNamedType(const std::string& name) const;

    // Turn `T name` into `T name[N]` when an array suffix follows a member
    // declarator (used for struct/class/union members).
    Type* parseMemberArraySuffix(Type* base);

    std::unique_ptr<DeclAST> parseDeclaration();

    // The raw declaration parser; `parseDeclaration` wraps it to stamp source
    // locations on the result (INF-03 / P0-06).
    std::unique_ptr<DeclAST> parseDeclarationImpl();

    std::unique_ptr<DeclAST> parseDeclarationAsType();

    std::unique_ptr<FunctionDeclAST> parseFunctionDecl(Type* returnType, const std::string& name, bool isConstexpr = false);

    std::unique_ptr<DeclAST> parseVariableDecl(Type* type, const std::string& name, bool isConstexpr = false);

    std::unique_ptr<DeclAST> parseVariableDeclList(Type* type, const std::string& firstName, bool isConstexpr = false);

    // Parse the parameter list of a function-pointer declarator, after the
    // closing ')' of "(*name)"; returns the pointed-to FunctionType.
    Type* parseFunctionPointerType(Type* returnType);

    // Parse a function-pointer declarator tail after its base return type:
    //   (* [name]) ( paramTypes )
    // Returns a pointer-to-function type with `outName` set, or nullptr (and
    // restores the position) when the input is not a function-pointer
    // declarator. When `requireName` is true an omitted name is rejected.
    Type* parseFunctionPointerDeclarator(Type* returnType, std::string& outName,
                                         bool requireName);

    std::unique_ptr<ParamDeclAST> parseParamDecl();

    std::unique_ptr<StructDeclAST> parseStructDecl();

    std::unique_ptr<StructDeclAST> parseClassDecl();

    std::unique_ptr<UnionDeclAST> parseUnionDecl();

    std::unique_ptr<EnumDeclAST> parseEnumDecl();

    std::unique_ptr<TypedefDeclAST> parseTypedefDecl();

    std::unique_ptr<DeclAST> parseNamespaceDecl();

    // The raw namespace parser; `parseNamespaceDecl` wraps it to stamp source
    // locations on the result (INF-03 / P0-06).
    std::unique_ptr<DeclAST> parseNamespaceDeclImpl();

    std::unique_ptr<ReturnStmtAST> parseReturnStmt();

    std::unique_ptr<CompoundStmtAST> parseCompoundStmt();

    std::unique_ptr<StmtAST> parseStmt();

    std::unique_ptr<StmtAST> parseIfStmt();

    std::unique_ptr<StmtAST> parseWhileStmt();

    std::unique_ptr<StmtAST> parseDoWhileStmt();

    std::unique_ptr<StmtAST> parseForStmt();

    std::unique_ptr<StmtAST> parseSwitchStmt();

    std::unique_ptr<StmtAST> parseBreakStmt();

    std::unique_ptr<StmtAST> parseContinueStmt();

    // If the current token is '#', report that preprocessor directives are
    // unsupported (NG-02) and skip the rest of the line. Returns true if it did.
    bool rejectPreprocessorDirective();

    std::unique_ptr<StmtAST> parseExprStmt();

    std::unique_ptr<ExprAST> parseExpr(int minPrec = 0);

    std::unique_ptr<ExprAST> parsePrimary();

    // Raw parsers wrapped by `parsePrimary`/`parseUnary` to stamp source
    // locations on every expression node (INF-03 / P0-06).
    std::unique_ptr<ExprAST> parsePrimaryImpl();
    std::unique_ptr<ExprAST> parseUnaryImpl();

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
    // Mangled prefix (trailing '_') active while parsing a namespace body, e.g.
    // "A_" inside `namespace A { ... }`.
    std::string m_typeNamespacePrefix;
};