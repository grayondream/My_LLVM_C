# SafeModern C 1.0 — 求值语义与静态分析规范

> 对应 TODO：**SEM-13 ~ SEM-18**、**FUN-07/10/13/14**、**GEN-06**
> 状态：`[impl]` 以 `src/sema/SemanticAnalyzer.cpp`、`src/ast/Symbol.cpp`、`src/ast/*.cpp` 为准；`[plan]` 为目标。
> 总原则：UB 清单对齐 C 标准（SEM-13）；**不引入**所有权/生命周期模型（NG-04）。
> 旁注（I4 收口，2026-10-11）：`string` 拥有型值语义（深拷贝活值/移动新鲜值/赋值自动释放）
> 是**单类型特例**，不构成一般所有权模型；`string_view`（原 `str`）悬垂仍是用户责任、无
> use-after-move 诊断。详见 `docs/superpowers/specs/2026-10-11-string-ownership-design.md`。

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

1. 逐个候选：参数个数在 `[minArgs, paramCount]` 区间内（`minArgs = paramCount - defaultArgCount`，可变参数允许实参 ≥ minArgs）。
2. 逐位计算 `conversionRank`（只对**已提供**的实参；任一位 `< 0` 则该候选不可行）。
3. 累加 rank 取得分；可变参数候选额外 `+1`（非变参优先）。
4. 取最低分候选；并列时**偏好少用默认参数的候选**（`defaultArgCount` 更小）；仍并列 → **歧义**，报错（返回 `nullptr`）。

> 默认参数（FUN-09）：`T name = const-expr` 仅尾随参数；默认值须编译期常量；调用点缺位时由 sema 折叠并暂存，codegen 补参。方法（含 `this`）同样区间判定。

**目标**：将以下规则写清并测试（SEM-16）：
- 精确匹配 > 提升 > 转换的偏序；
- 模板实例候选参与（GEN-06）；
- 运算符重载与 `to_string` 的同一套解析（FUN-08/FUN-14）；
- 歧义诊断包含候选列表。

## 4. 运算符重载（SEM-17 / FUN-08）`[impl]`

- 可重载集合（声明侧与调用侧**已收敛一致**）：`+ - * / % == != < > <= >= && || & | ^ << >> ~ ! ++ -- [] ()`。
- 仅对 struct/union/class（及 Optional/Result）类型的操作数触发查找；自由函数形式为主
  （`T operator+(T a, T b)`），内建路径在无匹配时照旧诊断。
- 一元：`operator- / operator+ / operator~ / operator!` 单参；前缀 `operator++ / --` 单参，
  后缀 `operator++ / --` 追加 `int32` 哑参（对齐 C++ 约定）——调用点 `v++` 只提交哑参 `0`，
  返回旧值语义由实现者负责。
- `operator[]`：`T operator[](T obj, Idx i)`（下标类型由声明决定）。
- `operator()`：`T operator()(T obj, Args...)`；调用点 `obj(args)` 走窄 operator 路径
  （不承诺默认参数折叠 / 显式模板实参）。
- **已知边界**：调用点只解析**自由函数形式**；类内方法形式的运算符声明可解析，但
  `v.op(...)` 的 `this` 形态未接入调用点；类外运算符定义（`T C::operator[](...)`）未支持。
  `operator[]` 的结果是值（语言无引用），故 `v[i] = x` 不可用——需要写入时让运算符返回指针
  并显式解引用。
- **`&&` / `||` 重载后不做短路**（两个实参都求值，对齐 C++）；内建 `&&`/`||` 保持短路。
- 参数类型/返回类型不做额外约束；mangle 使用原始操作数类型（既有约定；隐式转换的调用点
  不在本轮泛化）。

## 5. 扩展点：`to_string` / `to_hash` / `equals`（FUN-14 / STD-09/26）`[impl → 待规范]`

- 现状 `[impl]`：`to_string` 支持成员方法与自由函数两条路径，通过 `OverloadSet` 解析（见 `SemanticAnalyzer.cpp:665`）。
- 目标 `[plan]`：冻结精确签名（如 `str to_string(T)` / `u64 to_hash(T)` / `bool equals(T,T)`）、查找顺序与命名空间规则；供容器与格式化统一使用（STD-07/09/26）。

## 5.5 类型别名（PAR-24）`[impl]`

- 唯一形式：`using Name = type-id;`。
- `type-id` 支持基础类型与限定符、指针 `T*`、切片 `T[]`、optional `T?`、固定数组 `T[N]`（可多维，维度须整数字面量）、抽象函数指针 `Ret (*)(params)`、模板实例，以及内联/命名 struct/union/enum 定义。
- 别名在顶层与 namespace 作用域可用；块内、class/struct/union 成员体内的别名声明被拒绝（报 “only allowed at namespace scope”）。
- `using E = enum { A, B };` 的枚举量在别名所在作用域可见。
- `[[deprecated]]` 折叠到别名，使用点告警；`[[repr(C)]]` 等非别名目标注解报错。
- `typedef` 与 `type` 已移除：出现即报硬错误并提示改用 `using`，无兼容模式。内部 `TypeKind::Typedef` 仍是别名的实现表示。

## 6. 静态检查清单（SEM-01 ~ SEM-14）

