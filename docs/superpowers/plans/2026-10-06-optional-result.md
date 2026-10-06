# P1-02 Optional / Result 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Optional/Result 作为内建魔术类型端到端可用：`T?`/`Optional<T>`/`Result<T,E>` 语法、严格类型相等、显式伪字段访问、聚合布局与初始化、codegen 全通路。

**Architecture:** 沿用 Slice 先例——编译器硬编码多态类型，`TypeContext` 单例缓存；parser 仅在类型位置特判 `<...>` 实参表；sema 严格化类型相等；codegen 接入现有 struct 聚合路径（GEP/ExtractValue）。

**Tech Stack:** C++20、LLVM IR builder、GTest（`compiler_tests` 单一二进制，`ctest` 驱动）。

**Spec:** `docs/superpowers/specs/2026-10-06-optional-result-design.md`（DS1-DS4 裁决：内建载体 / 类型位置尖括号 / C 语义自由访问 / `{bool valid; T value;}` + `{bool ok; T value; E error;}`）。

## Global Constraints

- 基线 902/902 全绿（`cd build && ctest`）；每任务结束全量必须绿。
- TypeContext 全局单例跨测试共享：sema 测试名前缀 `OPT`/`RES`，e2e fixture 名 `OptionalResultE2E`，layout 测试名前缀 `OptResLayout`。
- 布局冻结（DS4）：`T? = {i1 valid, T value}`（**valid 在前**）；`Result<T,E> = {i1 ok, T value, E error}`。
- Mangle 沿用既有 `typeToMangled`：`<T>opt` / `<T>res<E>`，不改。
- NG-05：表达式中的 `?` 永远不是传播算子；不新增 `?` 相关表达式语法。
- TDD：先写测试亲见 RED，再实现；未亲见失败不写实现。
- 局部未初始化的 Optional/Result 跟随 struct 先例（alloca 不零初始化，P0-03 照常覆盖）；全局无初始化器 = 零初始化定义（`valid/ok=false`）。
- 构建命令：`cd build && cmake --build . -j$(nproc)`；测试过滤示例 `./bin/compiler_tests --gtest_filter='OPT*'`。
- 每任务提交一次；`Progress.md` 与 `docs/superpowers/` 需 `git add -f`。

## Review Focus

- 不同类型实参的同 kind 混用（`int32?` vs `float64?`、异参 Result）——现泛化规则静默放行，须严格拒绝（Task 2 钉）。
- LLVM 结构体命名冲突：不同 T 的 Optional 若共享 `Optional` 名会撞 LLVM 具名结构体——须唯一命名（Task 3 钉）。
- rvalue Optional（函数返回值直接取 `.value`）——ExtractValue 路径与 lvalue GEP 路径都要对（Task 5 钉）。
- 初始化列表元素数/类型错配（`{true}`、`{true, 5, 6}`、`{5, true}` 顺序反）——sema 校验拒绝（Task 4 钉）。
- 全局 `Result<int32,int32> g;` 零初始化后 `g.ok` 必须为 false 而非垃圾（Task 5 钉）。

---

### Task 1: Parser 语法——`Optional<T>` 与 `Result<T,E>`

**Files:**
- Modify: `src/frontend/Parser.cpp`（`parseBaseType` 的 `TOKEN_IDENTIFIER` 分支，约 :1669-1680）
- Test: `tests/sema/test_semantic_analyzer.cpp`（追加，前缀 `OPT`/`RES`）

**Interfaces:**
- Consumes: `TypeContext::getOptionalType(Type*)`、`TypeContext::getResultType(Type*, Type*)`（已存在，`src/ast/Type.cpp:226-233`）。
- Produces: 类型位置可解析 `Optional<T>`、`Result<T,E>`、`T?`（既有）、嵌套实参；后续任务的测试统一用此语法。

- [ ] **Step 1: Write the failing test**

```cpp
TEST(SliceSemTest, OptResParseNamedOptional) {
    // `Optional<T>` 显式形式必须与 `T?` 同型（spec §4.1）。
    EXPECT_TRUE(analyzeOk("int32 main() { Optional<int32> o; return 0; }"));
}
TEST(SliceSemTest, OptResParseResult) {
    EXPECT_TRUE(analyzeOk(
        "Result<int32, int32> make() { return {false, 0, 1}; } "
        "int32 main() { Result<int32, int32> r = make(); return 0; }"));
}
TEST(SliceSemTest, OptResParseNestedArgs) {
    EXPECT_TRUE(analyzeOk(
        "int32 main() { Result<int32, int32?> r; Optional<Optional<int32>> o; return 0; }"));
}
```

