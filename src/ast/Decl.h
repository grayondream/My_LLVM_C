#pragma once

#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include "Stmt.h"

struct FoldedValue {
    enum Type { INT, DOUBLE, CHAR } type;
    union { int intVal; double doubleVal; char charVal; };
};

class DeclAST : public ASTNode {
public:
    // Module visibility (P0-04 / MOD-05/06). `moduleName` is the owning module
    // (empty for legacy/anonymous units whose declarations are always visible).
    // A declaration of a participating module is visible outside it only when
    // marked `export`/`public` (`isExported`). See docs/spec/modules.md §4.
    bool isExported = false;
    std::string moduleName;

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
    bool isVarArg = false;
    // AGG-10: class-body `static` method — no `this` insertion; sema rewrites
    // the name to the desugared `Class_method` global symbol.
    bool isStatic = false;

    FunctionDeclAST(const std::string& n, Type* ret,
                    std::vector<std::unique_ptr<ParamDeclAST>>& parameters,
                    std::unique_ptr<CompoundStmtAST>& b, bool constexpr_ = false,
                    bool varArg = false)
        : name(n), returnType(ret), params(std::move(parameters)), body(std::move(b)),
          isConstexpr(constexpr_), isVarArg(varArg) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
    // P1-03 / GEN-05: 仅创建签名（无体）——TU codegen 预扫。
    llvm::Value* codegenPrototype(CodegenContext& ctx);
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
    // Module/file names named by `import` at the top level. The driver resolves
    // and loads them, splicing their declarations in front of this unit's.
    std::vector<std::string> imports;
    // The module declared by `module NAME;` at the head of this unit, or empty
    // for a legacy/anonymous unit (MOD-04). Dotted, e.g. "std.core".
    std::string moduleName;

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

// A single declaration statement that declares several variables, e.g.
// `int a = 1, b = 2;`. Each element is a VarDeclAST or ArrayDeclAST.
class MultiVarDeclAST : public DeclAST {
public:
    std::vector<std::unique_ptr<DeclAST>> decls;