| 编号 | 检查 | 状态 |
|---|---|---|
| SEM-01/02 | 变量/指针未初始化 | `[impl]` W3001 definite-assignment（见下） |
| SEM-03 | `[[nonnull]]` 空指针 | `[plan]` |
| SEM-04 | `public/private/protected` 访问控制 | `[impl]` class 默认 private / struct 默认 public；private 仅定义类内；**protected 定义类与派生类内可访问**（INH-06，2026-10-19 放宽），类外 E2009 |
| SEM-05 | 表达式/赋值/调用/返回/字段类型检查 | `[impl]` 基础版 |
| SEM-06 | 泛型实例化检查 | `[plan]` |
| SEM-07 | `compile_time` 条件与死代码 | `[plan]` |
| SEM-08 | 格式字符串类型检查 | `[plan]` |
| SEM-09 | 数组/Slice 边界检查 | 策略见 DEC-06 |
| SEM-10 | 整数溢出检查 | 策略见 DEC-07 |
| SEM-11 | 未使用变量/不可达/弃用/枚举穷尽 | `[impl]` W3002/W3003（见 6.2）；W3004 弃用与枚举穷尽 `[plan]` |
| SEM-12 | 位域语义检查 | `[plan]` |
| SEM-13 | UB 清单（对齐 C） | `[plan]` 基准文档 |
| SEM-14 | 诊断：错误码/位置/修复建议 | `[impl]` 基础版（INF-03/13） |

### 6.1 确定赋值分析（SEM-01/02）`[impl]`

对每个函数体做保守的 definite-assignment 数据流分析，产出 `W3001`（经 `getWarnings()`，永不导致编译失败，除非 `-Werror`）：

- 参数、全局、带初始化器的局部变量视为已赋值；裸声明视为未赋值。
- 直接赋值 / 复合赋值 / `++`/`--` / 取址（假定可能被写）标记为已赋值。
- `if/else` 在汇合点取两条路径已赋值集合的**交集**；无 `else` 时以入口集合为 else 路径。
- `while`/`for` 循环体可能执行零次，出口取入口集合；`do-while` 体至少执行一次。
- `switch` 保守取入口集合；`?:` 对两分支取交集。
- 每个变量只报告一次。实现：`SemanticAnalyzer::checkInitialization`。

### 6.2 未使用变量与不可达代码（SEM-11）`[impl]`

- `W3002` **未使用变量**：局部变量/局部数组在 `exitScope` 时若从未被引用则告警；参数、全局变量、函数不检查。任一引用（含仅赋值、取址、读）都算“使用”，即 GCC `-Wunused-variable` 语义（set-but-not-read 不告警）。实现：`Symbol::checkUnused`/`isUsed` + `SemanticAnalyzer::exitScope`。
- `W3003` **不可达代码**：同一 `{}` 块中，`return`/`break`/`continue`（含两分支都转移的 `if/else`）之后的语句告警，每块至多一次；`defer` 语句不受影响（仍在作用域退出时执行）。实现：`stmtAlwaysTransfers` + `visit(CompoundStmtAST)`。
- `W3004` 弃用 API 与枚举穷尽性属 `[plan]`：前者依赖注解系统（ANN-05），后者依赖 **DEC-13**（警告还是错误）。
- 所有分析警告经 `getWarnings()`，不导致编译失败，除非 `-Werror`。

## 7. 编译期求值边界（CT-14 / DEC-05）

- `constexpr`（`[impl]`）与 `compile_time`（`[plan]`）的职责边界待定：是否合并、各自可访问的能力，见 **DEC-05**。
- `compile_time` 必须 **零运行时开销**（ACC-04），求值结果以 LLVM 常量落地（CT-12）。
- 沙箱：限制文件/网络/系统访问（CT-11）。

## 7.5 Lambda（PAR-11/CG-11/DEC-10）`[impl]`

- 语法 `[captures](params) -> Ret { body }`；捕获 `[]`/`[=]`/`[a,&b]`；返回类型显式或推断（body 首个 return 表达式类型，多 return 不兼容未处理）。
- **无捕获 → 函数指针**（零开销）：可赋给 `int32 (*)(int32)`、作回调实参（qsort）、直接调用。
- **有捕获 → 匿名闭包 struct + 调用函数**：按值捕获拷贝、按引用捕获存指针（body 内写穿原对象）。
- **DEC-10 裁决**：语言无 `auto`，有捕获闭包类型不可命名——实际可用形态为**立即调用** `[&a](){ ... }();`；捕获闭包不可存于变量。
- 设计：`docs/superpowers/specs/2026-10-17-lambda-design.md`。

## 7.6 字面量与模板值的存储语义（PAR-08 / GEN-02）`[impl]`

- **复合字面量 `(T){...}`**：块作用域为栈上临时对象（自动存储）；文件作用域为静态常量。
  **直接转义**（`&` 取址、`return` 该表达式、数组/slice 形式直赋全局）属悬垂，sema 拒绝；
  **by-value** 标量/struct 拷贝到全局允许；副本转义不追踪（用户责任）。
  `T[]` 是 slice：`(T[N]){...}` 为定长数组复合字面量，`(T[]){...}` 为 slice 复合字面量。
- **slice 字面量 `T[] s = {1,2,3};`**：底层为栈上匿名数组 + `{ptr,len}` 视图；长度 = 元素个数；
  `return s` 属悬垂转义，sema 拒绝。
- **前缀字符串字面量**：`u8/u16/u32"..."` 类型为 `char8/16/32[]`（逐字节存储，不做 Unicode
  转码），可退化为对应指针；无前缀仍为 `string_view`。
- **非类型模板参数作为值**：实例化时把表达式中的参数名替换为整型常量（`return N`、算术、
  数组维度均可用）。模板体内声明与值参数同名的局部变量/形参将被拒绝（防 name-only 替换误捕获）。

## 8. 参考实现位置

- 语义检查：`src/sema/SemanticAnalyzer.cpp`
- 符号/重载：`src/ast/Symbol.cpp`（`conversionRank` / `OverloadSet::resolve`）
- defer：`src/codegen/CodegenContext.cpp`（`emitDefersFrom` / `popDeferScope`）、`src/ast/Stmt.cpp`
- 表达式 codegen：`src/ast/Expr.cpp`
