#pragma once

#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include "sema/Diagnostic.h"
#include "ast/Symbol.h"
#include "ast/Type.h"
#include "ast/Expr.h"
#include "ast/Stmt.h"
#include "ast/Decl.h"
#include "sema/CompileTimeEvaluator.h"

class SemanticAnalyzer {
public:
    // P1-04 / CT-06 / CT-14 / DEC-05: ConstValue 权威定义迁至
    // CompileTimeEvaluator（加 STR）；别名保持既有引用点不改动。
    using ConstValue = CompileTimeEvaluator::ConstValue;

    SemanticAnalyzer();

    void analyze(TranslationUnitAST& ast);
    const std::vector<Diagnostic>& getErrors() const;
    const std::vector<Diagnostic>& getWarnings() const;

    void enterScope();
    void exitScope();
    bool declare(const std::string& name, Type* type);
    Symbol* lookup(const std::string& name);

    Type* checkBinaryTypes(BinaryOp op, Type* left, Type* right, ExprAST& node);
    Type* checkAssignmentTypes(Type* lhs, Type* rhs, ExprAST& node);
    Type* checkFunctionCall(const std::string& name, const std::vector<std::unique_ptr<ExprAST>>& args, ExprAST& node, FunctionType** outFuncType = nullptr);

    // P1-03 / GEN-03: 解析类型树中的 TypeInstance 占位为具体实例类型。
    Type* resolveTypeInstance(Type* t, ASTNode& at);
    Type* resolveTypeInstanceUse(TypeInstanceType* use, ASTNode& at);
    // 别名展开产物中的实例类型当场补 visit。
    void ensureInstanceVisited(Type* t, ASTNode& at);
    // P1-03 / INH-05: 基类实例惰性方法的首次调用触发。
    void visitLazyMethodsOf(const std::string& className);
    // CRTP：实例字段值语义自嵌套拒绝。
    void checkInstanceFieldComplete(StructDeclAST& node);
    void visitStructDeclImpl(StructDeclAST& node);
    // 按名字解析类型（模板基类实参拼写用）。
    Type* resolveTypeByName(const std::string& name);

    Type* getExprType(ExprAST& expr);
    // LEX-15: fixed-width base type for a numeric literal kind (nullptr for None).
    Type* typeForLiteralKind(LiteralKind kind);
    std::optional<ConstValue> evaluateConstexpr(ExprAST* expr);
    // P1-04 评审 I1: 以已折叠实参解释 constexpr 函数体（求值器委托用，
    // 避免 evaluateConstexpr 整树 CT 委托互递归）。
    std::optional<ConstValue> evalConstexprCallCT(CallExprAST& call,
                                                  const std::vector<ConstValue>& argValues);
    const std::unordered_map<std::string, ConstValue>& getConstexprValues() const { return constexprValues; }

    // ---- P1-04 / CT-04/05/12: compile_time 特判 ----
    // 根标识符为 `compile_time` 的成员链判定（含嵌套链 target.os）；
    // 作用域内已声明的同名变量优先（spec §1 消歧，评审 I4）。
    bool isCompileTimeRoot(const ExprAST* expr);
    // 表达式树中任一位置含 compile_time 根（Binary/Unary/Ternary 守卫用）。
    bool containsCompileTimeRoot(const ExprAST* expr);
    // P1-04 评审 I3: 毒化类型（仅 compile_time.if 未选中分支声明）使用检查。
    void checkCtDeadBranchUse(Type* t, ASTNode& at);
    // P1-05 / ANN-06: 注解目标验证与冲突检测。target: function/variable/
    // field/parameter/struct/union/enum/typedef/module。paramType 供
    // nonnull 指针检查。
    void validateAnnotations(const std::vector<Annotation>& anns, ASTNode& at,
                             const std::string& target, Type* paramType = nullptr);
    // 整树交给编译期求值器（含 compile_time 成员值与 constexpr 委托）。
    std::optional<ConstValue> evalCompileTime(ExprAST* expr, ASTNode& at);
    // MethodCall 形态（compile_time.static_assert/size_of/...）钩子：返回 true
    // 表示已处理（节点已置 type/ct 值）。
    bool tryAnalyzeCompileTimeCall(MethodCallExprAST& node);
    // MemberAccess 链形态（compile_time.target.os / build.*）钩子。
    bool tryAnalyzeCompileTimeChain(MemberAccessExprAST& node);
    // CT-05 构建查询注入（driver/测试用；不加 CLI flag）。
    void setBuildConfig(bool debug, const std::string& optimize) {
        m_ctBuildDebug = debug;
        m_ctBuildOptimize = optimize;
    }
    // 惰性求值器（持 sema 引用，构造后创建）。
    CompileTimeEvaluator& ctEval();