（`{false, 0, 1}` 初始化校验在 Task 4 才实现；本任务红点在 **parse 失败**——今天 `Optional<`/`Result<` 无解析路径，`parseType` 返回 nullptr 报错。）

- [ ] **Step 2: Run test to verify it fails**

Run: `./bin/compiler_tests --gtest_filter='SliceSemTest.OptResParse*'`
Expected: 3 个 FAIL（解析错误）。

- [ ] **Step 3: Implement in `parseBaseType` 的 `TOKEN_IDENTIFIER` 分支**

在 `lookupNamedType(name)` 探测**之前**特判：若 `name == "Optional"` 且下一 token 为 `TOKEN_LESS`，消耗 `>` 前后解析 `parseType()` 得 elem，要求 `TOKEN_GREATER` 收口，返回 `TypeContext::instance().getOptionalType(elem)`；`name == "Result"` 同理解析两个 `parseType()`（中间要求 `TOKEN_COMMA`），返回 `getResultType(success, error)`。不匹配尖括号时维持原路径（`lookupNamedType`）。`parseType()` 返回 nullptr 或收口 token 缺失 → `errorUnexpected` 后返回 nullptr。注意 `<` 实参表内 `parseType` 递归天然支持嵌套（`Optional<Optional<int32>>` 的 `>>` 需确认 lexer 是否拆分——若 `>>` 为单 token 需在收口处拆分处理）。

- [ ] **Step 4: Run test to verify it passes**

Run: `./bin/compiler_tests --gtest_filter='SliceSemTest.OptResParse*'`
Expected: 3 PASS。

- [ ] **Step 5: 全量 ctest 绿后提交**

```bash
cd build && ctest   # 902+3 全绿
git add src/frontend/Parser.cpp tests/sema/test_semantic_analyzer.cpp
git commit -m "feat(typ13/14): Optional<T>/Result<T,E> 类型位置语法（内建魔术类型）"
```

---

### Task 2: 类型相等/赋值严格化

**Files:**
- Modify: `src/ast/Symbol.cpp`（`typesEqual`，`TypeKind::Slice` case 后、`default:` 前加 `Optional`/`Result` case）
- Modify: `src/sema/SemanticAnalyzer.cpp`（`typesCompatible` :260 的同 kind 泛化规则前；`checkAssignmentTypes` :466 的 `lhsS->kind == rhsS->kind` 泛化规则前）
- Test: `tests/sema/test_semantic_analyzer.cpp`（追加）

**Interfaces:**
- Consumes: Task 1 的 `Result<T,E>` 语法；`OptionalType::elementType`、`ResultType::successType/errorType`（`src/ast/Type.h:156-168`）。
- Produces: `typesEqual`/`typesCompatible`/`checkAssignmentTypes` 对 Optional/Result 按实参严格比较——后续任务的传参/返回/赋值校验全部依赖此语义。

- [ ] **Step 1: Write the failing test**

