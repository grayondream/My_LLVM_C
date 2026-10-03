# 多维数组本体 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `T a[2][3]` 多维固定数组本体——声明（局部/全局/成员）、嵌套初始化、链式下标读写、内层一维 slice 退化、sizeof。

**Architecture:** 嵌套 ArrayType 表示（`[2 x [3 x i32]]`）；只改 Parser 两处声明解析（维度链），sema/codegen/mangle 依赖既有递归通路零改动。首维推断复用 sema 既有 `size == 0` 逻辑。

**Tech Stack:** C++20 / LLVM / gtest（与仓库一致）。

**Spec:** `docs/superpowers/specs/2026-10-03-multidim-arrays-design.md`（D1–D5 冻结决策，`6652f2f`）

## Global Constraints

- 基线 **793/793**（`ctest`，01f2258）；每任务结束全量必须绿。
- D1：嵌套 ArrayType，**维度从右向左构建**——`int32 a[2][3]` = `ArrayType{ArrayType{T,3}, 2}`（外层 2）。左到右链式会建成 `[3][2]`（C 语义反转），这是本计划最大陷阱。
- D2：`parseVariableDecl`（Parser.cpp:2221-2237）收集 dims 后从右向左构建 elementType，最外层交给 `ArrayDeclAST(name, elemType, dims[0], init)`（sema :1733 现有 wrap 零改动）；`parseMemberArraySuffix`（Parser.cpp:2456-2466）同样收集 dims 从右向左构建后返回最外层（5 个调用点 struct/class/union/namespace 自动生效）。
- D3：首维缺省（`a[][3]`）→ `dims[0] = 0` → sema 既有推断（:1724-1728）取 initList 行数。
- D4：codegen 零改动（GEP/`emitAggregateInitializer`/`buildAggregateConstant`/`sizeof` 均已递归，spec §1 表）。
- D5：整体多维→slice 不放行、悬垂 return 自动覆盖——不写任何新守卫代码。
- 范围外（不做）：多维形参、VLA、数组赋值、行级长度校验。
- 测试名/类型名前缀 `MD`（TypeContext 单例跨测试泄漏）；新增 e2e 文件需 `cd build && cmake ..` 重新 configure。

## Review Focus

1. **维度顺序反转**——`[2][3]` 误建 `[3][2]` 时初始化与下标值错位但常能编译。→ Task 2 `MultiDimInitAndSum`（求和 21 布局敏感）+ `MultiDimSubscriptWriteRead`（位置敏感往返）。
2. **全局/sizeof 布局**——嵌套 LLVM 类型错误会在全局常量与字节量上暴露。→ Task 2 `MultiDimGlobalConstAndSizeof`（sizeof == 24 钉死）。
3. **`a[i]` 内层 lvalue**——GEP 链正确性与行退化写穿透。→ Task 2 `MultiDimRowDecayToSlice`（经 slice 写回原数组）。
4. **1-D 回归**——dims 长度 1 时必须与现行为逐一等价。→ Task 1 全量回归（既有 ArrayParam/MethodArgs/Slice 套件即回归网）。
5. **成员字段多维**——class/struct 字段 `int32 g[2][3]` 走 parseMemberArraySuffix 新链。→ Task 1 `MultiDimMemberFieldAccepted`。

---

### Task 1: Parser 维度链 + sema 测试

**Files:**
- Modify: `src/frontend/Parser.cpp:2221-2237`（parseVariableDecl）、`:2456-2466`（parseMemberArraySuffix）
- Test: `tests/sema/test_semantic_analyzer.cpp`（文件尾追加）

**Interfaces:**
- Consumes: `ArrayType(Type* elem, int size)`（Type.h:66）；`ArrayDeclAST(name, elementType, size, init)`（Decl.h:92-97，sema wrap 现有）。
- Produces: 嵌套 ArrayType 声明——Task 2 e2e 消费。

- [ ] **Step 1: 写 6 个测试**（追加到 `tests/sema/test_semantic_analyzer.cpp` 尾部）：

