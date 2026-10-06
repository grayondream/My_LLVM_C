# P1-04 compile_time 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 落地 `compile_time` 命名空间核心切片——static_assert、顶层条件编译（死代码消除）、目标/构建查询、编译期求值器、LLVM 常量集成。

**Architecture:** 新独立求值器 `CompileTimeEvaluator`（ConstValue 迁入 + STR + 布局查询）；sema 在 MethodCall/MemberAccess visit 钩子特判 `compile_time` 根链；CT-12 经节点 `ctInt/ctFloat` 字段直接出 llvm::Constant；`compile_time.if` 两个分支都 parse、sema 期选择、codegen 只出选中分支。

**Tech Stack:** 现有 C++/LLVM 管线；host TargetMachine + DataLayout（布局查询）；无新依赖。

**Spec:** `docs/superpowers/specs/2026-10-06-compile-time-design.md`（本计划从 spec 论证；执行者两份都读）

## Global Constraints

- 基线 ctest **999/999**；每任务全量绿 + 一次提交；TDD RED 先行（未亲见失败不写实现）。
- 诊断文案钉死（spec §3 表，逐字）：
  - `static_assert failed: <msg>` / `static_assert failed`
  - `compile_time argument must be a compile-time constant`
  - `compile_time.if condition must be a compile-time constant`
  - `compile_time.if condition must be a boolean`
  - `unknown compile_time member '<name>'`
  - `compile_time evaluation depth limit exceeded (64)`
  - `unknown type '<name>' in compile_time expression`
- `compile_time` **不是关键字**；仅成员名 ∈ {static_assert, if, target, build, size_of, align_of, offset_of} 的根链走编译期路径。
- DEC-05：并存——`constexpr` 语义不动；constexpr 函数调用在 CT 表达式内**委托** `SemanticAnalyzer::evaluateConstexpr`。
- `compile_time.if` 仅顶层；STR 值不得逃逸运行时（类型系统无 str，自然类型错误）。
- size_of/align_of/offset_of 返回 `usize`；`build.debug` 返回 `bool`；类型实参仅单个标识符（按名解析）。
- 新测试文件：`tests/e2e/test_compile_time.cpp`（fixture 仿 GenericE2E 的 runSource）、`tests/sema/test_compile_time.cpp`（前缀 `CT*`；含 analyzeFullyOk 式全管线助手——parse 诊断不得被吞）。

## Review Focus

1. 未选分支里 parse 期注册的类型名被使用 → 期望：明确诊断（incomplete/not found），不崩溃、不误用占位。→ pin：T6 `DeadBranchTypeUseDiagnosed`。
2. `namespace N { ... compile_time.build.debug ... }`——namespacePrefix 不得劫持根链特判 → 期望：值正确。→ pin：T3 `CompileTimeInsideNamespace`。
3. constexpr 函数在 compile_time 表达式内调用 → 委托路径返回正确值。→ pin：T4 `ConstExprFnInCompileTime`。
4. `size_of` 对含基类子对象的类 = DataLayout 布局（基类槽占位）→ 期望：与 codegen 布局一致。→ pin：T4 `SizeOfClassWithBase`。
5. STR 值赋给运行时变量 → 类型错误诊断，不崩溃。→ pin：T3 `StrEscapeDiagnosed`。

---

### Task 1: 解析层——两个顶层节点与前瞻特判

**Files:**
- Modify: `src/ast/Decl.h`（新增 `CompileTimeIfDeclAST`、`CompileTimeAssertDeclAST`，字段见 spec §1；`codegen` 返回 nullptr，实现在 `src/ast/Decl.cpp`）
- Modify: `src/frontend/Parser.cpp`（`parseDeclarationImpl`：template 分支之后加前瞻特判；新私有方法声明加 `Parser.h`）
- Modify: `src/frontend/Parser.h`（`bool isCompileTimeDeclStart();`、`std::unique_ptr<DeclAST> parseCompileTimeIf();`、`std::unique_ptr<DeclAST> parseCompileTimeAssert();`）
- Test: `tests/frontend/test_compile_time_parse.cpp`（新建，仿 test_template_parse.cpp 结构）

