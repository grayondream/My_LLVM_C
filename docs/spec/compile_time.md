# SafeModern C 1.0 — 编译期与反射规范（`compile_time`）

> 对应 TODO：**CT-01 ~ CT-14**、**PAR-15**、**SEM-06/07**、**DEC-05**
> 状态：`[impl]` 以 `src/sema/SemanticAnalyzer.cpp`、`src/frontend/Parser.cpp` 为准；`[plan]` 为目标。
> 原则：**零运行时开销**（ACC-04）；求值在编译期完成，结果以 LLVM 常量落地（CT-12）。**取代** `type_info(T)` 与独立 `static_assert` 关键字（NG-12）。

## 1. 语法入口（CT-01）

- 统一命名空间标识符 `compile_time`，成员用 `.` 访问：`compile_time.static_assert(...)`、`compile_time.if (...)`。
- 旧名 `comptime` 已移除（DEC-18，token 不再存在）。
- `compile_time` **不是保留关键字**，按普通标识符处理（见 keywords.md §9）。
- `constexpr`（现有）与 `compile_time` 的关系见 **DEC-05**（见 §10）。

## 2. 静态断言（CT-02）

```smc
compile_time.static_assert(size_of(Point) == 8, "Point must be 8 bytes");
```

- 条件必须是编译期常量布尔值；失败诊断携带**源码位置**与可选消息。
- 语法见 `grammar.ebnf` §8。

## 3. 条件编译（CT-03 / SEM-07）

```smc
compile_time.if (compile_time.target.os == "linux") {
    // 仅该目标参与语义分析与 codegen
} else {
    ...
}
```

- 条件在语义分析阶段求值；未选分支**不参与**类型检查与代码生成（死代码消除）。
- 与 `#if` 的区别：无预处理器、作用域/语法完整、仍受语法解析。

## 4. 目标查询（CT-04）

| 成员 | 类型 | 取值示例 |
|---|---|---|
| `compile_time.target.os` | `str` | `"linux"`, `"macos"`, `"windows"` |
| `compile_time.target.arch` | `str` | `"x86_64"`, `"aarch64"` |
| `compile_time.target.cpu` | `str` | CPU 名（来自目标三元组/特性） |

- 取值来自 `-target` 三元组（CG-16），保证跨平台可预测。

## 5. 构建查询（CT-05）

| 成员 | 类型 | 说明 |
|---|---|---|
| `compile_time.build.debug` | `bool` | Debug 构建 |
| `compile_time.build.optimize` | `str` | `"O0".."O3"/"Os"/"Oz"` |
| `compile_time.build.version` | `str` | 编译器版本（无 edition，见 NG-06） |

## 6. 编译期求值器（CT-06）

- 解释 AST 常量子集，支持：整数/浮点/布尔/字符串/字符字面量；算术、位、比较、逻辑、三元；常量数组/结构体成员访问；字符串比较/拼接；`size_of`/`align_of`/`offset_of`。
- 不得产生副作用；禁止运行时调用（除非标注为 `constexpr` 函数，见 §10）。
- 递归深度、迭代次数设上限并报诊断（防止编译期爆栈）。
- 常量折叠（`constexpr`）复用同一求值器（BASE-05）。

## 7. 反射 API（CT-07 / CT-13）

提案签名（待 **CT-13** 冻结精确返回类型）：

| API | 返回 | 说明 |
|---|---|---|
| `compile_time.name_of(T)` | `str` | 类型名 |
| `compile_time.size_of(T)` | `usize` | 分配大小 |
| `compile_time.align_of(T)` | `usize` | ABI 对齐 |
| `compile_time.fields_of(T)` | `Slice<FieldInfo>` | 字段列表 |
| `compile_time.offset_of(T, "f")` | `usize` | 字段偏移 |
| `compile_time.attributes_of(T)` | `Slice<Annotation>` | 注解列表（ANN-07） |

- `FieldInfo` 至少含：`name: str`、`type: Type`(类型值)、`offset: usize`、`size: usize`。
- 取代 `type_info(T)`（NG-12）；类型/ABI 细节见 [`abi.md`](./abi.md)。

## 8. 类型作为值（CT-08）

- 编译期一等公民：类型可作模板实参（GEN-03）、反射返回值、比较与选择。
- 类型值不等同运行时数据；不得逃逸到运行时。

## 9. 缓存 / 诊断 / 沙箱 / 常量集成（CT-09 ~ CT-12）

- **缓存（CT-09）**：按输入内容哈希缓存求值结果，接入增量编译（MOD-10）。
- **诊断（CT-10）**：错误包含源码位置、求值栈、原因；失败即中止该编译单元。
- **沙箱（CT-11）**：默认禁止文件/网络/系统访问；如需，必须显式白名单（接口待定）。
- **LLVM 常量集成（CT-12）**：求值结果直接生成 `llvm::Constant`，不留运行时指令。

## 10. 与 `constexpr` 的边界（CT-14 / DEC-05）

- 现状 `[impl]`：`constexpr` 变量/函数已被解析、常量折叠部分实现，无独立求值器。
- 选项（待 DEC-05）：
  1. 合并：`constexpr` 是 `compile_time` 的语法糖；
  2. 并存：`constexpr` 负责“可常量折叠的普通代码”，`compile_time` 负责“反射/目标查询/元编程”。
- 两选项都必须满足：**编译期完成、无运行时开销**（ACC-04）。

## 11. 与泛型的关系（GEN-06 / SEM-06）

- 模板实例化点做类型检查；反射可在实例化时读取类型信息，实现无 trait 的适配（`to_string`/`to_hash`/`equals`，见 semantics.md §5）。
- `compile_time` 不可绕过实例化规则，不得引入运行时多态（NG-09）。

## 12. 未决项

| 决策 | 内容 |
|---|---|
| DEC-05 | `constexpr` 与 `compile_time` 是否合并 |
| DEC-09 | 内联 `asm`（与 `compile_time` 正交，但共享目标查询） |
| CT-13 | 反射 API 精确签名与返回类型 |