```cpp
// TYP-11: multi-dimensional array declarations.
TEST(SliceSemTest, MultiDimDeclAccepted) {
    EXPECT_TRUE(analyzeOk(
        "int32 main() { int32 a[2][3] = {{1,2,3},{4,5,6}}; return a[1][2]; }"));
}

TEST(SliceSemTest, MultiDimFirstDimInferred) {
    EXPECT_TRUE(analyzeOk(
        "int32 main() { int32 a[][3] = {{1,2,3},{4,5,6}}; return a[1][2]; }"));
}

TEST(SliceSemTest, MultiDimSubscriptTypes) {
    EXPECT_TRUE(analyzeOk(
        "int32 MDSum(int32[] s) { return s[0] + s[1] + s[2]; } "
        "int32 main() { int32 a[2][3] = {{1,2,3},{4,5,6}}; return MDSum(a[1]); }"));
}

TEST(SliceSemTest, MultiDimWholeToSliceRejected) {
    EXPECT_FALSE(analyzeOk(
        "int32 MDSum(int32[] s) { return s[0]; } "
        "int32 main() { int32 a[2][3] = {{1,2,3},{4,5,6}}; return MDSum(a); }"));
}

TEST(SliceSemTest, MultiDimDanglingReturnRejected) {
    EXPECT_FALSE(analyzeOk(
        "int32[] MDBad() { int32 a[2][3] = {{1,2,3},{4,5,6}}; return a; } "
        "int32 main() { return 0; }"));
}

TEST(SliceSemTest, MultiDimMemberFieldAccepted) {
    EXPECT_TRUE(analyzeOk(
        "class MDBox { public: int32 g[2][3]; int32 last() { return this->g[1][2]; } }; "
        "int32 main() { MDBox b; return 0; }"));
}
```

- [ ] **Step 2: 跑 RED**

Run: `cd build && cmake --build . -j$(nproc) && ./bin/compiler_tests --gtest_filter='SliceSemTest.MultiDim*'`
预期：`MultiDimDeclAccepted`/`MultiDimFirstDimInferred`/`MultiDimSubscriptTypes`/`MultiDimDanglingReturnRejected`/`MultiDimMemberFieldAccepted` **FAIL**（`[2][3]` 解析错误）；`MultiDimWholeToSliceRejected` PASS（钉住现状：解析失败经错误恢复后 undeclared 拒绝）。分布不同则停下核因。

- [ ] **Step 3: 实现**

1. `parseVariableDecl`：第一个 `[N]` 消费后，`while (check(TokenType::TOKEN_LBRACKET))` 继续收集 dims（每个 `[` 后 NUMBER 可选，缺省 0，`expect(TOKEN_RBRACKET)`）；然后 `std::vector<int> dims`（首元素为最外层已读维度）——从 `dims` 末尾向索引 1 逆序 `elem = new ArrayType(elem, dims[i])`（elem 初始为 `type`），`ArrayDeclAST(name, elem, dims[0], init)`。
2. `parseMemberArraySuffix`：循环收集 dims（NUMBER 缺省 **1**，保持现状），逆序构建 `result = new ArrayType(result, dims[i])` 返回。

- [ ] **Step 4: 全量绿**

Run: `cd build && ctest`
预期：**799/799**。

- [ ] **Step 5: Commit**

```bash
git add src/frontend/Parser.cpp tests/sema/test_semantic_analyzer.cpp
git commit -m "feat(TYP-11): 多维数组声明解析——维度链从右向左构建嵌套 ArrayType"
```

### Task 2: e2e 钉住（布局/退化/全局/sizeof）

**Files:**
- Create: `tests/e2e/test_multidim.cpp`（fixture 复制 `tests/e2e/test_method_args.cpp`，fixture 名改 `MultiDimE2E`，头注释改 TYP-11 多维）

**Interfaces:**
- Consumes: Task 1 的嵌套声明；D4 零改动通路（GEP/初始化器/退化/sizeof）。