**Interfaces:**
- Produces: `CompileTimeIfDeclAST { unique_ptr<ExprAST> cond; vector<unique_ptr<DeclAST>> thenDecls, elseDecls; }`、`CompileTimeAssertDeclAST { unique_ptr<ExprAST> call; }`（call 为 MethodCallExprAST，object=VariableExpr("compile_time")，methodName="static_assert"）。
- 前瞻判定 `isCompileTimeDeclStart()`：peek 为 identifier `"compile_time"` 且后随 `.` 且再后随 identifier ∈ {"if","static_assert"}。

- [ ] **Step 1: 写失败测试**（test_compile_time_parse.cpp）：
  - `CompileTimeIfParses`：`compile_time.if (1) { int32 a; } else { int32 b; }` 在 TU 顶层 parse → 首声明 dynamic_cast<CompileTimeIfDeclAST*> 非空、thenDecls.size()==1、elseDecls.size()==1。
  - `CompileTimeIfElseOptional`：无 else → elseDecls.empty()。
  - `CompileTimeAssertParses`：`compile_time.static_assert(1 == 1, "x");` → CompileTimeAssertDeclAST* 非空，call 为 MethodCallExprAST 且 methodName=="static_assert"。
  - `BranchDeclsParse`：分支内 struct/函数声明都能 parse（thenDecls 内 dynamic_cast<StructDeclAST*> 成功）。
- [ ] **Step 2: 跑测试亲见失败**（编译失败=RED）：`cmake . && cmake --build . && ./bin/compiler_tests --gtest_filter='CompileTimeParse.*'`
- [ ] **Step 3: 实现**——`isCompileTimeDeclStart` 前瞻（只 peek 不消耗）；`parseCompileTimeIf`：消耗三 token → `expect(LPAREN)` → `cond = parseExpr()` → `expect(RPAREN)` → 循环 `parseDeclaration()` 至 `check(RBRACE)`（消耗 `}`）→ 可选 `else` 同构；`parseCompileTimeAssert`：消耗三 token → `expect(LPAREN)` → parseExpr 逗号分隔至 `)` → 消耗可选 `;` → 包装。`compile_time.static_assert` 无 `;` 时不报错（容错）。
- [ ] **Step 4: 全量绿**：`ctest` ≥ 999 + 新增 4。
- [ ] **Step 5: Commit** `feat(ct-01): compile_time 顶层声明解析`

### Task 2: 求值器骨架——ConstValue 迁移 + 运算

**Files:**
- Create: `src/sema/CompileTimeEvaluator.h/.cpp`
- Modify: `src/sema/SemanticAnalyzer.h`（删本地 ConstValue struct → `#include "sema/CompileTimeEvaluator.h"` + `using ConstValue = CompileTimeEvaluator::ConstValue;`；新增成员 `std::unique_ptr<CompileTimeEvaluator> m_ctEval;`（惰性）与 `CompileTimeEvaluator& ctEval();`）
- Modify: `src/sema/CMakeLists.txt` 若源码列表非 GLOB（核对；GLOB 则 `cmake .` 重跑）
- Test: `tests/sema/test_compile_time.cpp`（新建）

**Interfaces:**
- Produces:
  ```cpp
  // CompileTimeEvaluator.h（前向声明 class SemanticAnalyzer; class ExprAST; class ASTNode;）
  struct ConstValue {
      enum Type { INT, DOUBLE, CHAR, STR } type;
      long long intVal = 0; double doubleVal = 0; char charVal = 0;
      std::string strVal; // STR 时有效
  };
  class CompileTimeEvaluator {
  public:
      explicit CompileTimeEvaluator(SemanticAnalyzer& sema);
      // 不可常量折叠 → nullopt 且【无】诊断（调用方发钉死文案）；
      // compile_time 特有错误（未知成员/未知类型/深度超限）→ 诊断 + nullopt。
      std::optional<ConstValue> eval(ExprAST* expr, ASTNode& at);
  };
  ```
