#pragma once

#include <string>
#include <vector>
#include <optional>
#include "Stmt.h"

struct FoldedValue {
    enum Type { INT, DOUBLE, CHAR } type;
    union { int intVal; double doubleVal; char charVal; };
};

class DeclAST : public ASTNode {
public:
    virtual llvm::Value* codegen(CodegenContext& ctx) = 0;
};

class VarDeclAST : public DeclAST {
public:
    std::string name;
    Type* type;
    std::unique_ptr<ExprAST> initExpr;
    bool isConstexpr = false;
    std::optional<FoldedValue> foldedValue;

    VarDeclAST(const std::string& n, Type* t, std::unique_ptr<ExprAST> init = nullptr, bool constexpr_ = false)
        : name(n), type(t), initExpr(std::move(init)), isConstexpr(constexpr_) {}
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
    bool isConstexpr = false;

    FunctionDeclAST(const std::string& n, Type* ret,
                    std::vector<std::unique_ptr<ParamDeclAST>>& parameters,
                    std::unique_ptr<CompoundStmtAST>& b, bool constexpr_ = false)
        : name(n), returnType(ret), params(std::move(parameters)), body(std::move(b)), isConstexpr(constexpr_) {}
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

class ArrayDeclAST : public DeclAST {
public:
    std::string name;
    Type* elementType;
    int size;
    std::unique_ptr<ExprAST> initExpr;

    ArrayDeclAST(const std::string& n, Type* elemType, int sz, std::unique_ptr<ExprAST> init = nullptr)
        : name(n), elementType(elemType), size(sz), initExpr(std::move(init)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class StructDeclAST : public DeclAST {
public:
    std::string name;
    std::vector<std::pair<std::string, Type*>> fields;
    std::vector<std::unique_ptr<FunctionDeclAST>> methods;
    std::string baseClass;

    StructDeclAST(const std::string& n, std::vector<std::pair<std::string, Type*>> flds)
        : name(n), fields(std::move(flds)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class UnionDeclAST : public DeclAST {
public:
    std::string name;
    std::vector<std::pair<std::string, Type*>> members;

    UnionDeclAST(const std::string& n, std::vector<std::pair<std::string, Type*>> mems)
        : name(n), members(std::move(mems)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class EnumDeclAST : public DeclAST {
public:
    std::string name;
    std::vector<std::pair<std::string, int>> values;

    EnumDeclAST(const std::string& n, std::vector<std::pair<std::string, int>> vals)
        : name(n), values(std::move(vals)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class TypedefDeclAST : public DeclAST {
public:
    std::string name;
    Type* aliasedType;

    TypedefDeclAST(const std::string& n, Type* aliased)
        : name(n), aliasedType(aliased) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class ForwardDeclAST : public DeclAST {
public:
    std::string name;

    explicit ForwardDeclAST(const std::string& n) : name(n) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};