- [ ] **Step 1: 写 4 个 e2e 测试**（`runSource` 从 test_method_args.cpp 原样复制）：

```cpp
// Review Focus 1: layout-sensitive sum — catches [3][2] inversion.
TEST_F(MultiDimE2E, MultiDimInitAndSum) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            int32 a[2][3] = {{1, 2, 3}, {4, 5, 6}};
            int32 s = 0;
            for (int32 i = 0; i < 2; i = i + 1)
                for (int32 j = 0; j < 3; j = j + 1)
                    s = s + a[i][j];
            return s == 21 ? 0 : 1;
        }
    )", "test_md_sum.c"), 0);
}

TEST_F(MultiDimE2E, MultiDimSubscriptWriteRead) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            int32 a[2][3] = {{1, 2, 3}, {4, 5, 6}};
            a[1][2] = 60;
            a[0][0] = a[0][0] + 10;
            return (a[1][2] == 60 && a[0][0] == 11 && a[0][1] == 2) ? 0 : 1;
        }
    )", "test_md_write.c"), 0);
}

// Review Focus 3: a[1] is a 1-D array lvalue — decays to slice, writes go
// through to the original row.
TEST_F(MultiDimE2E, MultiDimRowDecayToSlice) {
    EXPECT_EQ(runSource(R"(
        int32 MDBump(int32[] s) { s[0] = s[0] + 100; return s[0]; }
        int32 main() {
            int32 a[2][3] = {{1, 2, 3}, {4, 5, 6}};
            return (MDBump(a[1]) == 104 && a[1][0] == 104 && a[0][0] == 1) ? 0 : 1;
        }
    )", "test_md_rowdecay.c"), 0);
}

// Review Focus 2: global constant init + DataLayout size (2*3*4 = 24).
TEST_F(MultiDimE2E, MultiDimGlobalConstAndSizeof) {
    EXPECT_EQ(runSource(R"(
        int32 g[2][3] = {{1, 2, 3}, {4, 5, 6}};
        int32 main() {
            return (g[1][2] == 6 && sizeof(g) == 24) ? 0 : 1;
        }
    )", "test_md_global.c"), 0);
}
```

- [ ] **Step 2: 跑测试**（先 `cd build && cmake ..`）

Run: `cd build && cmake .. >/dev/null && cmake --build . -j$(nproc) && ./bin/compiler_tests --gtest_filter='MultiDimE2E.*'`
预期：4 项 **PASS**（实现已在 Task 1 落地，D4 零改动主张的验证步骤；此为钉住，与 TYP-11/MethodCall 轮 e2e 同模式）。若有 FAIL，按 systematic-debugging 查因——D4 主张被证伪时停下重估 spec。

- [ ] **Step 3: 全量绿**

Run: `cd build && ctest`
预期：**803/803**。

- [ ] **Step 4: Commit**

```bash
git add tests/e2e/test_multidim.cpp
git commit -m "feat(TYP-11): 多维数组 e2e 钉住——布局/行退化/全局常量/sizeof"
```

### Task 3: 文档收尾 + 完成记录

**Files:**
- Modify: `TODO.md`（TYP-11 行：待补改为"多维形参、VLA"，补记本体完成）
- Modify: `Progress.md`（gitignore，需 `git add -f`）

- [ ] **Step 1: TODO.md** —— TYP-11 行"待补：多维数组本体、多维形参、VLA"改为"待补：多维形参、VLA"，并补"多维数组本体已完成（2026-10-03，spec `docs/superpowers/specs/2026-10-03-multidim-arrays-design.md`）"。
- [ ] **Step 2: Progress.md 追加**（时间戳条目：改动文件、验证 803/803、遗留）。
- [ ] **Step 3: Commit**

```bash
git add TODO.md && git add -f Progress.md
git commit -m "docs: 多维数组本体完成记录（803/803）"
```

- [ ] **Step 4: 整分支评审**（review-package `6652f2f..HEAD` → subagent 评审 → 修复；沿用 R7/R7b）