- 递归深度 64，超限发 `compile_time evaluation depth limit exceeded (64)`。
- VariableExpr → `sema.getConstexprValues()` 查找；constexpr 函数调用 → 委托 `sema.evaluateConstexpr(expr)`（DEC-05 共享内核）。
- 支持：Number/Float/Char/String 字面量、Unary/Binary（算术/位/比较/逻辑）、Ternary、字符串 `==`/`!=`/`+`。

- [ ] **Step 1: 写失败测试**（test_compile_time.cpp；助手：parse 整段 TU + `analyzer.analyze` + 从 `int32 f() { return <expr>; }` 提取 return 表达式 → `analyzer.ctEval().eval(expr, node)`）：
  - `EvalArithmetic`：`2 + 3 * 4` → INT 14。
  - `EvalStringCompareConcat`：`"a" + "b" == "ab"` → INT 1。
  - `EvalConstexprVar`：TU 含 `constexpr int32 k = 5;` → `k * 2` → INT 10。
  - `EvalDepthLimit`：程序化生成 100 层 `(...((1+1)+1)...)` → nullopt 且诊断含 `depth limit exceeded`。
  - `EvalNonConstantQuiet`：普通未声明变量 → nullopt 且**无**诊断。
- [ ] **Step 2: 亲见失败**（CompileTimeEvaluator.h 不存在=编译 RED）
- [ ] **Step 3: 实现**——ConstValue 迁移（SemanticAnalyzer.h 改 using；evaluator.cpp 内 eval 的 switch 按节点类型递归）；`ctEval()` 惰性 `m_ctEval = std::make_unique<CompileTimeEvaluator>(*this)`。全量回归确认 999 不破（constexpr 既有测试不动）。
- [ ] **Step 4: 全量绿**。
- [ ] **Step 5: Commit** `feat(ct-06): 编译期求值器骨架与 ConstValue 迁移`

### Task 3: target/build 查询 + sema 钩子管线

**Files:**
- Modify: `src/sema/SemanticAnalyzer.h/.cpp`（`static bool containsCompileTimeRoot(ExprAST*);`、`std::optional<ConstValue> evalCompileTime(ExprAST*, ASTNode&);`、`bool tryAnalyzeCompileTimeCall(MethodCallExprAST&);`、`bool tryAnalyzeCompileTimeChain(MemberAccessExprAST&);`；visit(MethodCallExprAST)/visit(MemberAccessExprAST) 顶部钩子；visit(BinaryExprAST/UnaryExprAST/TernaryExprAST) 顶部 guard；`setBuildConfig(bool debug, const std::string& optimize)`；成员 `bool m_ctBuildDebug = false; std::string m_ctBuildOptimize = "O0";`）
- Modify: `src/ast/Expr.h/.cpp`（MethodCallExprAST/MemberAccessExprAST/BinaryExprAST/UnaryExprAST/TernaryExprAST 加 `bool ctHandled = false; std::optional<long long> ctInt; std::optional<double> ctFloat;`；各 codegen 顶部：`ctInt` → `llvm::ConstantInt::get(ctx.getLLVMType(node.type))`（node.type 为 bool 用 i1 语义既有宽度）、`ctFloat` → ConstantFP、仅 `ctHandled` → i32 0 常量）
- Modify: `src/driver/CompilerDriver.cpp`（`analyzer.setBuildConfig(false, "O0")` 显式默认；不加 CLI flag）
- Test: `tests/sema/test_compile_time.cpp`、`tests/e2e/test_compile_time.cpp`

