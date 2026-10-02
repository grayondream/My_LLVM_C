# MethodCall 传参退化 —— 设计

日期：2026-10-02
状态：设计已批准（对谈 2026-10-02）
前置：TYP-12 Slice（退化机制）、TYP-11（数组形参去糖）

## 1. 背景与问题

TYP-12 遗留项：class 方法不能接收数组实参。两层缺陷：

1. **sema `resolveMethod`**（`SemanticAnalyzer.cpp:2108`）：形参匹配用 `Type*` 指针同一性（`paramTypes[i+1] != argTypes[i]`）。数组实参（`ArrayType*`）对 slice 形参（`SliceType*`）必失配 → "no matching method"。
2. **codegen `MethodCallExprAST::codegen`**：mangle 查找用**实参类型**构造（`argTypes = [this] + arg->type`），而方法定义点（`FunctionDeclAST::codegen`，方法即 `FunctionDeclAST`，`Decl.h:116`）按**声明类型** mangle（slice 形参 mangle 为 elemslice）。`obj.bump(arr)` 即使 sema 通过也会查不到符号。

对照：普通函数调用已有成熟方案——`CallExprAST::resolvedParamTypes`（`Expr.h:166`，sema 填充）+ codegen 按 resolvedParamTypes mangle + `isArrayToSliceArg` 退化 + `castValue`。

## 2. 冻结决策

| # | 决策 | 理由 |
|---|------|------|
| D1 | 镜像 CallExpr 模式：`MethodCallExprAST` 新增 `std::vector<Type*> resolvedParamTypes` | 与既有函数调用机制同构，无新概念 |
| D2 | `resolveMethod` 改**两阶段匹配**：先全 rank 0（精确），否则首个"全参 `conversionRank(argType, paramType) >= 0`"候选 | 精确路径与现状逐一等价（同序首个命中），扩展仅在原报错场景放行；否决"最优 match 打分"（YAGNI） |
| D3 | 命中后 `FunctionType::paramTypes`（**含 this**）整体存入 `resolvedParamTypes` | codegen mangle 直接与定义点一致；回退路径（resolvedParamTypes 为空）保持现状 |
| D4 | `argType == nullptr` 的实参不参与匹配（直接淘汰该候选） | getExprType 已报错，避免 conversionRank 解引用 null |
| D5 | codegen 实参循环：`isArrayToSliceArg` → `ctx.emitArrayToSliceDecay`；否则 `ctx.castValue(argVal, arg->type, getLLVMType(paramType))`（arg->type 非空时） | 镜像 CallExpr :560/:604；退化 helper 与 stripTypedefsT12 同文件 static 可直接用 |

否决方案：仅改 codegen 不改 sema（前后端不一致，TYP-12 轮已裁定）；mangle 改为按实参类型（破坏定义点一致性）。

## 3. 实现

### 3.1 `src/ast/Expr.h`（MethodCallExprAST）

新增公有成员 `std::vector<Type*> resolvedParamTypes;`（含 this，与 CallExprAST:166 同款注释）。

### 3.2 `src/sema/SemanticAnalyzer.cpp`

- `resolveMethod`（:2108）：两阶段。阶段 1 精确（现状逻辑）；阶段 2 逐候选计算每参 `conversionRank(argTypes[i], paramTypes[i+1])`，全 >= 0 则命中。基类递归保持（两阶段均递归）。argTypes 含 nullptr → 该候选淘汰。
- `visit(MethodCallExprAST)`（:1476）：命中后 `node.resolvedParamTypes = funcType->paramTypes;`（含 this）。

### 3.3 `src/ast/Expr.cpp`（MethodCallExprAST::codegen）

- mangle：`resolvedParamTypes` 非空 → `mangleFunction(methodName, resolvedParamTypes)`；否则回退现构造。继承链回退查找保留（仅回退路径使用）。
- 实参循环：镜像 CallExpr——`i + 1 < resolvedParamTypes.size()` 时按 D5 处理；`resolvedParamTypes` 为空保持现状（仅 emitLoad）。

## 4. 测试计划（TDD，RED 先行；基线 784）

1. **sema**（`SliceSemTest`，前缀 MC）：
   - `MethodSliceParamAcceptsArray`：`void bump(int32[] s)` 成员 + `obj.bump(arr)` → accepted；
   - `MethodExactMatchUnchanged`：精确 slice 实参/整型实参方法照常；
   - `MethodCrossElementRejected`：`float64[]` 形参收 `int32` 数组 → rejected；
   - `MethodNullArgGuard`：调用缺实参/未声明变量的方法 → rejected（不崩溃）。
2. **e2e**（新建 `tests/e2e/test_method_args.cpp`，fixture 复制 test_array_params.cpp，fixture 名 `MethodArgsE2E`）：
   - `MethodArrayParamWriteThrough`：`obj.bump(arr)` 写穿透（arr[0]==101）+ this 生效；
   - `MethodArrayParamReturnValue`：slice 形参方法带返回值（求和 7）。
3. 全量 `ctest` 绿。

## 5. 范围外

- 重载最优匹配打分（同签名多候选消歧）——YAGNI，两阶段足够。
- `obj.method(arr)` 的 Arrow（`obj->method`）路径自动同享（同一 codegen/sema 通道），不单测。
- 方法重载解析与基类链的多层 rank 比较。
