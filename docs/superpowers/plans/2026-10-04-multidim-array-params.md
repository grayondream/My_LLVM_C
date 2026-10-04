# 多维数组形参（行 slice 去糖）Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `T name[][3]` / `T name[2][3]` 形参去糖为 `Slice<int32[3]>`（行 slice），2-D 实参零拷贝传入、函数体内链式下标读写。

**Architecture:** 仅改 Parser `parseParamDecl`——单 `[N]` 消费块扩展为维度链（首维可选、内层显式），从右向左构建 elem `ArrayType` 后 `getSliceType(elem)`。sema/codegen 零改动（spec D4'：conversionRank 严格行比较、退化/mangle/MethodCall resolvedParamTypes 全递归已核实）。

**Tech Stack:** C++20 / LLVM 21 / GoogleTest / CTest

**Spec:** `docs/superpowers/specs/2026-10-04-multidim-array-params-design.md`（D1'–D5'）

## Global Constraints

- 基线 **806/806** 全绿；每任务结束全量 `ctest` 必须绿。
- 测试函数/类型名一律用 `MDP` 前缀（全局 `TypeContext` 单例跨测试泄漏）。
- e2e 复用 `tests/e2e/test_multidim.cpp` 既有 fixture `MultiDimE2E`（`runSource(source, filename)` 返回进程退出码）；该文件已在 CMake GLOB 内，**无需** 重新 `cmake ..`。
- `ArrayType(Type* elem, int size)`（Type.h:66）；`TypeContext::instance().getSliceType(Type*)`（Parser.cpp 已 include 用法同 :2296）。
- `Progress.md`、`docs/superpowers/` 均被 gitignore，提交需 `git add -f`。
- 构建：`cd build && cmake --build . -j$(nproc)`；测试二进制 `./bin/compiler_tests`。

## Review Focus

1. **`name[i][j]` 聚合元素左值再下标链**（Slice<Array> 下标 → `T[3]` 左值 → 再次下标/行退化）——无既有测试覆盖；预期：读写均落回实参存储。→ Task 2 e2e `MultiDimParamRowSumWriteThrough` 钉住。
2. **行类型不匹配静默通过**（`int32[2][4]` 实参 → `[][3]` 形参）——预期编译期拒绝（typesEqual 严格比较）。→ Task 1 `MDPRowTypeMismatchRejected` 钉住。
3. **1-D 去糖回归**（`T name[N]` 仍为 `Slice<T>`，dims.size()==1 路径逐一等价）——预期既有 784 轮套件不回归。→ 既有 `SliceSemTest.MultiDimArrayParamRejected` 等全套件隐式钉住 + 全量 ctest。
4. **内层缺省维度** `name[3][]`——预期解析期拒绝（不再走旧"多维不支持"报错，但 errors 非空不变）。→ Task 1 `MDPNonFirstDimRejected`（显式管线）钉住。
5. **MethodCall 传 2-D 实参给行 slice 方法形参**（resolvedParamTypes 递归）——预期正常匹配调用。→ Task 2 e2e `MultiDimParamMethodCall` 钉住。

---

### Task 1: Parser 维度链去糖 + 5 项 sema 测试

**Files:**
- Modify: `src/frontend/Parser.cpp`（`parseParamDecl` 数组后缀块 :2286–2301）
- Test: `tests/sema/test_semantic_analyzer.cpp`（文件末尾追加）

**Interfaces:**
- Consumes: `TypeContext::instance().getSliceType(Type*)`、`ArrayType(Type*, int)`、`errorUnexpected(...)`（Parser 既有用法）。
- Produces: `T name[][K]` / `T name[N][K]` 形参解析为 `SliceType(ArrayType(T, K))`；Task 2 e2e 消费此语义。

- [ ] **Step 1: 写 5 个 sema 测试（RED 先行）**