**Interfaces:**
- Consumes: T2 evaluator（`eval`）；`SemanticAnalyzer::resolveTypeByName`（T7/CT 已存在）。
- Produces: 钩子语义——`tryAnalyzeCompileTimeChain/Call` 返回 true = 已处理（节点置 type/ctInt/ctFloat/ctHandled），sema 不再走普通路径；`containsCompileTimeRoot` 递归扫描 MethodCall/MemberAccess 根（含 Binary/Unary/Ternary 子树）。
- 节点类型映射：`build.debug` → `typeCtx->getBool()`；`size_of` 族 → usize（T4 接管，T3 先报 unknown member 以外路径不处理）；STR 成员 → `node.type = nullptr`（普通上下文自然报错）。
- target 映射（evaluator 内）：triple 含 `linux`/`apple`→macos/`windows`→mingw+windows；arch `x86_64`/`aarch64`；cpu = `llvm::sys::getHostCPUName()`。

- [ ] **Step 1: 写失败测试**：
  - e2e `TargetOsIsLinux`：`constexpr int32 isLinux = compile_time.target.os == "linux"; int32 main() { return isLinux ? 1 : 0; }` → 1。
  - e2e `BuildDebugSetter`：runSource 变体（加 debug 参数）两态 `int32 main() { bool d = compile_time.build.debug; return d ? 1 : 0; }` → 注入 true 得 1、false 得 0。
  - e2e `StrEscapeDiagnosed`（Review Focus 5）：`int32 main() { int32 x = compile_time.target.os; return 0; }` → sema 诊断非空且不崩溃（手工管线，断言 errors 非空）。
  - sema `CompileTimeInsideNamespace`（Review Focus 2）：`namespace N { constexpr int32 ok = compile_time.build.debug; }` + main 用 `N::ok`？→ simplify：全局 `constexpr int32 ok = compile_time.build.debug;` + `namespace N { int32 f() { return ok; } }` → analyzeFullyOk true。
  - sema `UnknownMemberDiagnosed`：`compile_time.nope` → `unknown compile_time member 'nope'`。
- [ ] **Step 2: 亲见失败**。
- [ ] **Step 3: 实现**——钩子 + guard（Binary/Unary/Ternary visit 顶部：`containsCompileTimeRoot` → `evalCompileTime` 全树求值 → INT 置 node.type（int32/int64 按值域）+ ctInt；DOUBLE → float64 + ctFloat）+ evaluator 成员值逻辑 + `setBuildConfig` + codegen 常量路径。
- [ ] **Step 4: 全量绿**（重点盯既有 codegen 测试——ctValue 路径不得劫持普通 MethodCall）。
- [ ] **Step 5: Commit** `feat(ct-04/05/12): 目标/构建查询与常量落地管线`

### Task 4: 布局查询——size_of/align_of/offset_of

**Files:**
- Modify: `src/sema/CompileTimeEvaluator.h/.cpp`（私有：`llvm::Type* toLLVMType(Type* t)`（递归 + `std::unordered_map<Type*, llvm::Type*>` 缓存；标量/指针/数组/struct/class/union/enum；struct 含基类子对象槽——ClassType::baseClass 链先插基类 LLVM struct）；host TargetMachine + DataLayout 成员（惰性创建一次））
- Modify: `src/sema/SemanticAnalyzer.cpp`（tryAnalyzeCompileTimeCall 放行 size_of/align_of/offset_of；类型实参 = 单标识符 VariableExpr → `resolveTypeByName`，失败/非标识符 → `unknown type '<name>' in compile_time expression`；offset_of 第二实参 = StringExpr 字面量）
- Test: `tests/sema/test_compile_time.cpp`、`tests/e2e/test_compile_time.cpp`

**Interfaces:**
- Produces: `size_of(T)`/`align_of(T)`/`offset_of(T,"f")` → usize（`typeCtx->getUSize()`）；字段偏移 = `DataLayout.getStructLayout(...)->getElementOffset(字段序号)`（struct 字段序含基类槽 0；class 字段经 ClassType::getFieldType 序）。
- offset_of 的字段查找：StructType::fields / ClassType 字段序（与 codegen emitClassFieldGEP 同序——含基类槽）。

