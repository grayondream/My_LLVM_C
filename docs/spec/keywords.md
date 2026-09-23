# SafeModern C 1.0 — 关键字 / 保留字 / 运算符总表

> 对应 TODO：**INF-11**、**LEX-14**、**DEC-18**
> 状态图例：`[impl]` 已实现（见 `src/frontend/Lexer.cpp` 关键字表）｜`[plan]` 规划新增｜`[remove]` 计划移除/废弃｜`[decide]` 待决策
> 本表是词法层唯一权威关键字清单；`docs/spec/grammar.ebnf` 的终结符须与本表一致。

## 0. 现状说明

- 关键字目前通过 `src/frontend/Lexer.cpp` 中的静态 map 识别；`src/frontend/Token.h` 定义 token 枚举。
- `using`、`type`、`protected` 当前**不是**关键字，而是把标识符文本当作上下文关键字处理（见 `Parser::parseDeclaration`）——本表统一登记为“上下文关键字”。
- `namespace`、`template`、`typename`、`this`、`asm`、`thread_local`、`static_cast`、`reinterpret_cast` **尚无**对应 token，属规划项。

## 1. 数据类型关键字

| 关键字 | 状态 | 说明 |
|---|---|---|
| `void` | `[impl]` | |
| `bool` | `[impl]` | |
| `char` | `[impl]` | token `TOKEN_CHAR_KW` |
| `int` / `float` / `double` | `[impl]` | 平台默认宽度 |
| `int8` `int16` `int32` `int64` `int128` | `[impl]` | 有符号定宽 |
| `uint8` `uint16` `uint32` `uint64` `uint128` | `[impl]` | 无符号定宽 |
| `isize` / `usize` | `[impl]` | 平台指针宽度（TYP-03） |
| `float32` / `float64` | `[impl]` | |
| `f16` / `f32` / `f64` / `f128` | `[plan]` | 目标浮点类型，见 TYP-04；同时作为字面量后缀（LEX-15） |
| `str` / `String` | 非关键字 | 标准库类型（FMT-01/02），不在词法层保留 |

## 2. 存储类 / 限定符

| 关键字 | 状态 | 说明 |
|---|---|---|
| `const` | `[impl]` | |
| `constexpr` | `[impl]` | 与 `compile_time` 关系见 DEC-05 |
| `static` | `[impl]` | |
| `extern` | `[impl]` | C 链接（MOD-09） |
| `volatile` | `[impl]` | |
| `restrict` | `[impl]` | 语义见 MEM-04 |
| `inline` | `[impl]` | |
| `register` | `[remove]` | 无实际语义，建议废弃，见 DEC-18 |
| `thread_local` | `[plan]` | 存储类，见 CON-04 / DEC-12 |
| `atomic` | `[decide]` | 限定符还是类型构造器见 DEC-11 |

## 3. 控制流

| 关键字 | 状态 | 说明 |
|---|---|---|
| `if` `else` | `[impl]` | |
| `for` `while` `do` | `[impl]` | |
| `switch` `case` `default` | `[impl]` | 允许贯穿 |
| `break` `continue` | `[impl]` | |
| `return` | `[impl]` | 目标允许 `return;`（当前要求表达式） |
| `defer` | `[impl]` | 语义待冻结，见 SEM-18 / FUN-10 |
| `goto` | `[remove]` | 连同标签语句移除，见 NG-01 |

## 4. 类型构造 / 声明

| 关键字 | 状态 | 说明 |
|---|---|---|
| `struct` `union` `enum` `class` | `[impl]` | |
| `typedef` | `[impl]` | 别名规范形式见 DEC-16 |
| `operator` | `[impl]` | 可重载集合见 SEM-17 |
| `template` | `[plan]` | GEN-01 / PAR-21 |
| `typename` | `[plan]` | GEN-01 |
| `this` | `[plan]` | CRTP 必需，PAR-17 / INH-05 |
| `using` | `[impl]` 上下文 | 类型别名 `using X = T;` |
| `type` | `[impl]` 上下文 | distinct 别名 `type X = T;` |
| `generic` | `[remove]` | 未使用的旧泛型关键字，弃用（LEX-02） |

