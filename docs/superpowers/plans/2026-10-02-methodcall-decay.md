# MethodCall 传参退化 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** class 方法可接收数组实参——`obj.bump(arr)` 经数组→slice 零拷贝退化（TYP-12 遗留项）。

**Architecture:** 镜像普通函数调用的既有模式（`CallExprAST::resolvedParamTypes` + mangle 按声明类型 + `isArrayToSliceArg` 退化）：sema `resolveMethod` 改两阶段匹配（先精确后 rank），命中后把声明形参（含 this）存入 `MethodCallExprAST::resolvedParamTypes`；codegen 按 resolvedParamTypes mangle 与实参处理。

**Tech Stack:** C++20 / LLVM / gtest（与仓库一致）。

**Spec:** `docs/superpowers/specs/2026-10-02-methodcall-decay-design.md`（D1–D5 冻结决策，`4b346c7`）

## Global Constraints

- 基线 **784/784**（`ctest`，ec4a98f）；每任务结束全量必须绿。
- 决策 D2：两阶段匹配——阶段 1 精确（现行为，声明序首个命中）；阶段 2 首个"全参 `conversionRank(argTypes[i], paramTypes[i+1]) >= 0`"候选。**否决**最优 match 打分。
- D3：`resolvedParamTypes` 存 `FunctionType::paramTypes` **整体（含 this，索引 0）**；codegen 显式实参 `i` 对应 `resolvedParamTypes[i+1]`（与 CallExpr 的 `resolvedParamTypes[i]` 错一位，勿照抄索引）。
- D4：`argType == nullptr` 的实参淘汰该候选（conversionRank 在 `src/ast/Symbol.h:33`，`int conversionRank(Type* from, Type* to)`，不容忍 null）。
- D5：codegen 实参处理镜像 `CallExprAST::codegen`（Expr.cpp:596-615）：`isArrayToSliceArg`（Expr.cpp:19 static）→ `ctx.emitArrayToSliceDecay(static_cast<ArrayType*>(stripTypedefsT12(argType)), argVal)`；否则 `ctx.castValue(argVal, argType, ctx.getLLVMType(paramType))`。
- 诊断走既有通道，不新增错误码；测试名/类型名前缀 `MC`（TypeContext 单例跨测试泄漏）。
- 新增 e2e 文件需 `cd build && cmake ..` 重新 configure（GLOB_RECURSE）。

## Review Focus

1. **精确匹配回归**——现网全部方法调用依赖精确匹配；两阶段改造后行为必须逐一等价（声明序首个命中）。→ Task 1 `MethodExactMatchUnchanged` + 全量回归。
2. **this 索引错位**——resolvedParamTypes[0] 是 this，实参循环若照抄 CallExpr 的 `[i]` 会把 this 类型当形参用。→ Task 2 e2e `MethodArrayParamWriteThrough`（`this->x` 写读生效即证明 this 传递正确）。
3. **继承链 + 数组形参**——sema 递归到基类方法时，resolvedParamTypes 必须是**基类**声明形参（含基类 this），mangle 才能命中基类定义。→ Task 1 `MethodInheritedSliceParamAccepted`。
4. **跨元素不得借退化放行**——`float64[]` 形参收 `int32` 数组必须仍拒绝。→ Task 1 `MethodCrossElementRejected`。
5. **null argType 崩溃**——实参类型解析失败（未声明变量）不得让 conversionRank 解引用 null。→ Task 1 `MethodNullArgGuard`。

---

### Task 1: sema 两阶段匹配 + resolvedParamTypes 存储

**Files:**
- Modify: `src/ast/Expr.h:270`（MethodCallExprAST 类内新增成员）
- Modify: `src/sema/SemanticAnalyzer.cpp:2108`（resolveMethod）、`:1531`（visit(MethodCallExprAST) 尾部，`auto* funcType = ...` 之后）
- Test: `tests/sema/test_semantic_analyzer.cpp`（文件尾追加）

**Interfaces:**
- Consumes: `int conversionRank(Type* from, Type* to)`（`src/ast/Symbol.h:33`，已可用）；`resolveMethod` 现有签名不变。
- Produces: `MethodCallExprAST::resolvedParamTypes`（`std::vector<Type*>`，含 this）——Task 2 codegen 消费。