- [ ] **Step 1: 写失败测试**：
  - sema `SizeOfScalars`：`static_assert`-free 形式——`constexpr usize s = compile_time.size_of(int32);` → e2e 断言值 4（int32/float64=8/int8=1/bool=1/指针=8）。
  - e2e `SizeOfClassWithBase`（Review Focus 4）：`class B { public: int32 b; }; class D : B { public: float64 v; };` → `compile_time.size_of(D)` == 16（x86_64 DataLayout：i32 + pad + double）且 `offset_of(D,"v")` == 8。
  - e2e `SizeOfNestedStructUnion`：struct 含嵌套 struct 与 union 成员 → size/offset 断言。
  - e2e `ConstVarFromCompileTime`：`constexpr usize N = compile_time.size_of(int32); int32 arr`——数组维度不支持 → 改：`constexpr usize N = compile_time.size_of(float64); int32 main() { return N == 8 ? 1 : 0; }` → 1。
  - sema `ConstExprFnInCompileTime`（Review Focus 3）：`constexpr int32 twice(int32 x) { return x * 2; }` + `constexpr int32 r = compile_time.size_of(int32) * 0 + twice(21);` → e2e r==42。
  - sema `UnknownTypeArgDiagnosed`：`compile_time.size_of(Nope)` → `unknown type 'Nope' in compile_time expression`。
- [ ] **Step 2: 亲见失败**。
- [ ] **Step 3: 实现**——toLLVMType 递归 + DataLayout；class 布局注意：sema 期 ClassType 字段已齐（含基类槽由 codegen 决定——evaluator 须复刻 emitClassFieldGEP 的槽序：基类子对象为字段 0）。
- [ ] **Step 4: 全量绿**。
- [ ] **Step 5: Commit** `feat(ct-06): size_of/align_of/offset_of 布局查询`

### Task 5: static_assert（CT-02）

**Files:**
- Modify: `src/sema/SemanticAnalyzer.cpp`（tryAnalyzeCompileTimeCall 的 static_assert 分支：argc 1~2、cond eval、msg 可选 StringExpr；失败 emitError；`analyzeTopLevelDecl` 加 `CompileTimeAssertDeclAST` 分支 → visit 内层 call 的钩子路径）
- Test: `tests/sema/test_compile_time.cpp`

**Interfaces:**
- Consumes: T3 钩子、T2 evaluator。
- Produces: 顶层与函数体内 static_assert 均可用；诊断文案按 Global Constraints。

- [ ] **Step 1: 写失败测试**：
  - `StaticAssertOk`：`compile_time.static_assert(1 == 1, "ok");` → analyzeFullyOk true。
  - `StaticAssertFailsWithMsg`：`compile_time.static_assert(1 == 2, "must hold");` → 诊断含 `static_assert failed: must hold`。
  - `StaticAssertFailsNoMsg`：无 msg → 诊断含 `static_assert failed`（不含冒号后内容）。
  - `StaticAssertNonConstant`：cond 含普通变量 → `compile_time argument must be a compile-time constant`。
  - e2e `StaticAssertE2E`：编译期真断言 + 正常 main 返回值 → 0。
- [ ] **Step 2: 亲见失败**。
- [ ] **Step 3: 实现**。
- [ ] **Step 4: 全量绿**。
- [ ] **Step 5: Commit** `feat(ct-02): compile_time.static_assert`

### Task 6: compile_time.if 条件编译（CT-03 / SEM-07）

**Files:**
- Modify: `src/sema/SemanticAnalyzer.cpp`（`analyzeTopLevelDecl` 加 `CompileTimeIfDeclAST` 分支 → `visit(CompileTimeIfDeclAST&)`：cond eval → INT 选边（非零 then）→ 对选中分支逐个 `analyzeTopLevelDecl`；诊断两条钉死文案；`CompileTimeIfDeclAST` 节点记 `bool ctResolved = false; bool selectedThen;`（字段加 Decl.h）供 codegen）
- Modify: `src/ast/Decl.cpp`（`CompileTimeIfDeclAST::codegen`：ctResolved 时对选中分支逐个 `decl->codegen(ctx)`，否则返回 nullptr）
- Test: `tests/sema/test_compile_time.cpp`、`tests/e2e/test_compile_time.cpp`

