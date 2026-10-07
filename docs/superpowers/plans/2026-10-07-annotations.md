# 注解系统实施计划（P1-05）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 落地 `[[...]]` 注解：六挂载点解析、目标/冲突验证、布局三注解（repr(C)/packed/align，单源化 LayoutBuilder）、函数注解（inline/cold/nonnull）与 deprecated 使用警告（W3004）。

**Architecture:** 注解为 AST 值类型直挂节点；sema 验证（ANN-06）后把布局相关语义折叠进 Type 对象（`isPacked/forcedAlign/fieldAligns`，ctDeadBranch 先例）；布局构造单源化到 `LayoutBuilder`，codegen 与 CT 求值器共同消费。

**Tech Stack:** C++17 / LLVM（Attributes、DataLayout）/ gtest / CMake（in-source，测试 GLOB 收录）

**Spec:** `docs/superpowers/specs/2026-10-07-annotations-design.md`

## Global Constraints

- 基线 ctest **1037/1037**；每任务 TDD RED→GREEN，全量绿后一提交。
- **无注解聚合布局逐字节不变**（T4 起永久成立，回归 pin）。
- `offset_of`/`size_of`/`align_of` 与实际 IR 布局一致性由 LayoutBuilder 单源 + 内部 DataLayout 自检保证。
- 诊断文案按 spec §5 **逐字**：`unknown annotation '<name>'`、`annotation '<name>' is not valid on <target>`、`duplicate annotation '<name>'`、`align argument must be a power of two`、`annotation argument must be a compile-time constant`、`'<name>' is deprecated[: <msg>]`、`null passed to nonnull parameter '<param>' of '<fn>'`。
- **git add 只加明确文件清单，禁止 `git add -A src/ tests/`**（P1-04 教训：in-source 构建产物 CMakeFiles/*.o 会被扫入）。`Progress.md`/`docs/superpowers/` 需 `git add -f`。
- 已知 LLJIT flaky：`OptionalResultE2E.OptionalBranchExec`、`EnumUnderlyingE2E.Int8EnumNegativeValue` 偶发 SEGFAULT——隔离重跑验证，不阻塞。
- ASTNode 位置字段为 `sourceFile/sourceLine/sourceColumn`（`src/ast/Expr.h:67`）；Annotation 存 `line/column` 即可（文件随 emit 现场节点）。

## Review Focus

1. **FieldInfo 迁移语义遗漏**：某消费点仍按旧 `pair` 语义编译通过但语义错位（如匿名聚合提升 AGG-03、模板实例化字段复制）→ T2 步骤含消费点全清单 + 全量回归。
2. **无注解布局被 LayoutBuilder 意外改变**（packed literal 化/字段顺序/多余 padding）→ T4 pin `NoAnnotationLayoutUnchanged`（size_of/offset_of 快照）+ 全量回归。
3. **注解名作普通标识符**（注解名非关键字，`int32 packed = 1;` 必须正常）→ T1 pin `AnnotationNamesAreNotKeywords`。
4. **align 尾部补齐只改 size 不改字段偏移** → T5 矩阵测试断言两者。
5. **deprecated 漏报/误报**（类内 `this->method()`、extern 声明、类型用于字段）→ T6 四类使用点用例。

---

### Task 1: Annotation 值类型 + 顶层/模块/语句位置解析

**Files:**
- Create: `src/ast/Annotation.h`
- Modify: `src/ast/Decl.h`（DeclAST 基类加 `annotations`；含 ModuleDeclAST 继承即可）
- Modify: `src/frontend/Parser.h`（`std::vector<Annotation> parseAnnotations();` 私有声明）
- Modify: `src/frontend/Parser.cpp`（`parseDeclarationImpl`:1974 入口特判；`parseStmt`:980 声明语句位置特判；module 路径）
- Test: `tests/frontend/test_annotation_parse.cpp`（新建，仿 test_compile_time_parse.cpp 结构）

**Interfaces:**
- Produces（后续任务依赖，签名钉死）：
```cpp
// src/ast/Annotation.h
struct AnnotationArg {
    enum class Kind { Expr, Type, Ident, String };
    Kind kind;
    std::unique_ptr<ExprAST> expr; // Kind::Expr
    Type* type{};                  // Kind::Type
    std::string text;              // Kind::Ident / Kind::String
    int line{0};
    int column{0};
};
struct Annotation {
    std::string name;
    std::vector<AnnotationArg> args;
    int line{0};
    int column{0};
};
```
- `DeclAST` 基类：`std::vector<Annotation> annotations;`
- `Parser::parseAnnotations()`：光标处无 `[[` 返回空 vector；否则消费 `[[body]]` 序列直至无更多 `[[`，不回退。`annotation-name` 解析为 identifier（七知名不必在此区分）；`annotation-arg` 按文法 §7：const-expr→`Kind::Expr`（`parseExpr(2)`）、type→`Kind::Type`（`parseType()`，仅 `isTypeStart()` 时）、identifier→`Kind::Ident`、string→`Kind::String`。
- 挂载点解析：`parseDeclarationImpl` 入口与 `parseStmt` 的声明分支（`isTypeStart()||const/volatile` 判定前，:449/:478/:1075 同款判定的语句入口）先 `parseAnnotations()` 再继续；解析出的注解 attach 到产出的 DeclAST（parseStmt 仅在声明语句时 attach，非声明语句 → `expect` 失败报 E1002）。

- [ ] **Step 1: 写失败测试**（test_annotation_parse.cpp）：
  - `DeclAnnotationParses`：`[[inline]] int32 f() { return 0; }` → FunctionDeclAST `annotations.size()==1`、name `"inline"`。
  - `MultipleAnnotationsStack`：`[[cold]] [[inline]] int32 f()...` → size 2。
  - `AnnotationWithArgs`：`[[deprecated("old")]] int32 g()...` → args[0].kind==String、text=="old"。
  - `AlignArgIsExpr`：`[[align(16)]] struct S { int32 x; }` → args[0].kind==Expr。
  - `ModuleAnnotationParses`：`[[deprecated]] module m;` → ModuleDeclAST annotations.size()==1。
  - `LocalVarAnnotationParses`：`int32 main() { [[align(8)]] int32 x; return 0; }` → 局部 VarDecl annotations.size()==1。
  - `AnnotationNamesAreNotKeywords`：`int32 main() { int32 packed = 1; return packed; }` → parse 无错误（Review Focus 3）。
- [ ] **Step 2: 跑测试确认失败**——`./bin/compiler_tests --gtest_filter='AnnotationParse.*'`，编译失败（Annotation 未定义）即 RED。
- [ ] **Step 3: 实现**——Annotation.h、DeclAST/ParamDeclAST 挂载、`parseAnnotations()`、三处入口特判。args 的 Expr 分支用 `parseExpr(2)`（逗号分隔惯例）；type 分支仅在 `isTypeStart()` 时走 `parseType()`。
- [ ] **Step 4: 跑测试确认通过** + 全量 `ctest` 绿（基线 1037）。
- [ ] **Step 5: Commit**——`git add src/ast/Annotation.h src/ast/Decl.h src/ast/Decl.cpp src/frontend/Parser.h src/frontend/Parser.cpp tests/frontend/test_annotation_parse.cpp`（仅此清单），`feat(ann-01): 注解值类型与顶层/模块/语句位置解析`。

### Task 2: FieldInfo 化 + 字段/参数注解解析

**Files:**
- Modify: `src/ast/Annotation.h`（追加 `struct FieldInfo { std::string name; Type* type; std::vector<Annotation> annotations; };`）
- Modify: `src/ast/Decl.h`（`StructDeclAST::fields` → `std::vector<FieldInfo>`；ctor 同步；`ParamDeclAST` 加 `std::vector<Annotation> annotations;`）
- Modify: `src/ast/Type.h`（`UnionType::members` → `std::vector<FieldInfo>`）
- Modify: 消费点（**全清单，逐一迁移**，grep `\.fields`/`->fields`/`\.members`）：
  - `src/frontend/Parser.cpp`：struct 字段构造（:2053-2081 注册路径、ctor 调用点）、匿名聚合提升 AGG-03（:332-336）
  - `src/sema/SemanticAnalyzer.cpp`：visitStructDeclImpl 字段循环（:2556 起）、字段访问检查、AGG-03 提升
  - `src/sema/TemplateInstantiator.cpp`：实例化字段复制
  - `src/ast/Decl.cpp`：StructDeclAST/UnionDeclAST codegen 的字段遍历
  - `src/ast/Expr.cpp`：`getFieldType` 类消费（StructType::getFieldType 在 Type.h 内，同步）
  - `src/codegen/CodegenContext.cpp`：getLLVMType Struct/Union case 字段遍历
  - `src/sema/CompileTimeEvaluator.cpp`：offset_of 字段查找（fields[i].first）
- Modify: `src/frontend/Parser.cpp`（字段位置：struct/class/union 体解析循环内 `parseAnnotations()`；参数位置：ParamDeclAST 构造点 :2526/:2579 前挂接）
- Test: `tests/frontend/test_annotation_parse.cpp`（追加）

**Interfaces:**
- Produces: `FieldInfo`（上）；`StructDeclAST::fields` 为 `std::vector<FieldInfo>`；`UnionType::members` 为 `std::vector<FieldInfo>`；`ParamDeclAST::annotations`。
- 语义不变量：迁移后所有既有行为逐字节不变（无注解路径）。

- [ ] **Step 1: 写失败测试**：
  - `FieldAnnotationParses`：`struct S { [[packed]] int32 x; }` → fields[0].annotations.size()==1。
  - `ParamAnnotationParses`：`int32 f([[nonnull]] int32* p) { return 0; }` → params[0]->annotations.size()==1。
  - `UnionFieldAnnotationParses`：`union U { [[deprecated]] int32 i; }` → members[0].annotations.size()==1。
- [ ] **Step 2: 确认 RED**（编译失败：fields 尚无 annotations）。
- [ ] **Step 3: 实现**——先 FieldInfo 化迁移（保持行为），再接两处解析挂载。迁移以 grep 清单逐一过，**每改一处编译一次**。
- [ ] **Step 4: 全量 `ctest` 绿**（回归 pin：迁移不改变任何行为）+ 新测试绿。
- [ ] **Step 5: Commit**——`git add`（本次实际改动文件清单），`refactor(ann-01): FieldInfo 化聚合字段并挂接字段/参数注解`。

### Task 3: ANN-06 验证 + 诊断注册

**Files:**
- Modify: `src/sema/Diagnostic.h`/`Diagnostic.cpp`（注册 `SemUnknownAnnotation "E2010"`、`SemInvalidAnnotationTarget "E2011"`、`SemDuplicateAnnotation "E2012"`、`SemAlignNotPowerOfTwo "E2013"`、`SemAnnotationArgNotConstant "E2014"`）
- Modify: `src/sema/SemanticAnalyzer.{h,cpp}`（`void validateAnnotations(const std::vector<Annotation>&, ASTNode& at);` + 各 Decl visit 调用点）
- Test: `tests/sema/test_annotation_semantics.cpp`（新建）

**Interfaces:**
- Consumes: Task 1 的 `Annotation`。
- Produces: `validateAnnotations`——校验顺序：未知名（E2010）→ 重复（E2012）→ 目标（E2011，含 repr 实参非 C、nonnull 非指针参数）→ align 实参（非常量 E2014 → 折叠非 2 的幂 E2013）。调用点：visit(FunctionDeclAST)/visit(VarDeclAST)/visit(ArrayDeclAST)/visitStructDeclImpl（类型级+字段级）/visit(UnionDeclAST)/visit(EnumDeclAST)/visit(TypedefDeclAST)/visit(ModuleDeclAST)。
- 已知名集合：`{repr, packed, align, inline, cold, nonnull, deprecated}`；合法目标矩阵按 spec §3 逐字。

- [ ] **Step 1: 写失败测试**（每码至少一例，断言 message 逐字）：
  - `UnknownAnnotationE2010`：`[[bogus]] int32 f()...` → `unknown annotation 'bogus'`。
  - `WrongTargetE2011`：`[[nonnull]] int32 x;` → `annotation 'nonnull' is not valid on variable`。
  - `ReprCOnClassE2011`：`[[repr(C)]] class K { public: int32 x; }` → E2011。
  - `ReprArgMustBeC`：`[[repr(A)]] struct S { int32 x; }` → E2011。
  - `DuplicateE2012`：`[[inline]] [[inline]] int32 f()...` → `duplicate annotation 'inline'`。
  - `AlignNotPowerOfTwoE2013`：`[[align(3)]] struct S { int32 x; }` → `align argument must be a power of two`。
  - `AlignNotConstantE2014`：`int32 main() { [[align(n)]] int32 x; return 0; }`（n 未声明）→ `annotation argument must be a compile-time constant`。
- [ ] **Step 2: 确认 RED**。
- [ ] **Step 3: 实现**——注册码位；`validateAnnotations`；align 折叠走 `evaluateConstexpr`（P1-04 内核）。目标判定按节点种类传字符串（`"function"/"variable"/"field"/"parameter"/"struct"/"union"/"enum"/"typedef"/"module"`）。
- [ ] **Step 4: 全量绿**。
- [ ] **Step 5: Commit**——`feat(ann-06): 注解目标验证与冲突检测（E2010~E2014）`。

### Task 4: LayoutBuilder 单源化（无注解回归 pin）

**Files:**
- Create: `src/ast/LayoutBuilder.{h,cpp}`
- Modify: `src/codegen/CodegenContext.cpp`（getLLVMType Struct/Union case 改走 LayoutBuilder）
- Modify: `src/ast/Decl.cpp`（StructDeclAST::codegen 字段构造路径）
- Modify: `src/sema/CompileTimeEvaluator.cpp`（toLLVMType Struct/Union case + evalLayoutQuery 消费）
- Modify: `src/CMakeLists.txt`（若非 GLOB 需加源文件——当前 GLOB_RECURSE，无需）
- Test: `tests/sema/test_layout_builder.cpp`（新建）

**Interfaces:**
- Produces（签名钉死）：
```cpp
// src/ast/LayoutBuilder.h
struct LayoutField { llvm::Type* type; uint64_t offset; };
struct LayoutResult {
    std::vector<LayoutField> fields; // 含插入的显式 padding 字段
    bool isPacked{false};
    uint64_t size{0};                // getTypeAllocSize 语义
    uint64_t align{0};               // ABI 对齐
};
class LayoutBuilder {
public:
    // t: StructType/ClassType/UnionType（已剥 typedef）。内部用独立
    // LLVMContext 构造 body，并用 DataLayout 自检：构造结果的字段偏移/
    // alloc size 必须与返回值一致（不一致 = 内部错误，llvm::report_fatal_error）。
    static LayoutResult build(Type* t, llvm::LLVMContext& ctx);
};
```
- 关键语义：无注解（见 Task 5 的 Type 侧字段，本任务全为默认值）时走**现状构造路径**（自然布局，与迁移前逐字节一致）；有注解时构造显式 padding 字段列表并用 DataLayout 验证。
- 消费者改造后：`getLLVMType` 用 fields 构造（复用 getTypeByName 缓存逻辑）；evaluator 的 offset_of 改查 LayoutResult（替代现 fields[i]+getElementOffset）。

- [ ] **Step 1: 写失败测试**（test_layout_builder.cpp）：
  - `NoAnnotationLayoutUnchanged`：`struct S { int32 a; float64 b; int16 c; }` → LayoutBuilder.build 结果：a@0、b@8、c@16、size 24、align 8（与迁移前 codegen 实际一致——以迁移前实际 IR 值为准记录）。
  - `UnionLayoutUnchanged`：`union U { int32 i; float64 f; }` → size 8、align 8。
  - `ClassBaseSlotZero`：`class B { public: int32 b; }` + `class D : B { public: float64 v; }` → D：b@0、v@8、size 16（Review Focus：与 P1-04 Focus 4 基线一致）。
- [ ] **Step 2: 确认 RED**（LayoutBuilder 未定义）。
- [ ] **Step 3: 实现**——build() 默认路径复制现有构造语义（字段顺序、基类槽 0、union chunk+padding），DataLayout 自检；三处消费者切换。
- [ ] **Step 4: 全量 `ctest` 绿**（回归 pin 核心：既有全部布局相关测试不变）。
- [ ] **Step 5: Commit**——`refactor(ann): LayoutBuilder 单源化聚合布局（无注解回归 pin）`。

### Task 5: 布局注解 packed/align/repr(C)

**Files:**
- Modify: `src/ast/Type.h`（StructType/UnionType 加 `bool isPacked=false; bool reprC=false; uint64_t forcedAlign=0; std::unordered_map<std::string,uint64_t> fieldAligns;`）
- Modify: `src/sema/SemanticAnalyzer.cpp`（visitStructDeclImpl/visitUnionDeclAST 尾部：注解语义折叠进 Type——`applyAggregateAnnotations(DeclAST&, Type*)` 私有助手）
- Modify: `src/ast/LayoutBuilder.{h,cpp}`（消费 isPacked/forcedAlign/fieldAligns：packed literal、字段前置 padding、尾部补齐）
- Modify: `src/sema/SemanticAnalyzer.cpp`（visit(VarDeclAST) 全局/局部：类型级 align → 代码生成用，见下）与 `src/ast/Decl.cpp`（VarDeclAST::codegen：global `setAlignment(N)` / alloca `setAlignment(N)`；VarDeclAST 加 `uint64_t declAlign=0;` 由 sema 折叠写入）
- Test: `tests/e2e/test_annotations.cpp`（新建）+ `tests/sema/test_annotation_semantics.cpp`（追加）

**Interfaces:**
- Consumes: Task 3 validateAnnotations（先验证后折叠）；Task 4 LayoutBuilder。
- Produces: 语义映射（spec §3 逐字）——packed→isPacked；align(N)→forcedAlign/fieldAligns[name]/VarDeclAST::declAlign；repr(C)→reprC（T4/T5 阶段仅记录 + 校验，无布局分支）。

- [ ] **Step 1: 写失败测试**（e2e 走 runSource 管线 + size_of/offset_of 断言——单源化后 CT 查询即 IR 事实）：
  - `PackedStructE2E`：`[[packed]] struct P { int32 a; int16 b; }` → size_of==6、offset_of(P,"b")==4。
  - `AlignStructE2E`：`[[align(64)]] struct A { int32 x; }` → size_of==64、offset_of==0。
  - `FieldAlignE2E`：`struct F { int8 a; [[align(16)]] int32 b; }` → offset_of(F,"b")==16、size_of==32（GCC 语义）。
  - `PackedAlignComboE2E`：`[[packed]] [[align(8)]] struct PA { int32 a; int16 b; }` → offsets 0/4、size==8。
  - `ReprCPinE2E`：`[[repr(C)]] struct R { int32 a; float64 b; }` → size_of==16、offset_of(R,"b")==8（与默认一致——固化承诺）。
  - `UnionAlignE2E`：`[[align(16)]] union U { int32 i; }` → size_of==16。
  - sema 侧 `VarAlignFoldsToDecl`：`[[align(64)]] int32 g;` → 全局变量 IR alignment==64（经模块检查）。
- [ ] **Step 2: 确认 RED**。
- [ ] **Step 3: 实现**——折叠助手 + LayoutBuilder 三模式（默认/packed/padding）+ VarDecl align。LayoutBuilder padded 模式：目标偏移表算好后插显式 `[k x i8]` padding 字段使自然布局落到目标偏移，DataLayout 自检兜底。
- [ ] **Step 4: 全量绿**（含一致性矩阵——每 e2e 断言即矩阵行）。
- [ ] **Step 5: Commit**——`feat(ann-02/03): repr(C)/packed/align 布局落地（LayoutBuilder 三模式）`。

### Task 6: 函数注解与 deprecated/nonnull 诊断

**Files:**
- Modify: `src/ast/Type.h`（Type 基类加 `bool deprecated=false; std::string deprecatedMsg;`——ctDeadBranch 先例）
- Modify: `src/sema/SemanticAnalyzer.cpp`（deprecated 折叠：visit 各 Decl 时写 Type 侧/查 Symbol 侧）
- Modify: `src/ast/Decl.cpp`（codegenPrototype：`addFnAttr(AlwaysInline/Cold)`、`addParamAttr(NonNull)`——首次引入 LLVM 属性，含 `<llvm/IR/Attributes.h>`）
- Modify: `src/sema/SemanticAnalyzer.cpp`（W3004 四类使用点 + W3005 调用点检查；VarDecl align 折叠已在 T5）
- Test: `tests/sema/test_annotation_semantics.cpp`（追加）+ `tests/e2e/test_annotations.cpp`（追加）

**Interfaces:**
- deprecated 使用点检查实现路径：
  - 类型使用 → Type 侧折叠（`checkCtDeadBranchUse` 同位模式：VarDecl/ArrayDecl/字段/参数/cast 已有检查链，追加 deprecated 检查）
  - 函数调用 → 解析出 FunctionDeclAST 处查 `annotations`（visit(CallExprAST)/visit(MethodCallExprAST) resolve 点）
  - 变量引用 → visit(VariableExprAST) resolve 到 VarDeclAST 处（Scope Symbol 回源）
  - 字段访问 → visit(MemberAccessExprAST) 字段解析处（objType→字段注解经 StructType::fields——**注意**：字段注解在 AST 侧，Type 侧 fieldAligns 先例：sema 折叠时同步写 `StructType` 加 `std::unordered_map<std::string,bool> fieldDeprecated` + msg map）
- nonnull：visit(CallExprAST)/visit(MethodCallExprAST) 实参为字面 nullptr（`IsNullLiteralExpr` 或等价判定——查现有 nullptr 字面表示）或整型常量 0（evaluateConstexpr 折叠为 0）且对应形参挂 nonnull → W3005。
- LLVM 属性索引：非成员函数 param i → arg i；成员函数（含 this）→ arg i+1。

- [ ] **Step 1: 写失败测试**：
  - sema：`DeprecatedFuncW3004`（`[[deprecated]] int32 old()...` + 调用 → warnings 含 `is deprecated`）；`DeprecatedFuncWithMsg`（含 `use new`）；`DeprecatedTypeW3004`（struct 用于 VarDecl）；`DeprecatedVarW3004`；`DeprecatedFieldW3004`（MemberAccess）；`NonNullLiteralNullW3005`（`[[nonnull]] int32 f(int32* p) { return *p; }` + `f(nullptr)` → `null passed to nonnull parameter 'p' of 'f'`）；`NonNullZeroLiteral`（传字面 0）。
  - e2e：`InlineAttrIR`/`ColdAttrIR`/`NonNullAttrIR`——复制 runSource 管线到模块构造，按 mangled 名取 `llvm::Function` 断言 `hasFnAttribute(Attribute::AlwaysInline/Cold)`、参数 `hasAttribute(NonNull)`。
- [ ] **Step 2: 确认 RED**。
- [ ] **Step 3: 实现**（按上）。
- [ ] **Step 4: 全量绿**。
- [ ] **Step 5: Commit**——`feat(ann-04/05): inline/cold/nonnull LLVM 属性与 deprecated/nonnull 诊断`。

### Task 7: 硬化与收口

**Files:**
- Modify: `tests/frontend/test_annotation_parse.cpp`、`tests/sema/test_annotation_semantics.cpp`（硬化用例）
- Modify: `TODO.md`（ANN-01~06→[x]；ANN-09/10→[x] 注记并入 spec；LEX-10→[x]；PAR-07→[x]；SEM-11 W3004 落地注记；TYP-21/MEM-09 布局部分注记；ANN-07/08 挂账注记；P1-05→[x]）
- Modify: `Progress.md`（git add -f）

- [ ] **Step 1: 硬化用例**（先跑，绿则记回归 pin）：
  - `AnnotationOnNonDeclStmt`：`int32 main() { [[inline]] x = 1; }` → 解析错误（注解仅声明位置）。
  - `ReprCOnTypedefE2011`：`[[repr(C)]] typedef int32 T;` → E2011。
  - `ReprCUnionOk`：`[[repr(C)]] union U { int32 i; }` → 合法。
  - `ExternDeprecatedUse`：`[[deprecated]] int32 ext(); int32 main() { return ext(); }` → W3004（extern 声明同样触发）。
- [ ] **Step 2: 全量 `ctest` 绿**。
- [ ] **Step 3: TODO/Progress 收口 + Commit**——`chore(ann): P1-05 收口（TODO 注记与硬化用例）`。

---

## Self-Review 记录

1. **Spec 覆盖**：§2→T1/T2；§3 语义表→T5/T6；§4 单源化→T4/T5；§5 诊断→T3/T6；§6 测试与任务切分→各任务 + T7。无缺口。
2. **Step 扫描**：所有 code step 只含签名/位置/值；LayoutBuilder 算法因签名与测试不决定（padding 构造 + DataLayout 自检）给出职责级描述，非代码转录。
3. **类型一致**：`Annotation/AnnotationArg/FieldInfo/LayoutResult/checkCtDeadBranchUse 式检查链/validateAnnotations/evalConstexprCallCT(P1-04)` 各任务引用一致；`StructType.isPacked/forcedAlign/fieldAligns` 与 LayoutBuilder 消费字段同名。
4. **Review Focus**：5 条均有归属测试（T2 清单+回归、T4 pin、T1 pin、T5 矩阵、T6 用例）。
5. **比例**：计划 ≈ spec 1.4 倍，无代码转录。