    // Compile-time environment used while interpreting a constexpr function.
    using ConstEnv = std::unordered_map<std::string, ConstValue>;

    // P1-04 / CT-06: 编译期求值器需要发诊断与访问内部状态。
    friend class CompileTimeEvaluator;

private:
    void emitError(const std::string& msg, const ASTNode& node);
    void emitWarning(const std::string& msg, const ASTNode& node);
    void emitError(DiagnosticCode code, const std::string& msg, const ASTNode& node);
    void emitWarning(DiagnosticCode code, const std::string& msg, const ASTNode& node);
    bool isIntegerType(Type* type) const;
    bool isFloatType(Type* type) const;
    bool isArithmeticType(Type* type) const;
    bool isPointerOrArray(Type* type) const;
    // Scalar = arithmetic or pointer/array; the valid type for a condition.
    bool isScalarType(Type* type) const;
    bool typesCompatible(Type* left, Type* right) const;
    Type* getCommonType(Type* left, Type* right) const;
    std::string typeToString(Type* type) const;
    std::string binaryOpToString(BinaryOp op) const;
    Symbol* resolveOverload(const std::string& name, const std::vector<Type*>& argTypes);
    bool isStructOrUnionType(Type* type) const;
    std::string getOperatorMangledName(BinaryOp op, Type* left, Type* right);
    // Redef 轮: true when `name` already refers to a fully defined type of
    // ANY kind (class/struct/union/enum share one type-name namespace) —
    // used to diagnose duplicate type definitions (E2004).
    bool isTypeRedefined(const std::string& name) const;
    // INH-06（方案乙）: walks the class hierarchy — `defining` (when non-null)
    // receives the class whose table provided the method, so access levels
    // and E2009 attribution stay at the definition site. Depth-capped
    // (评审 C1): a redefinition-shaped cycle must never hang the compiler.
    Symbol* resolveMethod(ClassType* classType, const std::string& methodName,
                          const std::vector<Type*>& argTypes,
                          ClassType** defining = nullptr, int depth = 0);
    bool isMethodCall(ExprAST& expr);
    bool tryAnalyzePrintCall(CallExprAST& node);
    // P1-03 / GEN-03/06: 函数模板调用——推导/显式实参 → 实例化 → 调用点
    // 重写到实例符号。返回 true = 已处理（含诊断失败）。
    bool tryAnalyzeTemplateCall(CallExprAST& node);
    bool tryAnalyzeAssertCall(CallExprAST& node);
    bool tryAnalyzePanicCall(CallExprAST& node);
    bool lowerToString(CallExprAST& node, size_t argIndex, Type* argType);
    bool hasCircularInheritance(const std::string& className, const std::string& baseClass) const;