**Interfaces:**
- Consumes: T1 节点、T2 evaluator。
- Produces: 未选分支不 sema 不 codegen；节点上 `ctResolved/selectedThen` 由 sema 写、codegen 读。

- [ ] **Step 1: 写失败测试**：
  - e2e `CompileTimeIfSelectsThen`：cond=`compile_time.target.os == "linux"`，then 分支 `int32 f() { return 42; }`、else 分支 `int32 f() { return "dead" 未定义符号调用; }` → main 调 f → 42。
  - e2e `CompileTimeIfSelectsElse`：cond 恒假（`compile_time.build.debug` 默认 false 不可用——用 `1 == 2`）对称构造 → 走 else。
  - e2e `DeadBranchTypeUseDiagnosed`（Review Focus 1）：else 分支声明 `struct Ghost { int32 x; };`，main 使用 `Ghost g;` → 编译失败诊断（手工管线断言 errors 非空，消息含 `Ghost`——不崩溃即可）。
  - sema `CTIfNonConstantCond`：cond 含普通变量 → `compile_time.if condition must be a compile-time constant`。
  - sema `CTIfNonBoolCond`：cond=`"str"` → `compile_time.if condition must be a boolean`。
- [ ] **Step 2: 亲见失败**。
- [ ] **Step 3: 实现**。
- [ ] **Step 4: 全量绿**。
- [ ] **Step 5: Commit** `feat(ct-03): compile_time.if 条件编译与死代码消除`

### Task 7: 硬化与文档收口

**Files:**
- Test: `tests/sema/test_compile_time.cpp`、`tests/e2e/test_compile_time.cpp`
- Modify: `TODO.md`、`Progress.md`

**Interfaces:**
- Consumes: 全部前序任务。

- [ ] **Step 1: 写硬化测试**：
  - `UserCompileTimeVarNotHijacked`：`int32 compile_time = 3;` 后 `compile_time + 1` 正常（前瞻/钩子不劫持普通标识符）——若 parse 期前瞻误判导致编译失败则修复判定。
  - `CTInFunctionBodyStaticAssert`：函数体内 static_assert（表达式路径）成功与失败各一。
  - `CTDepthLimit`：非 constexpr 上下文 100 层算术 → 诊断（若 T2 已覆盖可作 e2e 复pin）。
  - 先跑：若全绿（行为已交付）→ 记录为回归 pin，不强行造 RED。
- [ ] **Step 2: 全量 ctest 绿**。
- [ ] **Step 3: TODO.md 收口**：CT-01/02/03/04/05/06/12/14、PAR-15（注明部分：static_assert/if/查询，反射未含）、SEM-07、DEC-05（并存裁决）→ `[x]` 带日期与关键决策；CT-07/08/09/13 保留 `[ ]` 注明另轮；CT-10 注明部分完成；CT-11 注明 YAGNI（求值器无 IO）。`P1-04` 行 → `[x]`（注明切片范围与反射另轮）。
- [ ] **Step 4: Progress.md 追加**（`git add -f`）。
- [ ] **Step 5: Commit** `test(ct): 硬化用例与文档收口`

---

## Self-Review 记录

- **Spec 覆盖**：CT-01→T1、CT-02→T5、CT-03/SEM-07→T6、CT-04/05→T3、CT-06→T2/T4、CT-12→T3、CT-14/DEC-05→T2（委托）；TODO 收口→T7。CT-07/08/09/13、CT-11 切除已注明。无缺口。
- **步骤粒度**：测试步含断言值；实现步钉签名/落点/映射；无 TBD。
- **类型一致性**：ConstValue（T2 迁移后签名）在 T3/T4 引用一致；`evalCompileTime`/`containsCompileTimeRoot`/`tryAnalyzeCompileTimeCall/Chain` 命名 T3 定义、T4/T5/T6 复用；`ctResolved/selectedThen` T6 定义 T6 codegen 消费。
- **Review Focus**：5 条均有归属任务测试（T6/T3/T4/T4/T3）。
- **比例**：plan ≈ spec 体量，代码主体留给实现者。
