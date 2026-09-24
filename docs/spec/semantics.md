# SafeModern C 1.0 — 求值语义与静态分析规范

> 对应 TODO：**SEM-13 ~ SEM-18**、**FUN-07/10/13/14**、**GEN-06**
> 状态：`[impl]` 以 `src/sema/SemanticAnalyzer.cpp`、`src/ast/Symbol.cpp`、`src/ast/*.cpp` 为准；`[plan]` 为目标。
> 总原则：UB 清单对齐 C 标准（SEM-13）；**不引入**所有权/生命周期模型（NG-04）。

## 1. 求值顺序与序列点（SEM-15）`[plan]`

- 现状 `[impl]`：codegen 按语法顺序**从左到右**求值；二元运算先左后右；函数实参从左到右。这是确定的*实现*行为，但尚未作为规范冻结。
- 目标 `[plan]`：与 C 标准一致（表达式/函数实参求值顺序为**未指定**，不得依赖）；同一标量对象的多次无序列修改为 UB。
- 序列点/UB 清单由 SEM-13 冻结，并由 ASan/UBSan 用例验证（TST-06/ACC-07）。

## 2. `defer` 语义（SEM-18 / FUN-10）`[impl → 待冻结]`

当前实现（`src/codegen/CodegenContext.cpp` / `src/ast/Stmt.cpp`）：

1. `defer expr;` 在执行到该语句时**注册**（按作用域保存）。
2. 作用域退出时按 **innermost scope first（后进先出）** 执行该作用域内所有 `defer`。
3. `return` / `break` / `continue` 会先执行到对应边界为止的待定 `defer`，再跳转（`emitDefersFrom`）。
   - `break`/`continue` 只运行到循环体作用域边界，不运行外层作用域的 defer。
4. `return` 时：先求值返回表达式，再执行所有待定 `defer`，最后 `ret`。

> 注意：这是**作用域级** defer（类似 Go，但在块退出即触发），不是函数级。该语义必须在 SEM-18 冻结并补测试，示例与边界用例同步写入测试。

## 3. 重载解析（SEM-16 / FUN-07/13）`[impl → 待规范]`

`OverloadSet::resolve`（`src/ast/Symbol.cpp`）：

1. 逐个候选：参数个数必须匹配（可变参数允许实参 ≥ 固定参数数）。
2. 逐位计算 `conversionRank`，任一位 `< 0` 则该候选不可行。
3. 累加 rank 取得分；可变参数候选额外 `+1`（非变参优先）。
4. 取最低分候选；若最低分并列 → **歧义**，报错（返回 `nullptr`）。

**目标**：将以下规则写清并测试（SEM-16）：
- 精确匹配 > 提升 > 转换的偏序；
- 模板实例候选参与（GEN-06）；
- 运算符重载与 `to_string` 的同一套解析（FUN-08/FUN-14）；
- 歧义诊断包含候选列表。

## 4. 运算符重载（SEM-17 / FUN-08）`[impl → 待规范]`

- 可重载集合（当前解析支持，`parseDeclarationAsType`）：`+ - * / % == != < > <= >= && || & | ^ << >> ~ ++ --`。
- 仅对 struct/union（及 class）类型的操作数触发查找（`SemanticAnalyzer` 二元/比较分支）。
- **目标**：明确可重载运算符的完整集合、参数/返回约束、与内建运算符的优先级/结合性共用关系。

## 5. 扩展点：`to_string` / `to_hash` / `equals`（FUN-14 / STD-09/26）`[impl → 待规范]`

- 现状 `[impl]`：`to_string` 支持成员方法与自由函数两条路径，通过 `OverloadSet` 解析（见 `SemanticAnalyzer.cpp:665`）。
- 目标 `[plan]`：冻结精确签名（如 `str to_string(T)` / `u64 to_hash(T)` / `bool equals(T,T)`）、查找顺序与命名空间规则；供容器与格式化统一使用（STD-07/09/26）。

## 6. 静态检查清单（SEM-01 ~ SEM-14）

| 编号 | 检查 | 状态 |
|---|---|---|
| SEM-01/02 | 变量/指针未初始化 | `[impl]` W3001 definite-assignment（见下） |
| SEM-03 | `[[nonnull]]` 空指针 | `[plan]` |
| SEM-04 | `public/private/protected` 访问控制 | `[plan]` |
| SEM-05 | 表达式/赋值/调用/返回/字段类型检查 | `[impl]` 基础版 |
| SEM-06 | 泛型实例化检查 | `[plan]` |
| SEM-07 | `compile_time` 条件与死代码 | `[plan]` |
| SEM-08 | 格式字符串类型检查 | `[plan]` |
| SEM-09 | 数组/Slice 边界检查 | 策略见 DEC-06 |
| SEM-10 | 整数溢出检查 | 策略见 DEC-07 |
| SEM-11 | 未使用变量/不可达/弃用/枚举穷尽 | 部分 `[impl]`（未初始化 W3001）；其余 `[plan]` |

### 6.1 确定赋值分析（SEM-01/02）`[impl]`

对每个函数体做保守的 definite-assignment 数据流分析，产出 `W3001`（经 `getWarnings()`，永不导致编译失败，除非 `-Werror`）：

- 参数、全局、带初始化器的局部变量视为已赋值；裸声明视为未赋值。
- 直接赋值 / 复合赋值 / `++`/`--` / 取址（假定可能被写）标记为已赋值。
- `if/else` 在汇合点取两条路径已赋值集合的**交集**；无 `else` 时以入口集合为 else 路径。
- `while`/`for` 循环体可能执行零次，出口取入口集合；`do-while` 体至少执行一次。
- `switch` 保守取入口集合；`?:` 对两分支取交集。
- 每个变量只报告一次。实现：`SemanticAnalyzer::checkInitialization`。
| SEM-12 | 位域语义检查 | `[plan]` |
| SEM-13 | UB 清单（对齐 C） | `[plan]` 基准文档 |
| SEM-14 | 诊断：错误码/位置/修复建议 | `[impl]` 基础版（INF-03/13） |

## 7. 编译期求值边界（CT-14 / DEC-05）

- `constexpr`（`[impl]`）与 `compile_time`（`[plan]`）的职责边界待定：是否合并、各自可访问的能力，见 **DEC-05**。
- `compile_time` 必须 **零运行时开销**（ACC-04），求值结果以 LLVM 常量落地（CT-12）。
- 沙箱：限制文件/网络/系统访问（CT-11）。

## 8. 参考实现位置

- 语义检查：`src/sema/SemanticAnalyzer.cpp`
- 符号/重载：`src/ast/Symbol.cpp`（`conversionRank` / `OverloadSet::resolve`）
- defer：`src/codegen/CodegenContext.cpp`（`emitDefersFrom` / `popDeferScope`）、`src/ast/Stmt.cpp`
- 表达式 codegen：`src/ast/Expr.cpp`
