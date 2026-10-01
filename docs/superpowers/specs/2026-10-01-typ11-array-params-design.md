# TYP-11 之一：数组形参去糖为 Slice —— 设计

日期：2026-10-01
状态：已批准（设计对谈 2026-10-01）
前置：TYP-12 Slice（`docs/superpowers/specs/2026-09-30-slice-design.md`）

## 1. 背景与问题

- 形参位置 `int32 a[2]` 解析报错：`parseParamDecl` 消耗标识符后，`[2]` 无人消费 → `expected ')' after parameter list`。本地数组声明（`ArrayDeclAST`）已有，仅缺形参位置。
- TYP-12 已建立完整通路：slice 形参 `int32[] s` + 数组实参零拷贝退化（CLI 实证 `sum(a)` exit=7）。
- **安全漏洞（TYP-12 Task 5 引入）**：`typesCompatible` 的 Slice/Array 分支使 `int32[] bad() { int32 a[2] = {1,2}; return a; }` 在 sema 层静默通过，返回指向已死亡栈帧的 slice 视图——悬垂 UB，且"看似正常运行"。

## 2. 冻结决策

| # | 决策 | 理由 |
|---|------|------|
| D1 | 范围=仅一维固定数组形参 | 多维数组本体（声明/下标/codegen）不存在；依赖它的多维形参与 VLA 范围外 |
| D2 | `T name[N]` 形参**去糖为 slice** `T[]`（parser 层） | 与语言 slice 哲学一致：保长度、零拷贝；sema/codegen/mangle 零改动（全走既有 slice 通道） |
| D3 | `[N]` 为**文档性标注**，不静态检查长度 | slice 长度本就动态；`int32[] s` 形参与 `T name[N]` 形参完全等价 |
| D4 | **悬垂封堵**：return 表达式类型为 ArrayType 且函数返回类型为 Slice → 拒绝 | 保守规则：当前所有 ArrayType 值均为局部变量（形参已去糖、全局数组非本项范围）；安全优先 |

否决的替代方案：B（ArrayType 保留到 sema 再去糖——多触一层、mangle 需处理 ArrayType 形参，无收益）；C（值传递拷贝——违背零拷贝哲学，触发 MEM-14 ABI 复杂度）。

## 3. 实现

### 3.1 `parseParamDecl`（`src/frontend/Parser.cpp`）

标识符消耗后，若遇 `[`：

1. 消耗 `[`；若遇数字 token 记录（丢弃）；期望 `]`（缺失则报错返回 nullptr）。
2. 参数类型 = `TypeContext::instance().getSliceType(type)`。
3. 若 `]` 之后紧跟 `[`（多维尝试）→ 明确报错"数组形参暂不支持多维"（清晰诊断优于让 `expect(')')` 报歧义错误）。
4. 空参名允许（`int32[2]`？）——与现状 `parseParamDecl` 的可选标识符行为一致：无标识符时 `name` 为空。仅在有标识符时处理 `[N]` 后缀。

注意：类型位置的 `int32[] a`（slice 类型 + 标识符）已由 `parseType` 的 slice 循环（TYP-12 Task 2）处理，两条拼写在此收敛为同一 SliceType。

### 3.2 `visit(ReturnStmtAST)`（`src/sema/SemanticAnalyzer.cpp`）

在现有返回类型检查前加守卫：若 `currentFunction->returnType`（去 typedef）为 Slice 且返回表达式类型（去 typedef）为 ArrayType → `emitError`（无码通道，与 struct no-member 一致）："cannot return a slice view of an array with local storage (dangling view)"。（实现备注：`ReturnStmtAST` 无 type 字段，守卫仅报错；当前所有 ArrayType 值均具局部存储，全局数组被保守拒绝。）

该守卫同时移除 TYP-12 Task 5 遗留的隐患：`typesCompatible` 的 Slice/Array 分支从 return 路径不可达（赋值/初始化路径不受影响）。

## 4. 测试计划（TDD，RED 先行）

1. **parse/sema**：`int32 sum(int32 a[2])` + `sum(arr)` 实参数组 → accepted；`int32 sum(float64 a[2])` 传 `int32` 数组 → rejected（元素类型严格）。
2. **悬垂拒绝**：`int32[] bad() { int32 a[2] = {1,2}; return a; }` → rejected；诊断快照 1 份。
3. **e2e**（新建 `tests/e2e/test_array_params.cpp`，fixture 复制 test_slice.cpp）：
   - `int32 sum(int32 a[2]) { return a[0]+a[1]; }` + `{3,4}` → 7
   - 形参写穿透：`void bump(int32 a[2]) { a[0] += 100; }` 原数组可见（零拷贝）
   - `int32[] pick(int32 arr[2]) { return arr; }`（形参 slice，return 直通）→ 下标/`.len` 正确
4. 全量 `ctest` 绿（基线 772）。

## 5. 文档与收尾

- `conversions.md` §10：`return` 退化改述为"局部数组返回被拒绝（D4）"。
- `TODO.md`：TYP-11 标注部分完成（一维形参 ✅；多维本体/多维形参/VLA 待补）；消除"MethodCall 退化"依赖描述中关于形参数组的表述。

## 6. 范围外

- 多维数组本体（`T a[2][3]` 声明/下标/codegen）、多维形参、VLA（运行时长度栈数组，与 slice 语义冲突需独立设计）。
- 全局数组视图的 return 放开（待全局数组本体落地后重议）。
- `extern` 变量声明（语言无该标志）。
