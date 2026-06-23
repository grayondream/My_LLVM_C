#pragma once

#include <string>
#include <vector>
#include "Stmt.h"

class DeclAST : public ASTNode {
public:
    virtual llvm::Value* codegen(CodegenContext& ctx) = 0;
};

class VarDeclAST : public DeclAST {
public:
    std::string name;
    Type* type;
    std::unique_ptr<ExprAST> initExpr;

    VarDeclAST(const std::string& n, Type* t, std::unique_ptr<ExprAST> init = nullptr)
        : name(n), type(t), initExpr(std::move(init)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class ParamDeclAST : public ASTNode {
public:
    std::string name;
    Type* type;

    ParamDeclAST(const std::string& n, Type* t)
        : name(n), type(t) {}
};

class FunctionDeclAST : public DeclAST {
public:
    std::string name;
    Type* returnType;
    std::vector<std::unique_ptr<ParamDeclAST>> params;
    std::unique_ptr<CompoundStmtAST> body;

    FunctionDeclAST(const std::string& n, Type* ret,
                    std::vector<std::unique_ptr<ParamDeclAST>>& parameters,
                    std::unique_ptr<CompoundStmtAST>& b)
        : name(n), returnType(ret), params(std::move(parameters)), body(std::move(b)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class DeclStmtAST : public StmtAST {
public:
    std::unique_ptr<DeclAST> decl;

    explicit DeclStmtAST(std::unique_ptr<DeclAST> d) : decl(std::move(d)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class TranslationUnitAST : public ASTNode {
public:
    std::vector<std::unique_ptr<DeclAST>> declarations;

    explicit TranslationUnitAST(std::vector<std::unique_ptr<DeclAST>> decls)
        : declarations(std::move(decls)) {}
    llvm::Value* codegen(CodegenContext& ctx);
};