- [ ] **Step 1: 写 5 个失败/钉住测试**（追加到 `tests/sema/test_semantic_analyzer.cpp` 尾部；类语法 `class Foo { public: ... };`，实例 `Foo f;`，调用 `f.setX(...)`，字段 `this->x`）：

```cpp
// TYP-12 遗留: 方法形参为 slice 时接受数组实参（退化）。
TEST(SliceSemTest, MethodSliceParamAcceptsArray) {
    EXPECT_TRUE(analyzeOk(
        "class MCBox { public: void bump(int32[] s) { s[0] = s[0] + 100; } }; "
        "int32 main() { MCBox b; int32 arr[2] = {1,2}; b.bump(arr); return 0; }"));
}

// Review Focus 1: 精确匹配路径逐一等价于现状。
TEST(SliceSemTest, MethodExactMatchUnchanged) {
    EXPECT_TRUE(analyzeOk(
        "class MCCnt { public: int32 x; void setX(int32 v) { this->x = v; } }; "
        "int32 main() { MCCnt c; c.setX(42); return 0; }"));
}

// Review Focus 4: 跨元素不得借退化放行。
TEST(SliceSemTest, MethodCrossElementRejected) {
    EXPECT_FALSE(analyzeOk(
        "class MCF { public: void f(float64[] s) { } }; "
        "int32 main() { MCF m; int32 arr[2] = {1,2}; m.f(arr); return 0; }"));
}

// Review Focus 3: 继承链解析到基类方法（基类声明形参入 resolvedParamTypes）。
TEST(SliceSemTest, MethodInheritedSliceParamAccepted) {
    EXPECT_TRUE(analyzeOk(
        "class MCB { public: void bump(int32[] s) { s[0] = s[0] + 1; } }; "
        "class MCD : public MCB { public: int32 y; }; "
        "int32 main() { MCD d; int32 arr[2] = {1,2}; d.bump(arr); return 0; }"));
}

// Review Focus 5: 实参数目不足/类型解析失败不崩溃、仍拒绝。
TEST(SliceSemTest, MethodNullArgGuard) {
    EXPECT_FALSE(analyzeOk(
        "class MCG { public: void g(int32[] s) { } }; "
        "int32 main() { MCG m; m.g(); return 0; }"));
}
```

- [ ] **Step 2: 跑 RED**

Run: `cd build && cmake --build . -j$(nproc) && ./bin/compiler_tests --gtest_filter='SliceSemTest.Method*'`
预期：`MethodSliceParamAcceptsArray`、`MethodInheritedSliceParamAccepted` **FAIL**（现 resolveMethod 指针同一性失配）；其余 3 项 PASS（钉住现状）。若 PASS/FAIL 分布不同，停下核因再继续。

- [ ] **Step 3: 实现**

1. `Expr.h:270` MethodCallExprAST 内（public 段，镜像 CallExprAST:166 注释风格）：
   `std::vector<Type*> resolvedParamTypes; // Declared param types (incl. this) filled by sema.`
2. `resolveMethod`（:2108）：保留阶段 1 精确循环不动；在其后（基类递归前）加阶段 2——逐候选：任一 `argTypes[i] == nullptr` → 淘汰；否则全参 `conversionRank(argTypes[i], paramTypes[i+1]) >= 0` → 命中返回。两阶段之后保留既有基类递归。
3. `visit(MethodCallExprAST)`（:1531 `auto* funcType = ...` 之后、`node.type = ...` 之前）：
   `node.resolvedParamTypes = funcType->paramTypes;`

- [ ] **Step 4: 全量绿**

Run: `cd build && ctest`
预期：**789/789**。

- [ ] **Step 5: Commit**

```bash
git add src/ast/Expr.h src/sema/SemanticAnalyzer.cpp tests/sema/test_semantic_analyzer.cpp
git commit -m "feat(MC): resolveMethod 两阶段匹配并存储声明形参（含 this）"
```

### Task 2: codegen 按 resolvedParamTypes mangle 与实参退化

**Files:**
- Modify: `src/ast/Expr.cpp:636-712`（MethodCallExprAST::codegen）
- Create: `tests/e2e/test_method_args.cpp`（fixture 复制 `tests/e2e/test_array_params.cpp`，fixture 名改 `MethodArgsE2E`，文件头注释改 TYP-12 MethodCall）