```cpp
// TYP-11: multi-dimensional array parameters desugar to row slices.
// Spec: docs/superpowers/specs/2026-10-04-multidim-array-params-design.md

// D1': f(a) with a: int32[2][3], param int32 m[][3].
TEST(SliceSemTest, MDP2DParamAccepted) {
    EXPECT_TRUE(analyzeOk(
        "int32 MDPF(int32 m[][3]) { return m[1][2]; } "
        "int32 main() { int32 a[2][3] = {{1,2,3},{4,5,6}}; return MDPF(a); }"));
}

// D3': row type mismatch is rejected at compile time.
TEST(SliceSemTest, MDPRowTypeMismatchRejected) {
    EXPECT_FALSE(analyzeOk(
        "int32 MDPF(int32 m[][3]) { return m[0][0]; } "
        "int32 main() { int32 b[2][4] = {{1,2,3,4},{5,6,7,8}}; return MDPF(b); }"));
}

// D2': only the first dimension may be empty — pin the parse level
// explicitly (analyzeOk ignores parse errors).
TEST(SliceSemTest, MDPNonFirstDimRejected) {
    Lexer lexer("test.c",
        "int32 MDPF(int32 m[3][]) { return 0; } int32 main() { return 0; }");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_TRUE(ast != nullptr);
    EXPECT_FALSE(parser.getErrors().empty());
}

// D2': explicit first dimension is documentation, identical to [].
TEST(SliceSemTest, MDPExplicitFirstDimAccepted) {
    EXPECT_TRUE(analyzeOk(
        "int32 MDPG(int32 m[2][3]) { return m[1][2]; } "
        "int32 main() { int32 a[2][3] = {{1,2,3},{4,5,6}}; return MDPG(a); }"));
}

// D1': chained subscript inside the callee body.
TEST(SliceSemTest, MDPParamSubscript) {
    EXPECT_TRUE(analyzeOk(
        "int32 MDPGet(int32 m[][3]) { return m[1][2] + m[0][0]; } "
        "int32 main() { int32 a[2][3] = {{1,2,3},{4,5,6}}; return MDPGet(a); }"));
}
```

- [ ] **Step 2: 运行验证 RED 分布**

Run: `cd build && cmake --build . -j$(nproc) && ./bin/compiler_tests --gtest_filter='SliceSemTest.MDP*' 2>&1 | grep -E '\[ *(OK|FAILED)'`
Expected: `MDP2DParamAccepted` / `MDPExplicitFirstDimAccepted` / `MDPParamSubscript` FAIL；`MDPRowTypeMismatchRejected` / `MDPNonFirstDimRejected` PASS（pin——现状 parse 报"multi-dimensional ... not supported"已使 analyzeOk=false / errors 非空）。RED 分布偏差属正常（parse 错误恢复路径），记录到 ledger，不改测试。

- [ ] **Step 3: 实现 `parseParamDecl` 维度链去糖**

替换 `src/frontend/Parser.cpp` :2286–2301 的单 `[N]` 块（保留注释精神，更新 TYP-11 引用为本 spec）：

```cpp
if (check(TokenType::TOKEN_LBRACKET)) {
    std::vector<int> dims;
    std::vector<bool> explicitDim;
    while (check(TokenType::TOKEN_LBRACKET)) {
        advance();
        int dimSize = 0;
        bool hasNumber = false;
        if (auto numTok = match(TokenType::TOKEN_NUMBER)) {
            dimSize = static_cast<int>(std::get<long long>(numTok->value));
            hasNumber = true;
        }
        if (!expect(TokenType::TOKEN_RBRACKET, "expected ']' after array parameter size")) {
            return nullptr;
        }
        dims.push_back(dimSize);      // 首维数值仅文档性，内维数值用于行类型
        explicitDim.push_back(hasNumber);
    }
    for (size_t i = 1; i < dims.size(); ++i) {
        if (!explicitDim[i]) {
            errorUnexpected("expected array dimension: only the first dimension may be empty");
            return nullptr;
        }
    }
    // Right-to-left: `T name[][3]` desugars to Slice<ArrayType(T, 3)>.
    Type* elemType = type;
    for (size_t i = dims.size() - 1; i >= 1; --i) {
        elemType = new ArrayType(elemType, dims[i]);
    }
    type = TypeContext::instance().getSliceType(elemType);
}
```

删除旧尾部 `if (check(TOKEN_LBRACKET)) { errorUnexpected("multi-dimensional array parameters are not supported"); ... }` 分支。1-D（dims.size()==1）时构建循环体不执行，`getSliceType(type)` 与旧码逐一等价。同时更新该块上方注释：TYP-11 引用改指本 spec（2026-10-04-multidim-array-params-design.md）。

- [ ] **Step 4: 全量测试**

Run: `cd build && cmake --build . -j$(nproc) && ctest 2>&1 | grep -E 'tests passed|tests failed'`
Expected: **811/811**（806 + 5）。

- [ ] **Step 5: Commit**

```bash
git add src/frontend/Parser.cpp tests/sema/test_semantic_analyzer.cpp
git commit -m "feat(TYP-11): 多维数组形参去糖为行 slice（首维文档性，内维显式）"
```

---

### Task 2: e2e 3 项（写穿透 / MethodCall / len）

**Files:**
- Test: `tests/e2e/test_multidim.cpp`（文件末尾追加；fixture `MultiDimE2E` 既有）

