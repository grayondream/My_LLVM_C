# 多维数组本体 —— 设计

日期：2026-10-03
状态：设计已批准（对谈 2026-10-03）
前置：TYP-11（一维数组与形参去糖）、TYP-12（Slice 退化）
TODO：TYP-11 剩余项之一

## 1. 背景与问题

`T a[2][3]` 目前无法声明：`parseVariableDecl`（Parser.cpp:2222-2237）只消费**一个** `[size]` 即返回 `ArrayDeclAST`（单维度 `{elementType, size}`）；`parseMemberArraySuffix`（Parser.cpp:2456-2466，类/结构成员字段）同样单层。CLI 实证：`int32 a[2][3] = ...` 报 "expected ';' after variable declaration"。

其余通路已天然递归，是本设计零改动的前提（均已核实）：

| 通路 | 现状 | 位置 |
|---|---|---|
| LLVM 类型 | `llvm::ArrayType::get(getLLVMType(elem), size)` 递归 | CodegenContext.cpp:476 |
| 下标 sema | `node.type = elementType`（`a[i]` 得内层 ArrayType lvalue） | SemanticAnalyzer.cpp:1330 |
| 下标 codegen | `CreateGEP(getLLVMType(elemTy), ...)` 嵌套可用 | Expr.cpp:862-894 |
| 嵌套初始化 | `emitAggregateInitializer` Array 分支对 nested init list 递归 | Decl.cpp:58-80 |
| 全局常量初始化 | `buildAggregateConstant` 同样递归 | Decl.cpp:131+ |
| mangle | `typeToMangled(elem) + "arr"` 递归 | Mangle.cpp:27-31 |
| sizeof | DataLayout `getTypeStoreSize` | Expr.cpp:1033 |

## 2. 冻结决策

| # | 决策 | 理由 |
|---|------|------|
| D1 | **嵌套 ArrayType 表示**：`T[2][3]` = `ArrayType{ArrayType{T,3}, 2}`，LLVM `[2 x [3 x i32]]` | 对齐 C；上表全部递归通路零改动。否决扁平化 stride 表示（需新类型、新下标算术、新 mangle，收益为零） |
| D2 | `parseVariableDecl` 循环消费连续 `[N]`，**从右向左**构建嵌套 elementType：`int32 a[2][3]` → `ArrayDeclAST(name, ArrayType(T,3), 2)`，sema 现有 `new ArrayType(node.elementType, node.size)`（SemanticAnalyzer.cpp:1733）自动成嵌套 | 最小改动：sema ArrayDecl 通道零改动；`parseMemberArraySuffix` 同链支持成员字段 |
| D3 | 首维推断：`T a[][3] = {{...},{...}}` 行数从 initList 推断（`node.size == 0` 时取 `initializers.size()`） | 对齐 1-D 现状（SemanticAnalyzer.cpp:1724-1728 已有同逻辑） |
| D4 | **codegen 零改动**：GEP/初始化器/sizeof/内层退化全部复用；`a[i]` 是一维 ArrayType lvalue，现有 array→slice 退化通道（`emitArrayToSliceDecay`）直接可用 | D1 的直接推论 |
| D5 | 整体 `T[2][3]` → `slice<T>` **不放行**：`isArrayToSliceArg` 要求元素 typesEqual（`Slice<T>` 元素 T vs 实际元素 `T[3]`）天然拒绝，无需新代码；`return a`（多维）被 TYP-11 悬垂守卫拒绝（匹配任意 ArrayType）✓ 自动 | 语义一致性；行 slice 语义（`slice<slice<T>>`）留待多维形参轮次 |

## 3. 范围外

- 多维形参 `T a[2][3]` 去糖（需先定语义：行 slice 的 slice / 扁平化——独立轮次）
- VLA（可变长度数组）
- 整体数组赋值 / 行赋值（`a[i] = {...}`、`a = b`；现状 1-D 亦无数组赋值）
- 行级初始化长度校验（`{{1,2},{3,4,5}}` 超长行保持既有截断语义，1-D 亦不查）
- `a[i]` 作为整行右值传 `slice<slice<T>>`（依赖多维形参轮次）

## 4. 测试计划（TDD，RED 先行；基线 793）

1. **sema**（`SliceSemTest`，前缀 MD）：
   - `MultiDimDeclAccepted`：`int32 a[2][3] = {{1,2,3},{4,5,6}}` → accepted；
   - `MultiDimFirstDimInferred`：`int32 a[][3] = {{1,2,3},{4,5,6}}` → accepted；
   - `MultiDimSubscriptTypes`：`a[1][2]` 为 int32、`a[1]` 可传 `int32[]` 形参 → accepted；
   - `MultiDimWholeToSliceRejected`：整体 `a` 传 `slice` 形参 → rejected；
   - `MultiDimDanglingReturnRejected`：`return a` → rejected（悬垂守卫）；
   - `MultiDimMemberFieldAccepted`：class 成员 `int32 g[2][3]` → accepted。
2. **e2e**（新建 `tests/e2e/test_multidim.cpp`，fixture 复制 test_method_args.cpp，fixture 名 `MultiDimE2E`，函数/类型前缀 MD）：
   - `MultiDimInitAndSum`：2x3 初始化 + 全元素求和 == 21；
   - `MultiDimSubscriptWriteRead`：`a[i][j] = v` 写读往返；
   - `MultiDimRowDecayToSlice`：`a[1]` 传 slice 形参求和；
   - `MultiDimGlobalConstAndSizeof`：全局 `int32 g[2][3] = {{...}}` 常量初始化 + `sizeof(a) == 24`。
3. 全量 `ctest` 绿（预期 793 + 10 = 803）。

## 5. 实现落点

- `src/frontend/Parser.cpp` `parseVariableDecl`（:2221-2237）：`[N]` 后循环消费 `[N]` 链，收集 dims 向量，从右向左 `new ArrayType` 构建elementType；`parseMemberArraySuffix`（:2456）同链。
- `src/sema/SemanticAnalyzer.cpp` `visit(ArrayDeclAST)`（:1723）：零改动（D2/D3 验证此点；如首维推断需要 initList 已解析维度则在此处微调）。
- `src/ast/Expr.cpp` / `src/ast/Decl.cpp` / `src/ast/Mangle.cpp` / `src/codegen/CodegenContext.cpp`：零改动。