    // SEM-01/02: definite-assignment analysis. Warns when a local variable or
    // pointer may be read before being assigned. Path-sensitive through
    // if/else (intersection at the join) and conservative for loops.
    void checkInitialization(FunctionDeclAST& node);
    void collectLocalNames(StmtAST* stmt, std::unordered_set<std::string>& out);
    void initWalkStmt(StmtAST* stmt, std::unordered_set<std::string>& state);
    void initWalkExpr(ExprAST* expr, std::unordered_set<std::string>& state);
    void initRead(const std::string& name, const ASTNode& node,
                  std::unordered_set<std::string>& state);
    std::unordered_set<std::string>* initLocals = nullptr;
    std::unordered_set<std::string> initWarned;

    // Enumerators by scoped key (e.g. "RED", "A_Red"): type + integer value.
    std::unordered_map<std::string, std::pair<Type*, int>> enumConstants;

    // constexpr function interpretation (CT-06 seed): evaluate a call and walk
    // the function body's statements (return / if / block / local decl).
    // Namespace support (PAR-22 / MOD-12). Declarations at namespace scope are
    // registered under a mangled key ("A_B_name") and lookups try the enclosing
    // namespace prefixes before the global name.
    static std::string mangleNamespaceName(const std::string& name);
    std::vector<std::string> namespaceCandidates(const std::string& name) const;
    std::string scopedName(const std::string& name) const;
    std::string resolveNamespaceName(const std::string& name) const;

    // Module visibility (P0-04 / MOD-05/06). Declarations of a participating
    // module (one that declares `module NAME;`) are registered in a per-module
    // scope; only `export`/`public` ones are additionally registered in the
    // global scope, so importers see exactly the public interface. Declarations
    // of legacy/anonymous units (moduleName empty) remain global.
    bool atGlobalLevel() const;
    Scope* moduleScopeFor(const std::string& moduleName);
    void enterModuleContext(const std::string& moduleName);
    std::unordered_map<std::string, std::unique_ptr<Scope>> moduleScopes;
    Scope* activeModuleScope = nullptr;
    std::string currentModule;
    bool currentDeclExported = false;
    bool namespaceExported = false;

    std::optional<ConstValue> evalConstexprCall(CallExprAST& call, int depth);
    std::optional<ConstValue> evalConstexprStmt(StmtAST* stmt, ConstEnv& env, int depth);
    static bool constValueTruthy(const ConstValue& v);

    void visit(TranslationUnitAST& node);
    void visit(FunctionDeclAST& node);
    void visit(VarDeclAST& node);
    void visit(ArrayDeclAST& node);
    void visit(StructDeclAST& node);
    void visit(UnionDeclAST& node);
    void visit(EnumDeclAST& node);
    void visit(TypedefDeclAST& node);
    void visit(ForwardDeclAST& node);
    void visit(UsingDeclAST& node);
    void visit(TypeDeclAST& node);
    void visit(ModuleDeclAST& node);
    void visit(NamespaceDeclAST& node);
    // P1-04 / CT-03: compile_time.if 条件编译——求值条件、选分支、原位展开。
    void visit(CompileTimeIfDeclAST& node);

    // Shared top-level dispatcher (translation unit and namespace bodies).
    void analyzeTopLevelDecl(DeclAST& decl);
    void visit(DeclStmtAST& node);
    void visit(CompoundStmtAST& node);
    void visit(ExprStmtAST& node);
    void visit(ReturnStmtAST& node);
    void visit(IfStmtAST& node);
    void visit(WhileStmtAST& node);
    void visit(DoWhileStmtAST& node);
    void visit(ForStmtAST& node);
    void visit(SwitchStmtAST& node);
    void visit(BreakStmtAST& node);
    void visit(ContinueStmtAST& node);
    void visit(NullStmtAST& node);
    void visit(DeferStmtAST& node);