**Interfaces:**
- Consumes: Task 1 的行 slice 形参语义；`MultiDimE2E.runSource(source, filename)`。

- [ ] **Step 1: 写 3 个 e2e 测试（预期直接 PASS，D4' 零改动验证）**

```cpp
// Review Focus 1: aggregate-element lvalue re-subscript + zero-copy
// write-through to the caller's storage.
TEST_F(MultiDimE2E, MultiDimParamRowSumWriteThrough) {
    EXPECT_EQ(runSource(R"(
        int32 MDSum(int32 m[][3]) {
            m[1][0] = 100;
            int32 s = 0;
            for (int32 i = 0; i < m.len; i = i + 1)
                for (int32 j = 0; j < 3; j = j + 1)
                    s = s + m[i][j];
            return s;
        }
        int32 main() {
            int32 a[2][3] = {{1, 2, 3}, {4, 5, 6}};
            int32 r = MDSum(a);
            return (r == 117 && a[1][0] == 100 && a[0][0] == 1) ? 0 : 1;
        }
    )", "test_mdp_writethrough.c"), 0);
}

// Review Focus 5: MethodCall with a 2-D array argument to a row-slice param.
TEST_F(MultiDimE2E, MultiDimParamMethodCall) {
    EXPECT_EQ(runSource(R"(
        class MDPMat {
            public:
            int32 rowSum(int32 m[][3]) { return m[1][2]; }
        };
        int32 main() {
            MDPMat obj;
            int32 a[2][3] = {{1, 2, 3}, {4, 5, 6}};
            return obj.rowSum(a) == 6 ? 0 : 1;
        }
    )", "test_mdp_method.c"), 0);
}

// D1': slice length on a row-slice param = row count.
TEST_F(MultiDimE2E, MultiDimParamLen) {
    EXPECT_EQ(runSource(R"(
        int32 MDRows(int32 m[][3]) { return m.len; }
        int32 main() {
            int32 a[2][3] = {{1, 2, 3}, {4, 5, 6}};
            return MDRows(a) == 2 ? 0 : 1;
        }
    )", "test_mdp_len.c"), 0);
}
```

注意：`MDSum` 先写穿透 `m[1][0]=100` 再求和——原总和 21，`a[1][0]` 由 4 改为 100，故 `r == 117`（已在测试代码中核算完毕）。

- [ ] **Step 2: 运行验证**

Run: `cd build && cmake --build . -j$(nproc) && ./bin/compiler_tests --gtest_filter='MultiDimE2E.MultiDimParam*' 2>&1 | grep -E '\[ *(OK|FAILED)'`
Expected: 3 项全 PASS（若 FAIL——D4' 零改动主张破产，停下升级路径并回报，勿顺手改 sema/codegen）。

- [ ] **Step 3: 全量测试**

Run: `ctest 2>&1 | grep -E 'tests passed|tests failed'`
Expected: **814/814**。

- [ ] **Step 4: Commit**

```bash
git add tests/e2e/test_multidim.cpp
git commit -m "test(TYP-11): 多维形参 e2e——写穿透/MethodCall/len 钉住"
```

---

### Task 3: 收尾——TODO/Progress/评审

**Files:**
- Modify: `TODO.md`（TYP-11 行 :131）
- Modify: `Progress.md`（追加）
- Review: 整分支 subagent 评审

**Interfaces:**
- Consumes: Task 1/2 提交；review-package 脚本路径 `/home/ares/.cache/opencode/npm/git-superpowers-0958a5557860/1790514376069/node_modules/superpowers/skills/subagent-driven-development/scripts/review-package`。

- [ ] **Step 1: 更新 TODO.md TYP-11 行**

"待补：多维形参、VLA。" → "待补：VLA。多维形参已完成（2026-10-04，spec docs/superpowers/specs/2026-10-04-multidim-array-params-design.md——行 slice 去糖）。"

- [ ] **Step 2: 追加 Progress.md 条目**（流程、冻结决策 D1'–D5'、测试数、遗留 VLA），`git add -f` 两文件并提交。

- [ ] **Step 3: 生成评审包并派发 subagent 整分支评审**

Run: `review-package docs/superpowers/plans/2026-10-04-multidim-array-params.md <spec-commit> HEAD`（spec-commit=`6f6acbe`），对照 Review Focus 五条 + spec D1'–D5'，输出 Critical/Important/Minor。

- [ ] **Step 4: 修复评审发现（TDD：先测试后实现），全量 ctest 绿后提交，记录 Rulings 到 ledger。**