## 5. 模块 / 可见性

| 关键字 | 状态 | 说明 |
|---|---|---|
| `module` | `[impl]` | |
| `import` | `[impl]` | 访问语法见 DEC-02 / MOD-11 |
| `export` | `[impl]` | |
| `public` | `[impl]` | |
| `private` | `[impl]` | |
| `protected` | `[plan]` | 目前为上下文关键字；继承访问控制 INH-01 / PAR-04 |
| `namespace` | `[plan]` | **缺失**；`src/libsafec` 已使用，见 PAR-22 / MOD-12 |

## 6. 编译期 / 反射

| 关键字 | 状态 | 说明 |
|---|---|---|
| `constexpr` | `[impl]` | 见 §2 |
| `true` / `false` | `[impl]` | 布尔字面量 |
| `null` | `[impl]` | 空字面量 |
| `comptime` | `[remove]` | 统一并入 `compile_time`，弃用旧名（CT-01 / NG-12） |
| `compile_time` | `[plan]` | 编译期命名空间（非保留字，按标识符处理即可） |
| `static_assert` | `[plan]` | 作为 `compile_time.static_assert`，**不设独立关键字**（NG-12） |
| `sizeof` | `[impl]` | 一元运算符 |
| `alignof` / `offsetof` | `[impl]` token | 语义/语法完善见 PAR-14 |
| `typeof` | `[decide]` | 既有 token，去留见 DEC-18 |
| `cast` | `[decide]` | 既有 token，去留见 DEC-18 |
| `type_info` | `[remove]` | 由 `compile_time` 反射取代（NG-12） |

## 7. 泛型 / 转换 / 汇编

| 关键字 | 状态 | 说明 |
|---|---|---|
| `static_cast` | `[plan]` | LEX-11 / PAR-18 |
| `reinterpret_cast` | `[plan]` | LEX-11 / PAR-18 |
| `asm` | `[plan]` | PAR-16 / CG-10 / DEC-09 |

## 8. 运算符与标点（token）

| 记号 | 含义 | 状态 |
|---|---|---|
| `+ - * / %` | 算术 / 一元 / 指针 | `[impl]` |
| `= += -= *= /= %=` | 赋值 | `[impl]` |
| `== != < > <= >=` | 比较 | `[impl]` |
| `&& \|\| !` | 逻辑 | `[impl]` |
| `& \| ^ ~` | 位运算 / 取地址 | `[impl]` |
| `<< >>` | 移位 | `[impl]` |
| `&= \|= ^= <<= >>=` | 复合赋值 | `[impl]` |
| `++ --` | 自增自减 | `[impl]` |
| `? :` | 条件运算符 | `[impl]` |
| `::` | 限定名 | `[plan]` 待补 token（PAR-22 / BASE-02） |
| `. ->` | 成员访问 | `[impl]` |
| `( ) [ ] { }` | 分组 / 下标 / 初始化 | `[impl]` |
| `, ;` | 分隔 | `[impl]` |
| `...` | 可变参数 | `[impl]` |
| `#` | 预处理器残留 | `[remove]` 随 NG-02 移除（Base-02/BASE-07 保留至清理完成） |
| `[[` `]]` | 注解定界 | `[plan]` ANN-01 |

## 9. 保留策略

1. 本表为**唯一权威**；新增关键字需同步更新 `Lexer.cpp`、`Token.h` 与本文件。
2. 不以关键字形式复用的标识符：`str`、`String`、`Array` 等标准库类型名保持普通标识符。
3. 上下文关键字（`using`/`type`/`protected`/`compile_time`）在引入二义性前不改为一等关键字，见 DEC-16。
4. `[decide]` 项（`register`/`cast`/`typeof`）由 DEC-18 统一裁决并回填本表状态。