```cpp
TEST(SliceSemTest, OptMismatchRejected) {
    // 同 kind 泛化规则今天会误放行：Optional<int32> ≠ Optional<float64>。
    EXPECT_FALSE(analyzeOk(
        "int32 main() { int32? a = {true, 5}; float64? b = a; return 0; }"));
}
TEST(SliceSemTest, ResMismatchRejected) {
    EXPECT_FALSE(analyzeOk(
        "int32 main() { Result<int32, int32> a; Result<int32, float64> b = a; return 0; }"));
}
TEST(SliceSemTest, OptToPlainRejected) {
    // T? ↔ T 无隐式转换（spec §4.3）。
    EXPECT_FALSE(analyzeOk(
        "int32 main() { int32? a = {true, 5}; int32 b = a; return 0; }"));
}
TEST(SliceSemTest, OptNominalEqualPin) {
    EXPECT_TRUE(analyzeOk(
        "int32 main() { int32? a = {true, 5}; int32? b = a; b.value = 6; return b.value; }"));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `./bin/compiler_tests --gtest_filter='SliceSemTest.OptMismatchRejected:SliceSemTest.ResMismatchRejected:SliceSemTest.OptToPlainRejected:SliceSemTest.OptNominalEqualPin'`
Expected: 前 3 个 FAIL（静默放行），pin PASS。

- [ ] **Step 3: Implement 严格分支**

`typesEqual`（Symbol.cpp）加：
```cpp
case TypeKind::Optional: {
    auto* oa = static_cast<OptionalType*>(a);
    auto* ob = static_cast<OptionalType*>(b);
    return typesEqual(oa->elementType, ob->elementType);
}
case TypeKind::Result: {
    auto* ra = static_cast<ResultType*>(a);
    auto* rb = static_cast<ResultType*>(b);
    return typesEqual(ra->successType, rb->successType) &&
           typesEqual(ra->errorType, rb->errorType);
}
```
`typesCompatible` 与 `checkAssignmentTypes`：在各自同 kind 泛化规则**之前**，对 `Optional`/`Result` kind 改为委托 `typesEqual(left, right)`（strip typedef 后比较），不等则走各自原有报错路径。

- [ ] **Step 4: Run test to verify it passes**

Run: 同 Step 2 过滤器。Expected: 4 PASS。

- [ ] **Step 5: 全量 ctest 绿后提交**

```bash
cd build && ctest
git add src/ast/Symbol.cpp src/sema/SemanticAnalyzer.cpp tests/sema/test_semantic_analyzer.cpp
git commit -m "feat(typ13/14): Optional/Result 类型相等按实参严格化（typesEqual/compatible/assignment）"
```

---

### Task 3: getLLVMType 布局修正 + 唯一命名

**Files:**
- Modify: `src/codegen/CodegenContext.cpp:512-529`（`TypeKind::Optional`/`TypeKind::Result` 分支）
- Test: `tests/codegen/test_optional_result.cpp`（新建，仿 `test_codegen_context.cpp` 的 include 与 fixture 模式）

**Interfaces:**
- Consumes: `typeToMangled(Type*)`（`src/ast/Mangle.h`，已导出）。
- Produces: LLVM 布局冻结 `{i1, T}` / `{i1, T, E}`，具名结构体 `Optional_<T-mangled>` / `Result_<T-mangled>_<E-mangled>`——Task 5 的 GEP 索引（valid/ok=0, value=1, error=2）依赖此布局。

- [ ] **Step 1: Write the failing test**

```cpp
TEST(OptResLayoutTest, OptionalIsFlagFirst) {
    // DS4：{i1 valid, T value}——valid 在前；现状 {T, i1} 且误转换 SliceType。
    CodegenContext ctx;
    Type* t = TypeContext::instance().getOptionalType(
        TypeContext::instance().getInt32());
    auto* st = llvm::cast<llvm::StructType>(ctx.getLLVMType(t));
    EXPECT_EQ(st->getNumElements(), 2u);
    EXPECT_EQ(st->getElementType(0), llvm::Type::getInt1Ty(ctx.getContext()));
    EXPECT_EQ(st->getElementType(1), llvm::Type::getInt32Ty(ctx.getContext()));
}
TEST(OptResLayoutTest, ResultIsFlagFirstTriple) {
    CodegenContext ctx;
    Type* t = TypeContext::instance().getResultType(
        TypeContext::instance().getInt32(),
        TypeContext::instance().getInt64());
    auto* st = llvm::cast<llvm::StructType>(ctx.getLLVMType(t));
    EXPECT_EQ(st->getNumElements(), 3u);
    EXPECT_EQ(st->getElementType(0), llvm::Type::getInt1Ty(ctx.getContext()));
    EXPECT_EQ(st->getElementType(1), llvm::Type::getInt32Ty(ctx.getContext()));
    EXPECT_EQ(st->getElementType(2), llvm::Type::getInt64Ty(ctx.getContext()));
}
TEST(OptResLayoutTest, DistinctInstancesHaveDistinctNames) {
    // 不同 T 不得共享 LLVM 具名结构体（Review Focus #2）。
    CodegenContext ctx;
    Type* a = TypeContext::instance().getOptionalType(TypeContext::instance().getInt32());
    Type* b = TypeContext::instance().getOptionalType(TypeContext::instance().getFloat64());
    EXPECT_NE(llvm::cast<llvm::StructType>(ctx.getLLVMType(a))->getName(),
              llvm::cast<llvm::StructType>(ctx.getLLVMType(b))->getName());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `./bin/compiler_tests --gtest_filter='OptResLayoutTest.*'`
Expected: 3 FAIL（现状 `{T,i1}` 顺序 + 误转换 bug；Result 两字段；同名冲突）。

- [ ] **Step 3: Implement**

`CodegenContext.cpp` 两分支重写：Optional → `{getInt1Ty, getLLVMType(elementType)}`；Result → `{getInt1Ty, getLLVMType(successType), getLLVMType(errorType)}`；正确 `static_cast<OptionalType*>/ResultType*>`（修误转换）；`llvm::StructType::create(*context, fieldTypes, "Optional_" + typeToMangled(elementType))`（Result 名 `"Result_" + typeToMangled(successType) + "_" + typeToMangled(errorType)`），include `ast/Mangle.h`。

- [ ] **Step 4: Run test to verify it passes**

Run: `./bin/compiler_tests --gtest_filter='OptResLayoutTest.*'` → 3 PASS。

- [ ] **Step 5: 全量 ctest 绿后提交**

```bash
cd build && ctest
git add src/codegen/CodegenContext.cpp tests/codegen/test_optional_result.cpp
git commit -m "fix(typ13/14): Optional/Result 布局修正——{i1,T}/{i1,T,E} + SliceType 误转换 + 唯一命名"
```

---

### Task 4: sema 成员访问 + 初始化列表校验

**Files:**
- Modify: `src/sema/SemanticAnalyzer.cpp`（`visit(MemberAccessExprAST)` :1392 的 `.` 路径；`visit(VarDeclAST)` :1746 的 init-list 分支）
- Test: `tests/sema/test_semantic_analyzer.cpp`（追加）

**Interfaces:**
- Consumes: `OptionalType/ResultType` 字段名冻结（spec §4.3）：Optional `.valid`(bool)/`.value`(T)；Result `.ok`(bool)/`.value`(T)/`.error`(E)。
- Produces: 成员访问表达式带正确 type 与 `isLValue=true`（Task 5 codegen 依赖可写语义）；VarDecl 聚合初始化元素数/类型校验。

- [ ] **Step 1: Write the failing test**

```cpp
TEST(SliceSemTest, OptMemberFieldTypes) {
    EXPECT_TRUE(analyzeOk(
        "int32 main() { int32? o = {true, 5}; bool v = o.valid; int32 x = o.value; return x; }"));
}
TEST(SliceSemTest, OptUnknownMemberRejected) {
    EXPECT_FALSE(analyzeOk(
        "int32 main() { int32? o = {true, 5}; int32 x = o.nope; return x; }"));
}
TEST(SliceSemTest, ResMemberFieldTypes) {
    EXPECT_TRUE(analyzeOk(
        "int32 main() { Result<int32, int32> r = {false, 0, 1}; bool ok = r.ok; "
        "int32 v = r.value; int32 e = r.error; return e; }"));
}
TEST(SliceSemTest, ResUnknownMemberRejected) {
    EXPECT_FALSE(analyzeOk(
        "int32 main() { Result<int32, int32> r; int32 x = r.valid; return x; }"));
}
TEST(SliceSemTest, OptInitListArityRejected) {
    EXPECT_FALSE(analyzeOk(
        "int32 main() { int32? o = {true}; return 0; }"));
}
TEST(SliceSemTest, OptInitListOrderRejected) {
    EXPECT_FALSE(analyzeOk(
        "int32 main() { int32? o = {5, true}; return 0; }"));
}
TEST(SliceSemTest, ResInitListArityRejected) {
    EXPECT_FALSE(analyzeOk(
        "int32 main() { Result<int32, int32> r = {false, 0}; return 0; }"));
}
TEST(SliceSemTest, OptMemberWritablePin) {
    EXPECT_TRUE(analyzeOk(
        "int32 main() { int32? o = {true, 5}; o.value = 6; o.valid = false; return 0; }"));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `./bin/compiler_tests --gtest_filter='SliceSemTest.OptMember*:SliceSemTest.ResMember*:SliceSemTest.OptInitList*:SliceSemTest.ResInitList*'`
Expected: 字段类型/校验各 FAIL（今天成员访问报 "requires struct/class/union"，初始化列表无校验）；WritablePin FAIL。

- [ ] **Step 3: Implement**

`visit(MemberAccessExprAST)` 的 `.`（非 arrow）路径、struct/class/union 检查**之前**加分支（镜像 Slice `.len` 先例）：
- `Optional`：`"valid"` → `getBool()`；`"value"` → `elementType`；其余 `emitError("no member named '...' in Optional")`。
- `Result`：`"ok"` → `getBool()`；`"value"` → `successType`；`"error"` → `errorType`；其余同上报错。
- `node.isLValue = true`（可写，与字段语义一致）。

`visit(VarDeclAST)` init-list 分支：strip typedef 后若 `Optional` 要求 `initializers.size() == 2` 且元素 0 与 `bool`、元素 1 与 `elementType` `typesCompatible`；`Result` 要求 `size() == 3` 且 bool/T/E。不满足 `emitError("initializer list for '...' has wrong ...")`（沿用现有 type mismatch 文案风格）。

- [ ] **Step 4: Run test to verify it passes**

Run: 同 Step 2 过滤器 → 8 PASS。

- [ ] **Step 5: 全量 ctest 绿后提交**

```bash
cd build && ctest
git add src/sema/SemanticAnalyzer.cpp tests/sema/test_semantic_analyzer.cpp
git commit -m "feat(typ13/14): Optional/Result 伪字段访问与初始化列表校验（sema）"
```

---

### Task 5: codegen 成员访问 + 聚合初始化 + e2e 全通路

**Files:**
- Modify: `src/ast/Expr.cpp`（`MemberAccessExprAST::codegen` :952，Slice 分支后加 Optional/Result 分支）
- Modify: `src/ast/Decl.cpp`（`emitAggregateInitializer` :43 与 `buildAggregateConstant` :121 加 Optional/Result case；`VarDeclAST::codegen` :269 零初始化条件加 `Optional||Result`）
- Test: `tests/e2e/test_optional_result.cpp`（新建，fixture 仿 `ClassCodegenE2E` 的 `runSource` 模式）

**Interfaces:**
- Consumes: Task 3 布局索引（valid/ok=0, value=1, error=2）；Task 4 伪字段 type/isLValue。
- Produces: Optional/Result 端到端可执行；e2e fixture `OptionalResultE2E::runSource`（完整 JIT 执行返回 main 返回值）。

- [ ] **Step 1: Write the failing test（e2e）**

```cpp
TEST_F(OptionalResultE2E, OptionalBranchExec) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            int32? o = {true, 42};
            if (o.valid) { return o.value; }
            return -1;
        }
    )", "optres1.c"), 42);
}
TEST_F(OptionalResultE2E, OptionalRvalueMember) {
    // Review Focus #3：函数返回的 rvalue Optional 直接取 .value。
    EXPECT_EQ(runSource(R"(
        int32? make(bool v) { return {v, 7}; }
        int32 main() { return make(true).value; }
    )", "optres2.c"), 7);
}
TEST_F(OptionalResultE2E, ResultErrorPath) {
    EXPECT_EQ(runSource(R"(
        Result<int32, int32> divide(int32 a, int32 b) {
            if (b == 0) { return {false, 0, -1}; }
            return {true, a / b, 0};
        }
        int32 main() {
            Result<int32, int32> r = divide(10, 2);
            if (r.ok) { return r.value; }
            return r.error;
        }
    )", "optres3.c"), 5);
}
TEST_F(OptionalResultE2E, ResultErrorValue) {
    EXPECT_EQ(runSource(R"(
        Result<int32, int32> divide(int32 a, int32 b) {
            if (b == 0) { return {false, 0, -1}; }
            return {true, a / b, 0};
        }
        int32 main() {
            Result<int32, int32> r = divide(10, 0);
            if (r.ok) { return r.value; }
            return r.error;
        }
    )", "optres4.c"), -1);
}
TEST_F(OptionalResultE2E, OptionalParamAndCopy) {
    EXPECT_EQ(runSource(R"(
        int32 pick(int32? o) { if (o.valid) { return o.value; } return 0; }
        int32 main() {
            int32? a = {true, 11};
            int32? b = a;
            b.value = 13;
            return pick(a) * 100 + pick(b);
        }
    )", "optres5.c"), 1113);
}
TEST_F(OptionalResultE2E, GlobalZeroInit) {
    // Review Focus #5：全局零初始化 valid=false。
    EXPECT_EQ(runSource(R"(
        int32? g;
        Result<int32, int32> gr;
        int32 main() {
            if (g.valid || gr.ok) { return -1; }
            return 1;
        }
    )", "optres6.c"), 1);
}
TEST_F(OptionalResultE2E, NestedOptional) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            int32?? o = {true, {false, 0}};
            if (o.valid) { if (o.value.valid) { return o.value.value; } return 2; }
            return 3;
        }
    )", "optres7.c"), 2);
}
```

（NestedOptional 的内层 `{false, 0}` 依赖 Task 4 的嵌套列表校验放行；若 Task 4 校验仅按顶层类型比较，内层嵌套列表按 `emitAggregateInitializer` 递归处理。）

- [ ] **Step 2: Run test to verify it fails**

Run: `./bin/compiler_tests --gtest_filter='OptionalResultE2E.*'`
Expected: 全 FAIL（成员访问无 codegen 分支返回 nullptr / 布局/初始化错位）。

- [ ] **Step 3: Implement**

`MemberAccessExprAST::codegen`：Slice 分支后加——strip typedef 后 `valType->kind == Optional`：`"valid"` → idx 0（`i1`→bool 需 zext 到 i8/比较，按 ctx 现有 bool 处理惯例）；`"value"` → idx 1。`Result`：`"ok"` 0、`"value"` 1、`"error"` 2。`object->isLValue` → `CreateStructGEP`（返回地址，可写）；否则先 load 整值再 `CreateExtractValue`（rvalue）。镜像既有 union/struct 分支写法。

`emitAggregateInitializer`/`buildAggregateConstant`：各加 `TypeKind::Optional`（2 字段：i1 常量 `ConstantInt::getBool` + elem）与 `TypeKind::Result`（3 字段）case，GEP idx 按 Task 3 布局；`buildAggregateConstant` 内 i1 字段用 `llvm::ConstantInt::get(getInt1Ty, v)`。`VarDeclAST::codegen` 局部 init-list 的"先整体置零"条件 `(Struct||Class||Union)` 扩为含 `Optional||Result`。

- [ ] **Step 4: Run test to verify it passes**

Run: `./bin/compiler_tests --gtest_filter='OptionalResultE2E.*'` → 7 PASS。

- [ ] **Step 5: 全量 ctest 绿后提交**

```bash
cd build && ctest   # 预计 902 + 3 + 4 + 3 + 8 + 7 = 927
git add src/ast/Expr.cpp src/ast/Decl.cpp tests/e2e/test_optional_result.cpp
git commit -m "feat(typ13/14): Optional/Result codegen 全通路——伪字段/聚合初始化/拷贝/传参返回（e2e）"
```

---

### Task 6: 文档同步 + TODO 收口

**Files:**
- Modify: `docs/spec/abi.md`（:27-28 两行布局 + §97 核对 mangle）
- Modify: `docs/spec/conversions.md`（§4 Optional/Result 行标注无隐式转换）
- Modify: `docs/spec/stdlib.md`（STD-02 由语言内建承载的说明）
- Modify: `TODO.md`（TYP-13/14、STD-02 勾选 + 摘要；DEC-03 记录 DS4 裁决；P1-02 勾选）
- Modify: `Progress.md`（`git add -f`）

**Interfaces:** 无代码接口；引用 spec `docs/superpowers/specs/2026-10-06-optional-result-design.md` 与本计划。

- [ ] **Step 1: 按上述文件同步文档**——布局以 Task 3 实测为准（`{i1,T}`/`{i1,T,E}`、唯一命名），语义以 spec §4.3 为准；TODO 各条目附一句完成摘要与测试数。
- [ ] **Step 2: 全量 `ctest` 最终验证**——Expected: 全绿（927 前后，以实际为准），无回归。
- [ ] **Step 3: Commit**

```bash
git add docs/spec/abi.md docs/spec/conversions.md docs/spec/stdlib.md TODO.md
git add -f Progress.md docs/superpowers/plans/2026-10-06-optional-result.md
git commit -m "docs(p1-02): Optional/Result 收口——abi/conversions/stdlib/TODO/DEC-03 同步"
```

---

## Self-Review 结论

- **Spec coverage**：§4.1→Task 1；§4.2→Task 3；§4.3→Task 2/4；§4.4→Task 5；§4.5→Task 6；§5 测试→各任务；§6 范围外未引入。无缺口。
- **Step scan**：各步骤单一动作；实现步骤给落点+镜像先例（union/struct/Slice 分支），未替写函数体。
- **Type consistency**：`getOptionalType/getResultType/typeToMangled` 与现有头文件签名一致；GEP 索引（0/1/2）在 Task 3/5 间一致。
- **Review Focus**：5 项均有归属任务测试（#1→Task 2，#2→Task 3，#3/#4/#5→Task 4/5）。
- **Proportion**：计划 ≈ spec 体量，无 transcript。