    void visit(NumberExprAST& node);
    void visit(FloatExprAST& node);
    void visit(CharExprAST& node);
    void visit(StringExprAST& node);
    void visit(VariableExprAST& node);
    void visit(BinaryExprAST& node);
    void visit(UnaryExprAST& node);
    void visit(CallExprAST& node);
    void visit(AssignmentExprAST& node);
    void visit(TernaryExprAST& node);
    void visit(CastExprAST& node);
    void visit(CommaExprAST& node);
    void visit(PostfixIncDecExprAST& node);
    void visit(ArrayAccessExprAST& node);
    void visit(MemberAccessExprAST& node);
    void visit(MethodCallExprAST& node);
    void visit(SizeofExprAST& node);
    void visit(InitializerListExprAST& node);

    void visit(ExprAST& expr);
    void visit(StmtAST& stmt);

    std::vector<Diagnostic> errors;
    std::vector<Diagnostic> warnings;
    // P1-03 / GEN-03: 正在 visit 的实例化栈（诊断附 `in instantiation of`）
    // 与已 visit 实例去重集。
    std::vector<std::string> m_instStack;
    std::unordered_set<std::string> m_visitedInstances;
    // PAR-17: static 方法体 visit 标记（this 诊断）。
    bool m_inStaticMethod = false;
    // P1-03 / INH-05 / GEN-09: 惰性方法体——基类实例（CRTP）延迟到首次调用。
    int m_useDepth = 0; // 评审 I2：使用点解析深度
    static constexpr int kMaxTemplateUseDepth = 64;
    std::vector<std::string> m_definingStack; // visit 中的类名栈（CRTP 自嵌套检测）
    std::unordered_set<std::string> m_lazyInstanceMethods;
    std::unordered_map<std::string, std::vector<FunctionDeclAST*>> m_pendingLazyMethods;
    // 别名模板展开缓存：键 = 实例 mangled 名。
    std::unordered_map<std::string, Type*> m_aliasCache;
    std::unique_ptr<Scope> globalScope;
    Scope* currentScope;
    FunctionDeclAST* currentFunction;
    ClassType* currentClass = nullptr; // SEM-04: class whose method is being analyzed

    // AGG-10: desugared static-member symbol key -> {defining class, member
    // name}. Key is mangleNamespaceName of the fully qualified spelling
    // (namespace prefix included), matching what access sites resolve.
    std::unordered_map<std::string, std::pair<ClassType*, std::string>> staticMemberIndex;
    void checkStaticMemberAccess(const std::string& originalName, ExprAST& node);
    // AGG-11: flattened enclosing-class path for nested-type visits, e.g.
    // "Outer_" while visiting a type nested directly in Outer. Accumulated
    // from BARE class names so desugared static-member symbols inside nested
    // classes match what the qualified access spelling flattens to.
    std::string classPathPrefix;
    // AGG-11/DS5: flattened nested-type name -> {defining class, access level}.
    std::unordered_map<std::string, std::pair<ClassType*, AccessLevel>> nestedTypeAccess;
    void visitNestedDecl(DeclAST& node);
    void visitNestedTypeDecls(std::vector<std::unique_ptr<DeclAST>>& nestedTypes,
                              ClassType* owner);
    // AGG-11/DS5: E2009 when a private nested type is named as a var decl
    // type or a cast target outside its defining class.
    void checkNestedTypeAccess(Type* type, const ASTNode& site);
    TypeContext* typeCtx;
    std::unordered_map<std::string, ConstValue> constexprValues;

    // P1-04 / CT-04/05: build 查询注入值与惰性求值器。
    bool m_ctBuildDebug = false;
    std::string m_ctBuildOptimize = "O0";
    std::unique_ptr<CompileTimeEvaluator> m_ctEval;
    std::unordered_set<std::string> definedFunctions;
    std::unordered_map<std::string, FunctionDeclAST*> constexprFunctions;
    ConstEnv* activeEnv = nullptr; // innermost constexpr call environment
    int constexprCallDepth = 0;    // guards runaway constexpr recursion
    static constexpr int kConstexprMaxDepth = 128;
    std::string namespacePrefix;   // e.g. "A_B_" while analyzing namespace A::B
};