    explicit MultiVarDeclAST(std::vector<std::unique_ptr<DeclAST>> d)
        : decls(std::move(d)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class StructDeclAST : public DeclAST {
public:
    std::string name;
    std::vector<std::pair<std::string, Type*>> fields;
    std::vector<std::unique_ptr<FunctionDeclAST>> methods;
    // AGG-10: static data members — parser-built VarDeclASTs. Never part of
    // `fields` (no object layout); sema rewrites the name to the desugared
    // `Class_member` global symbol and codegen emits them as globals.
    std::vector<std::unique_ptr<VarDeclAST>> staticMembers;
    // AGG-10: class name before qualifyTypeDeclName's namespace prefixing —
    // the desugared static-member symbol must combine the BARE class name
    // with sema's own namespace prefix (scopedName), matching what the fully
    // qualified access spelling flattens to.
    std::string bareName;
    std::string baseClass;

    // PAR-04/DEC-01: explicit access levels recorded by the parser (class
    // declarations record every member; structs leave this empty -> Public).
    std::unordered_map<std::string, AccessLevel> memberAccess;
    // INH-01: true when this node came from a forward declaration (`class D;`)
    // — sema uses it to keep ClassType::isComplete accurate.
    bool isForwardDecl = false;
    // AGG-11: nested type declarations (enum/struct/class/union), in source
    // order. Registered under flat keys (`Outer_Inner`) at parse time.
    std::vector<std::unique_ptr<DeclAST>> nestedTypes;
    bool isClassDecl = false;

    StructDeclAST(const std::string& n, std::vector<std::pair<std::string, Type*>> flds)
        : name(n), fields(std::move(flds)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
    void emitStaticMembers(CodegenContext& ctx);
};

class UnionDeclAST : public DeclAST {
public:
    std::string name;
    std::vector<std::pair<std::string, Type*>> members;
    // AGG-11: nested type declarations, in source order.
    std::vector<std::unique_ptr<DeclAST>> nestedTypes;
    // AGG-11: union name before qualifyTypeDeclName's namespace prefixing
    // (mirrors StructDeclAST::bareName).
    std::string bareName;
    // Redef 轮: true when from a forward declaration (`union U;`) — sema uses
    // it to keep UnionType::isComplete accurate (mirrors StructDeclAST).
    bool isForwardDecl = false;

    UnionDeclAST(const std::string& n, std::vector<std::pair<std::string, Type*>> mems)
        : name(n), members(std::move(mems)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class EnumDeclAST : public DeclAST {
public:
    std::string name;
    std::vector<std::pair<std::string, int>> values;
    // Optional explicit underlying type (`enum E : u8`); null means default int.
    Type* underlyingType;
    // AGG-11: enum name before qualifyTypeDeclName's namespace prefixing
    // (mirrors StructDeclAST::bareName).
    std::string bareName;
    // Redef 轮: true when from a forward declaration (`enum E;`) — sema uses
    // it to keep EnumType::isComplete accurate (mirrors StructDeclAST).
    bool isForwardDecl = false;

    EnumDeclAST(const std::string& n, std::vector<std::pair<std::string, int>> vals,
                Type* underlying = nullptr)
        : name(n), values(std::move(vals)), underlyingType(underlying) {}
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

// P1-03 / GEN-01 / PAR-21: 模板声明——`template<...>` 修饰函数/struct/class/
// using 别名。定义处只 parse 不 sema；实例化由 sema 侧 TemplateRegistry 驱动。
class TemplateDeclAST : public DeclAST {
public:
    struct Param {
        std::string name;
        bool isType = true;
        // isType==false 时为该非类型参数的类型节点（如 usize），否则 nullptr。
        Type* nonTypeType = nullptr;
    };

    std::vector<Param> params;
    std::unique_ptr<DeclAST> decl; // FunctionDeclAST / StructDeclAST / UsingDeclAST
    bool isAlias = false;

    llvm::Value* codegen(CodegenContext& ctx) override;
};

// 新增AST节点
class UsingDeclAST : public DeclAST {
public:
    std::string name;
    Type* aliasedType;

    UsingDeclAST(const std::string& n, Type* aliased)
        : name(n), aliasedType(aliased) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class TypeDeclAST : public DeclAST {
public:
    std::string name;
    Type* aliasedType;

    TypeDeclAST(const std::string& n, Type* aliased)
        : name(n), aliasedType(aliased) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class DeferStmtAST : public StmtAST {
public:
    std::unique_ptr<ExprAST> callExpr;

    explicit DeferStmtAST(std::unique_ptr<ExprAST> call)
        : callExpr(std::move(call)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

class ModuleDeclAST : public DeclAST {
public:
    std::string name;
    std::vector<std::string> imports;
    std::vector<std::string> exports;

    ModuleDeclAST(const std::string& n, std::vector<std::string> imp, std::vector<std::string> exp)
        : name(n), imports(std::move(imp)), exports(std::move(exp)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

// namespace Name { ... } (TODO PAR-22 / MOD-12). Members are code-generated as
// part of the enclosing translation unit; the semantic analyzer qualifies their
// names so they do not collide with the global namespace.
class NamespaceDeclAST : public DeclAST {
public:
    // Dotted/`::`-joined namespace name, e.g. "geometry.ops" or "geometry::ops".
    std::string name;
    std::vector<std::unique_ptr<DeclAST>> declarations;

    NamespaceDeclAST(const std::string& n, std::vector<std::unique_ptr<DeclAST>> decls)
        : name(n), declarations(std::move(decls)) {}
    llvm::Value* codegen(CodegenContext& ctx) override;
};

// P1-04 / CT-03: 顶层 `compile_time.if (cond) { decls... } [else { decls... }]`。
// 两个分支语法均 parse（spec compile_time.md §3）；选择在 sema 期完成。
class CompileTimeIfDeclAST : public DeclAST {
public:
    std::unique_ptr<ExprAST> cond;
    std::vector<std::unique_ptr<DeclAST>> thenDecls;
    std::vector<std::unique_ptr<DeclAST>> elseDecls;
    llvm::Value* codegen(CodegenContext& ctx) override;
};

// P1-04 / CT-02: 顶层 `compile_time.static_assert(...);` 的包装节点（call 为
// MethodCallExprAST 形态）。函数体内走普通表达式路径，无需包装。
class CompileTimeAssertDeclAST : public DeclAST {
public:
    std::unique_ptr<ExprAST> call;
    llvm::Value* codegen(CodegenContext& ctx) override;
};
