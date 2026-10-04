# TYP-11 多维数组形参——行 slice 去糖 设计（spec）

日期：2026-10-04
状态：已获用户批准的设计（方案 A：行 slice）
前置：多维数组本体已完成（`docs/superpowers/specs/2026-10-03-multidim-arrays-design.md`，806/806）

## 1. 目标

多维数组作实参传递给形参：

```
int32 f(int32 name[][3])      // 去糖：Slice<int32[3]>
int32 g(int32 name[2][3])     // 同上——首维 [2] 文档性
f(a)                          // a: int32[2][3] → 退化 {ptr, 2}，元素 int32[3]
```

延续 TYP-11 一维去糖先例（`docs/superpowers/specs/2026-10-01-typ11-array-params-design.md`：`[N]` 文档性、slice 承载动态性、零拷贝）。

## 2. 冻结决策

- **D1'（语义）**：`T name[][K]` 去糖为 `Slice<ArrayType(T, K)>`（行 slice）。函数体内 `name[i]` 是 `T[K]` 左值，行退化回 `T[]`（既有通路）；`name[i][j]` 链式下标；`name.len` = 行数。
- **D2'（仅首维可缺省）**：`T name[2][3]` 与 `T name[][3]` 等价（首维文档性，沿用一维 D3）；内层维度必须显式——`T name[3][]` 解析期拒绝（与本体轮 NonFirstDimDefaultRejected 守卫一致）。
- **D3'（行类型编译期安全）**：实参→形参走 `conversionRank` 的数组→slice 规则（`typesEqual(数组元素, slice元素)` 严格比较）——`int32[2][4]` 传给 `[][3]` 形参被拒绝，无静默重解释。
- **D4'（零改动主张）**：去糖仅在 Parser `parseParamDecl`（现有 `:2287` 单 `[N]` 块扩展为维度链），sema/codegen 零改动。依据（已核实）：
  - `TypeContext::getSliceType(Type*)` 泛型（Type.cpp），聚合元素可直接构造；
  - `conversionRank`（Symbol.cpp:33）数组→slice 严格比较元素，对元素为 ArrayType 天然成立；
  - 退化 `emitArrayToSliceDecay` / `getLLVMType` / mangle 全递归（本体轮 D4 逐条核实表沿用）；
  - MethodCall `resolvedParamTypes` 两阶段匹配用 `conversionRank`，2-D 实参→行 slice 形参自动生效。
- **D5'（return 不放行）**：多维数组 return 仍被悬垂守卫拒绝（本体轮 D4），本轮不新增 return 语义。

## 3. 范围外

- VLA（TYP-11 另一项）
- `name[3][]`（内层缺省）——直接拒绝
- Slice of Slice（`name[][][]`）语法
- 行级初始化长度校验（1-D 亦无）
- 裸函数类型形参 `int32 cb(int32 a[2])`（parser 既有缺口，另立项）

## 4. 实现要点

- `Parser.cpp::parseParamDecl`：现有单 `[N]` 消费块改为维度链收集（首维可选、内层必须显式，缺省即 `errorUnexpected`）→ 从右向左构建 elem `ArrayType` → `getSliceType(elem)`。删除 `:2298` 的多维拒绝分支。
- 预判陷阱（计划阶段 Review Focus 钉住）：**slice 下标产出聚合元素左值再下标**（`name[i][j]`）这条链无既有测试覆盖——`Slice<Array>` 下标 → `T[K]` 左值 → 行退化/再次下标。e2e 必测。

## 5. 测试计划（TDD，基线 806）

sema（`tests/sema/test_semantic_analyzer.cpp`，前缀 `MDP` 防 TypeContext 泄漏）：

1. `MDP2DParamAccepted`：`f(a)`，`a: int32[2][3]` → 形参 `int32 name[][3]` ✓
2. `MDPRowTypeMismatchRejected`：`int32[2][4]` 实参 → `[][3]` 形参 ✗（EXPECT_FALSE）
3. `MDPNonFirstDimRejected`：`int32 name[3][]` 解析期拒绝（显式管线断言 parser errors 非空）
4. `MDPExplicitFirstDimAccepted`：`name[2][3]` 形参与 `[][3]` 等价 ✓
5. `MDPParamSubscript`：函数体内 `return name[i][j] + name[0][0];` 全程序 sema ✓

e2e（`tests/e2e/test_multidim.cpp` 追加）：

6. 行 slice 形参 `name[i][j]` 求和/写穿透回实参（零拷贝实证：函数内写 `name[1][0]`，调用方 `a[1][0]` 变化）
7. MethodCall 方法形参 `int32 name[][3]`，`obj.m(a)` 传 2-D 实参 ✓（resolvedParamTypes 通路）
8. `name.len` = 行数

预期全量 806 → 约 814，全绿。

## 6. 验收标准

- 全量 ctest 绿；2-D 数组可零拷贝传给行 slice 形参并在函数体内链式下标读写
- 行类型不匹配、内层缺省维度在编译期拒绝
- TODO.md TYP-11 行更新（仅剩 VLA）；Progress.md 记录；subagent 评审并修复