**Interfaces:**
- Consumes: Task 1 的 `resolvedParamTypes`（含 this）；`isArrayToSliceArg`（Expr.cpp:19）、`stripTypedefsT12`（Expr.cpp static）、`ctx.emitArrayToSliceDecay`（CodegenContext.h:85）、`ctx.castValue(Value*, Type* fromAST, llvm::Type*)`（CodegenContext.h:81）。

- [ ] **Step 1: 写 2 个 RED e2e 测试**（`tests/e2e/test_method_args.cpp`，`runSource` 从 test_array_params.cpp 原样复制）：

```cpp
// TYP-12 遗留: 数组实参经退化传入方法，写穿透直达原数组。
TEST_F(MethodArgsE2E, MethodArrayParamWriteThrough) {
    EXPECT_EQ(runSource(R"(
        class Box {
            public:
            int32 x;
            void bump(int32[] s) { s[0] = s[0] + 100; this->x = 1; }
        };
        int32 main() {
            Box b;
            int32 arr[2] = {1, 2};
            b.bump(arr);
            return (arr[0] == 101 && arr[1] == 2 && b.x == 1) ? 0 : 1;
        }
    )", "test_mc_writethrough.c"), 0);
}

TEST_F(MethodArgsE2E, MethodArrayParamReturnValue) {
    EXPECT_EQ(runSource(R"(
        class Sum {
            public:
            int32 total(int32[] s) { return s[0] + s[1]; }
        };
        int32 main() {
            Sum m;
            int32 arr[2] = {3, 4};
            return m.total(arr) == 7 ? 0 : 1;
        }
    )", "test_mc_retval.c"), 0);
}
```

- [ ] **Step 2: 跑 RED**（先 `cd build && cmake ..` 重新 configure）

Run: `cd build && cmake .. >/dev/null && cmake --build . -j$(nproc) && ./bin/compiler_tests --gtest_filter='MethodArgsE2E.*'`
预期：2 项 **FAIL**（sema 已放行但 codegen 按实参类型 mangle 查不到符号 → runSource 返回 -1）。

- [ ] **Step 3: 实现**（Expr.cpp MethodCallExprAST::codegen）

1. mangle：现构造 `argTypes = [thisType] + arg->type` 改为——
   `if (!resolvedParamTypes.empty()) argTypes = resolvedParamTypes;`（`thisType` 构造仅留在回退分支）。
2. 继承链回退 walk（`if (!calleeFn) { ... }` 整块）仅在回退路径执行（sema 已按链解析，resolvedParamTypes 即定义类声明）。
3. 实参循环：`emitLoad` 之后、`argsV.push_back` 之前镜像 CallExpr:603，**索引 i+1**：
   - `if (i + 1 < resolvedParamTypes.size())`：
     - `isArrayToSliceArg(args[i]->type, resolvedParamTypes[i+1])` → `argVal = ctx.emitArrayToSliceDecay(static_cast<ArrayType*>(stripTypedefsT12(args[i]->type)), argVal);`
     - 否则 `argVal = ctx.castValue(argVal, args[i]->type, ctx.getLLVMType(resolvedParamTypes[i+1]));`
   - resolvedParamTypes 为空时保持现状（仅 emitLoad）。

- [ ] **Step 4: 全量绿**

Run: `cd build && ctest`
预期：**791/791**。

- [ ] **Step 5: Commit**

```bash
git add src/ast/Expr.cpp tests/e2e/test_method_args.cpp
git commit -m "feat(MC): 方法调用按声明形参 mangle 并接入数组→slice 退化"
```

### Task 3: 文档收尾 + 完成记录

**Files:**
- Modify: `TODO.md`（TYP-12 行"遗留：MethodCall 传参退化"更新）
- Modify: `Progress.md`（gitignore，需 `git add -f`）

- [ ] **Step 1: TODO.md** —— TYP-12 行遗留改为已消除（保留既有其余遗留），P1-01 行核对无需改（不涉及）。
- [ ] **Step 2: Progress.md 追加**（时间戳条目：改动文件、验证 791/791、遗留）。
- [ ] **Step 3: Commit**

```bash
git add TODO.md && git add -f Progress.md
git commit -m "docs: MethodCall 传参退化完成记录（791/791）"
```

- [ ] **Step 4: 整分支评审**（review-package `4b346c7..HEAD` → subagent 评审 → 修复；沿用 R7/R7b：失败一次则自审）
