# My_LLVM_C / SafeModern C 1.0 详细待办（工程 backlog）

> 本文件是唯一的需求与进度追踪清单。每条以稳定 ID 编号，便于 issue/commit 引用。
>
> - **状态**：`[ ]` 未开始 ｜ `[~]` 进行中 ｜ `[x]` 已完成 ｜ `[!]` 阻塞
> - **标记**：`(已实现)` = 代码已有但需补测试/清理；`(新)` = 本轮审计新增；`(改)` = 需修订既有条目
> - 提交时在 message 中引用 ID，例如 `feat(LEX-04): ...`
> - 所有特性必须映射到可预测的 C 内存模型与 ABI；无隐藏分配/控制流；comptime 无运行时开销

---

## 0. 现状基线（已完成，勿重复实现）

- `[x]` **BASE-01** 基于 LLVM（>= 18，实测 22.1.8）的 C 风格编译器；CMake 构建、CTest 测试。
- `[x]` **BASE-02** 词法：标识符/数字/字符串/字符/运算符/关键字；整型 `int8..int128/uint8..uint128/isize/usize`，浮点 `float/float32/float64`；`#`/`...`/`::`(待补) 等符号。
- `[x]` **BASE-03** 语法：函数、变量、数组、struct/class/union/enum、typedef、`sizeof`、初始化列表、三元、逗号、赋值复合运算符、前缀/后缀 `++/--`、成员访问 `.`/`->`、下标、方法调用。
- `[x]` **BASE-04** 控制流：if/else、while、do-while、for、switch/case/default（含贯穿）、break、continue、return、goto/label、defer。
- `[x]` **BASE-05** 语义：作用域/符号表、函数重载（`OverloadSet`）、运算符重载（`operator`）、左值/右值、隐式类型转换（部分）、常量折叠 `constexpr`、`print/println` 内建与 `to_string` 分派。
- `[x]` **BASE-06** 代码生成：基础类型/指针/数组/struct/class/union/enum/函数指针、全局变量、字符串字面量、指针算术（GEP）、负浮点（fneg）、非 void 函数补 `unreachable`。
- `[x]` **BASE-07** 驱动：`-c/-o/-S/-E/-I/-D/-O/-g/-v/-Wall/-Werror/-std/-fsyntax-only/-l/-L`、JIT、`cc` 链接系统 libc。
- `[x]` **BASE-08** 自带标准库 **libsafec**（`src/libsafec/`，`namespace safec`，独立目标 `safec` → `build/lib/libsafec.a`）：`printf/sprintf`、`malloc/free/calloc/realloc`、`strlen/strcmp/strcpy/memcpy/memset/memcmp`。
- `[x]` **BASE-09** `resources/main.c` 可完整编译运行。全量 **528** 测试通过。

---

## 1. 基础设施与工程（INF）

- `[ ]` **INF-01** 锁定 LLVM 版本与集成层：CMake `find_package(LLVM)`、最低版本断言、版本不匹配诊断。
- `[ ]` **INF-02** 构建系统：Debug/Release、跨平台（Linux/macOS/Windows）、交叉编译目标配置。
- `[ ]` **INF-03** **诊断系统**：统一 `Diagnostic`（源码位置/错误码/严重级别/修复建议）、诊断快照测试设施。
- `[ ]` **INF-04** 测试框架完善：单元、集成、黄金文件（IR/输出）、诊断快照；CTest 分组与标签。
- `[ ]` **INF-05** CI：Linux/macOS/Windows 矩阵、交叉编译、ABI 测试、性能基准。（外围仅限构建/测试，不含 LSP/包管理）
- `[ ]` **INF-06** 文档系统：语言规范（EBNF 语法）、类型/ABI 文档、`compile_time` API 文档。
- `[ ]` **INF-07** 可复现构建：确定性输出、路径无关、固定优化流程。
- `[ ]` **INF-08** 最小命令行驱动保留：`smc check/build/run/test`（不含 fmt/doc/lsp，见 Non-goals）。

---

## 2. 词法分析（LEX）

- `[ ]` **LEX-01** `(已实现)` 关键字与基础类型关键字词法；核对与规范一致。
- `[ ]` **LEX-02** `(新)` 新增关键字 token：`template`、`typename`、`this`；弃用未使用的 `generic`。
- `[ ]` **LEX-03** `(改)` 移除/废弃：C 预处理器与宏相关 token（`#include/#define/#if`）与 `-E/-I/-D`（见 Non-goals）。
- `[ ]` **LEX-04** `(已实现)` 整型字面量：十进制/十六进制/二进制/八进制/分隔符/后缀；补全测试。
- `[ ]` **LEX-05** `(已实现)` 浮点字面量：小数/指数/`f16/f32/f64/f128` 后缀；补全测试。
- `[ ]` **LEX-06** `(已实现)` 字符与字符串字面量：转义、UTF-8；补原始字符串/多行字符串。
- `[ ]` **LEX-07** `(已实现)` 注释：行注释/块注释；补文档注释。
- `[ ]` **LEX-08** `(改)` 泛型 `template<...>` 与比较运算符 `<`、`>>` 拆分的歧义消解（为 GEN 铺路）。
- `[ ]` **LEX-09** Slice 语法 `T[]` 的 token 与解析支持（当前已有 `SliceType`，核对）。
- `[ ]` **LEX-10** 注解 token：`[[repr(C)]]/[[packed]]/[[align(64)]]/[[inline]]/[[cold]]/[[nonnull]]/[[deprecated]]`。
- `[ ]` **LEX-11** `static_cast<T>(x)` / `reinterpret_cast<T>(x)` 等转换关键字（为 INH/CRTP 铺路）。
- `[ ]` **LEX-12** 内联汇编 `asm` token。
- `[ ]` **LEX-13** 错误恢复与词法诊断（非法字符、未闭合字面量、整型溢出）。

---

## 3. 语法分析与 AST（PAR）

- `[ ]` **PAR-01** `(已实现)` 顶层声明：变量/函数/struct/class/enum/union/`typedef`/`using`/`type`。
- `[ ]` **PAR-02** `(改)` 函数声明与定义：**C 风格**，不使用 `fn`；覆盖 `extern/static/inline`、成员函数。
- `[ ]` **PAR-03** `(已实现)` 方法定义语法 `Vector::length()`；补嵌套/`static` 成员。
- `[ ]` **PAR-04** class 成员：`public/private/protected` 段、成员变量/函数、`static` 成员、嵌套类型。
- `[ ]` **PAR-05** 类型语法：基础类型、指针 `T*`、数组 `T[N]`、Slice `T[]`、模板实例 `Box<i32>`。
- `[ ]` **PAR-06** `(新)` 指针限定符：`const/volatile/restrict/atomic`。
- `[ ]` **PAR-07** `(新)` 注解挂载点：类型/字段/函数/参数/变量/模块。
- `[ ]` **PAR-08** `(已实现)` 表达式：字面量/标识符/调用/成员访问/下标/一元/二元/三元/强转；补切片表达式。
- `[ ]` **PAR-09** `(改)` 语句：块、if/else、while、do-while、for、switch/case、return、break、continue、声明。（**不含 goto/label**，见 Non-goals）
- `[ ]` **PAR-10** `(新)` 位域声明：`T name : N;`（struct/union 成员），含零宽位域。
- `[ ]` **PAR-11** `(新)` 简单 lambda：语法（建议 C++ 风格 `[capture](params) -> T { ... }` 或最简 `[](p){...}`）、捕获列表。
- `[ ]` **PAR-12** `(新)` 匿名 class/struct/union：匿名类型定义、匿名成员提升。
- `[ ]` **PAR-13** `(新)` 指定初始化器 `{.field = v}`、柔性数组 `T a[]`。
- `[ ]` **PAR-14** `(已实现)` `sizeof`；`(新)` `alignof`、`offsetof`。
- `[ ]` **PAR-15** `(新)` `compile_time` 表达式：`static_assert`、`if`、目标/构建查询、反射（取代 `type_info`）。
- `[ ]` **PAR-16** `(新)` 内联汇编 `asm` 语句/表达式。
- `[ ]` **PAR-17** `(新)` `this` 表达式（CRTP 必需）。
- `[ ]` **PAR-18** `(新)` `static_cast<T>(x)` / C 风格强转；向下转换（CRTP 必需）。
- `[ ]` **PAR-19** AST 节点带源码位置/属性/注释/文档；AST 序列化（增量编译缓存）。
- `[ ]` **PAR-20** 语法错误恢复与高质量诊断。
- `[ ]` **PAR-21** `(新)` 模板声明语法 `template<typename T>` / `template<typename T, usize N>`（函数/类/别名，见 GEN）。

---

## 4. 名称解析与模块系统（MOD）

- `[ ]` **MOD-01** 符号表：全局/模块/类型/函数/变量/字段/方法。
- `[ ]` **MOD-02** 作用域规则：块/函数/类型/模块。
- `[ ]` **MOD-03** 前向声明：函数/struct/class/enum/union/类型别名。
- `[ ]` **MOD-04** `module` 声明与文件映射。
- `[ ]` **MOD-05** `export` 导出规则；`import` 导入规则与访问语法（决策见 DEC-02）。
- `[ ]` **MOD-06** 模块可见性与 `public/private`。
- `[ ]` **MOD-07** 模块循环依赖检测。
- `[ ]` **MOD-08** 名称修饰：C ABI 符号、class 方法、**模板单态化符号**、`static` 成员。
- `[ ]` **MOD-09** `(改)` **C 互操作（无预处理器方案）**：不解析真实 C 头文件，改用 `extern` 声明 + 内置 `std.c` 绑定层（**关键缺口**，见 INF/NOTES）。
- `[ ]` **MOD-10** 增量编译：模块/AST/类型/IR 缓存与失效策略（内容哈希）。
- `[ ]` **MOD-11** 导入符号访问语法待定并实现（`math.add` 还是直接 `add`）。

---

## 5. 类型系统（TYP）

- `[ ]` **TYP-01** 基础类型大小/对齐表：按目标平台生成。
- `[ ]` **TYP-02** 整数类型：`i8..i128`、`u8..u128`。
- `[ ]` **TYP-03** 平台整数：`isize/usize` 与目标指针宽度绑定。
- `[ ]` **TYP-04** 浮点类型：`f16/f32/f64/f128` 的 LLVM 映射与软件兜底。
- `[ ]` **TYP-05** `bool/char/char8/char16/char32/void`。
- `[ ]` **TYP-06** 类型别名 `type Handle = u64;`：展开、循环检测。
- `[ ]` **TYP-07** `struct`：字段、布局、默认公开、C ABI 兼容。
- `[ ]` **TYP-08** `class`：布局等价 struct，成员函数不占空间。
- `[ ]` **TYP-09** `enum` 强类型：底层类型 `:u8`、固定大小、不隐式转换。
- `[ ]` **TYP-10** `union`：C 风格布局、初始化、访问。
- `[ ]` **TYP-11** 固定数组 `i32[32]`；VLA（可变长度数组）；多维数组。
- `[ ]` **TYP-12** `Slice`：`{ptr, length}`，不拥有、零拷贝；数组↔Slice 退化规则。
- `[ ]` **TYP-13** `Optional<T>`：`{ bool valid; T value; }`；显式访问（**无 `?` 传播算子**）。
- `[ ]` **TYP-14** `Result<T,E>`：布局与 `.error`/`.value` 语义；显式访问。
- `[ ]` **TYP-15** 指针类型：`T*`、函数指针、多级指针。
- `[ ]` **TYP-16** 限定符类型：`const/volatile/restrict/atomic`。
- `[ ]` **TYP-17** 函数类型：参数/返回/调用约定/可变参数。
- `[ ]` **TYP-18** `(新)` 模板实例类型 `Box<i32>`：实例化、缓存、去重（见 GEN）。
- `[ ]` **TYP-19** 类型相等/兼容/隐式转换/显式转换规则（含常规算术转换）。
- `[ ]` **TYP-20** 枚举与整数必须显式转换。
- `[ ]` **TYP-21** ABI 类型检查：`[[repr(C)]]` 下布局可预测；位域布局规则（见 DEC-08）。

---

## 6. 泛型与模板（GEN）〔新〕

> 语法**对齐 C++ 简单泛型**；支持函数模板、类模板、非类型参数、CRTP；单态化展开，零运行时开销。

- `[ ]` **GEN-01** 声明语法：`template<typename T>` 函数模板、类模板、类型别名；`typename` 关键字。
- `[ ]` **GEN-02** 非类型（常量）模板参数：`template<typename T, usize N>`；用于数组长度/`sizeof`/`compile_time`。
- `[ ]` **GEN-03** 实例化：显式 `max<int>(3,4)`、`Box<i32>`；实参推导 `max(3,4)`。
- `[ ]` **GEN-04** 解析歧义：`<`/`>` 比较、`>>` 拆分（配合 LEX-08）。
- `[ ]` **GEN-05** 单态化：编译期展开、实例去重、缓存、代码膨胀控制。
- `[ ]` **GEN-06** 约束策略：**无 trait**，定义处不检查；每个实例化点各自类型检查（依赖运算符重载与 `to_string/to_hash/equals` 扩展点）。
- `[ ]` **GEN-07** 符号修饰与 ABI：导出模板实例的命名规则。
- `[ ]` **GEN-08** 模板成员方法解析与 `this` 传递。
- `[ ]` **GEN-09** CRTP 支撑：见 INH-05（`this`、模板基类、延迟实例化、`static_cast`）。
- `[ ]` **GEN-10** **不做**（Non-goals）：特化/偏特化、SFINAE、可变参数模板、模板模板参数、concepts、默认模板实参。

---

## 7. 语义检查与静态分析（SEM）

- `[ ]` **SEM-01** 变量未初始化检查：数据流分析、分支合并、循环。
- `[ ]` **SEM-02** 指针未初始化检查；解引用前必须赋值。
- `[ ]` **SEM-03** 指针对空性检查：`[[nonnull]]`、可选运行时非空断言。
- `[ ]` **SEM-04** 访问控制检查：`public/private/protected`（单继承链）。
- `[ ]` **SEM-05** 类型检查：表达式/赋值/调用/返回/字段访问。
- `[ ]` **SEM-06** 泛型实例化检查（GEN-06）。
- `[ ]` **SEM-07** `compile_time` 条件求值与死代码消除。
- `[ ]` **SEM-08** 格式字符串类型检查：`{}`、`{:x}`、`{:f}`、`{:02}`、`{:.2f}`（`print/println`）。
- `[ ]` **SEM-09** 数组/Slice 边界检查策略：静态可证明或运行时检查（见 DEC-06）。
- `[ ]` **SEM-10** 整数溢出检查策略：Debug 检查 / Release 行为（见 DEC-07）。
- `[ ]` **SEM-11** 警告：未使用变量、不可达代码、弃用 API、enum 穷尽性。
- `[ ]` **SEM-12** 位域语义检查：宽度合法、跨存储单元规则。
- `[ ]` **SEM-13** UB 清单（**对齐 C 标准**）：为 SEM-09/10/11 与 UBSan 提供依据；不引入所有权/生命周期模型。
- `[ ]` **SEM-14** 静态分析诊断：错误码、源码位置、修复建议。

---

## 8. 预定义类型结构体/类/枚举/联合体（AGG）

### struct
- `[ ]` **AGG-01** 声明、字段、初始化、赋值、嵌套、前向声明。
- `[ ]` **AGG-02** 默认公开；无隐藏数据；C ABI 兼容布局。
- `[ ]` **AGG-03** `(新)` 匿名 struct/union 成员与成员提升。
- `[ ]` **AGG-04** `(新)` 柔性数组 `T a[]`。
- `[ ]` **AGG-05** `(新)` 指定初始化器 `{.x=1}`。
- `[ ]` **AGG-06** `(新)` 位域字段布局与 codegen。

### class
- `[ ]` **AGG-07** `class` 声明与 `public/private/protected` 段。
- `[ ]` **AGG-08** 成员变量、成员函数声明/实现、隐式 `self`。
- `[ ]` **AGG-09** 成员函数编译为 `Vector_length(Vector* self)`；方法调用 `v.length()`。
- `[ ]` **AGG-10** `static` 成员函数（无 `self`）与 `static` 成员变量。
- `[ ]` **AGG-11** 嵌套类型、前向声明。
- `[ ]` **AGG-12** 不生成 vtable/构造/析构/GC；生命周期由用户控制（栈/`malloc`/`free`）。
- `[ ]` **AGG-13** 布局等价 struct；`[[repr(C)]]` 下 ABI 稳定。

### enum
- `[ ]` **AGG-14** 底层类型 `:u8` 等；值/作用域/名称解析。
- `[ ]` **AGG-15** 不隐式转换整数；显式转换语法与实现。
- `[ ]` **AGG-16** 固定大小与 ABI；`(新)` 穷尽性检查（配合 SEM-11）。

### union
- `[ ]` **AGG-17** 声明、字段、布局、初始化与访问；不跟踪活跃成员（C 风格）。

---

## 9. 继承与 CRTP（INH）〔新〕

- `[ ]` **INH-01** **单继承**：`struct D : public B`；`public/private/protected` 说明符。
- `[ ]` **INH-02** **多继承不支持**：`struct D : B, C` 给出明确诊断。
- `[ ]` **INH-03** 继承布局：基类子对象为派生类第一个字段（偏移 0）；`[[repr(C)]]` 稳定。
- `[ ]` **INH-04** 转换：派生→基隐式；基→派生需 `static_cast`。
- `[ ]` **INH-05** **CRTP 必需项**：
  - `this` 关键字与类型（PAR-17）；
  - 基类支持模板实例 `: Shape<Circle>`（现 `baseClass` 仅为 `string`，需改为类型化）；
  - `Shape<Circle>` 的**延迟实例化**（`Circle` 未定义完即可实例化）；
  - `static_cast<Derived*>(this)` 向下转换（PAR-18）。
- `[ ]` **INH-06** 成员/方法沿单继承链查找；重写（非虚，静默遮蔽规则）。
- `[ ]` **INH-07** **不做**（Non-goals）：多继承、虚继承、菱形继承、虚函数/vtable/RTTI。

---

## 10. 函数（FUN）

- `[ ]` **FUN-01** 普通函数：声明/定义/调用/递归。
- `[ ]` **FUN-02** 参数传递：值/指针/Slice/模板参数。
- `[ ]` **FUN-03** 返回值：基础类型/struct/class/指针/Slice/Optional/Result。
- `[ ]` **FUN-04** `inline` / `extern` / `static` 函数。
- `[ ]` **FUN-05** `(已实现)` 函数指针：声明/赋值/调用/回调；补测试。
- `[ ]` **FUN-06** 可变参数：与 C varargs 兼容。
- `[ ]` **FUN-07** `(已实现)` 函数重载；补文档与测试。
- `[ ]` **FUN-08** `(已实现)` 运算符重载 `operator`；补文档与测试。
- `[ ]` **FUN-09** `(新)` 默认参数（**不做**命名参数，见 DEC-04）。
- `[ ]` **FUN-10** `(已实现)` `defer`；补测试与文档。
- `[ ]` **FUN-11** `(已实现)` `constexpr` 函数/常量；与 `compile_time` 关系待定（DEC-05）。

---

## 11. 指针、内存与 ABI（MEM）

- `[ ]` **MEM-01** `(已实现)` 完整 C 指针：解引用、取地址、指针运算、指针比较；补测试。
- `[ ]` **MEM-02** 地址转换：整数↔指针、不同指针类型显式转换。
- `[ ]` **MEM-03** `(已实现)` 函数指针；补回调与 ABI 测试。
- `[ ]` **MEM-04** 限定符：`const/volatile/restrict/atomic` 语义。
- `[ ]` **MEM-05** `[[nonnull]]`：调用处检查、函数内假设。
- `[ ]` **MEM-06** `malloc/calloc/realloc/free` 声明与标准库包装（libsafec）。
- `[ ]` **MEM-07** 预留：allocator 接口、arena、pool、placement 分配。
- `[ ]` **MEM-08** 无隐藏分配；无隐藏控制流（不插入异常/GC/析构）。
- `[ ]` **MEM-09** ABI：`[[repr(C)]]`、`[[packed]]`、`[[align(64)]]`；位域布局（DEC-08）。
- `[ ]` **MEM-10** FFI：与 C 函数/结构体/回调互操作（绑定层见 MOD-09）。
- `[ ]` **MEM-11** 调用约定：C 调用约定（可选其他）。
- `[ ]` **MEM-12** 名称修饰（配合 MOD-08）。
- `[ ]` **MEM-13** `(新)` freestanding / 无 libc 目标（可选）。

---

## 12. 编译期与反射（CT）〔新〕

> 以 `compile_time` 命名空间统一承载；**不再使用 `type_info(T)` 与独立 `static_assert` 关键字**。

- `[ ]` **CT-01** `compile_time` 语法入口：`comptime` 关键字统一并入 `compile_time`（弃用旧名）。
- `[ ]` **CT-02** `compile_time.static_assert(cond, msg)`：编译期断言，失败带源码位置诊断。
- `[ ]` **CT-03** `compile_time.if(cond) { ... }`：条件编译与死代码消除。
- `[ ]` **CT-04** 目标查询：`compile_time.target.os/arch/cpu`。
- `[ ]` **CT-05** 构建查询：`compile_time.build.debug` 等。
- `[ ]` **CT-06** 编译期求值器：解释 AST、常量折叠、字符串比较/整数运算/布尔逻辑。
- `[ ]` **CT-07** 反射 API（取代 `type_info`）：类型名、大小、对齐、字段、偏移、属性。
- `[ ]` **CT-08** 类型作为值：编译期与类型系统交互。
- `[ ]` **CT-09** 编译期缓存：求值结果缓存、增量编译。
- `[ ]` **CT-10** 编译期错误诊断：位置、求值栈、原因。
- `[ ]` **CT-11** 编译期沙箱：限制文件/网络/系统访问。
- `[ ]` **CT-12** 与 LLVM 常量集成。

---

## 13. 注解系统（ANN）

- `[ ]` **ANN-01** 注解语法 `[[attribute]]`、解析与 AST 挂载。
- `[ ]` **ANN-02** `[[repr(C)]]`：布局与 ABI。
- `[ ]` **ANN-03** `[[packed]]`、`[[align(64)]]`。
- `[ ]` **ANN-04** `[[inline]]`、`[[cold]]`。
- `[ ]` **ANN-05** `[[nonnull]]`、`[[deprecated]]`。
- `[ ]` **ANN-06** 注解目标验证、冲突检测。
- `[ ]` **ANN-07** 注解反射：可由 `compile_time` 读取。
- `[ ]` **ANN-08** 自定义注解预留机制。

---

## 14. 字符串与格式化（FMT）

### 字符串
- `[ ]` **FMT-01** `str`：UTF-8、不拥有、视图语义。
- `[ ]` **FMT-02** `String`：动态字符串、长度/容量、**C 语义内存管理（显式 `new/destroy`，无析构）**。
- `[ ]` **FMT-03** 字符串字面量类型与 `char*` 互操作。
- `[ ]` **FMT-04** UTF-8 验证与遍历；`str`↔`String` 互转。
- `[ ]` **FMT-05** 切片、连接、比较、查找、替换。

### format
- `[ ]` **FMT-06** `(已实现)` `print/println` 内建，`{}` 占位符；`(改)` 纳入 `import std.format` 体系。
- `[ ]` **FMT-07** `format("name={} age={}", name, age)`。
- `[ ]` **FMT-08** 格式说明：`{}`、`{:x}`、`{:f}`、`{:02}`、`{:.2f}`。
- `[ ]` **FMT-09** 编译期格式字符串解析与类型检查（SEM-08）。
- `[ ]` **FMT-10** 自定义类型格式化接口：`to_string`（已有类方法/自由函数两条路径）。
- `[ ]` **FMT-11** 错误处理：参数不足、类型不匹配、格式非法。
- `[ ]` **FMT-12** 性能：避免不必要分配。

---

## 15. 并发（CON）

- `[ ]` **CON-01** `atomic` 类型限定符：`atomic i32 count;`（见 DEC-11）。
- `[ ]` **CON-02** 原子 load/store、CAS、fence。
- `[ ]` **CON-03** 内存序：relaxed/acquire/release/acq_rel/seq_cst。
- `[ ]` **CON-04** `thread_local`：存储类语法（见 DEC-12）。
- `[ ]` **CON-05** `std.thread`：线程创建/join/分离（C 语义，显式管理）。
- `[ ]` **CON-06** 线程局部存储 ABI；与 LLVM atomic 指令映射。
- `[ ]` **CON-07** 与 C11 atomics 互操作。
- `[ ]` **CON-08** 并发测试：数据竞争检测、压力测试。

---

## 16. 标准库（STD）

> 容器采用 **C 语义**：显式 `new/destroy`，不自动释放元素；泛型容器通过扩展点 `to_hash`/`equals`/`to_string` 适配用户类型（无 trait）。

### std.core
- `[ ]` **STD-01** 基础类型导出；`panic/assert/abort`。
- `[ ]` **STD-02** `Optional<T>`、`Result<T,E>`（显式访问访问器）。
- `[ ]` **STD-03** 内存操作：`memcpy/memset/memmove/memcmp`。
- `[ ]` **STD-04** 整数运算与溢出辅助。

### std.mem
- `[ ]` **STD-05** `malloc/calloc/realloc/free` 包装；Allocator 接口；Arena/Pool；placement 分配；对齐工具。

### std.collections 〔新〕
- `[ ]` **STD-06** `Vec`/`ArrayList`（增长、容量、索引、迭代）。
- `[ ]` **STD-07** `HashMap`/`HashSet`（依赖 `to_hash`/`equals`）。
- `[ ]` **STD-08** `Deque`、`List`、`BTree`（可选）。
- `[ ]` **STD-09** 扩展点约定文档：`to_string`/`to_hash`/`equals`。

### std.string
- `[ ]` **STD-10** `str`、`String`、UTF-8 工具、字符串构建器、比较/查找/分割/拼接。

### std.format
- `[ ]` **STD-11** `format`、`print`、格式说明解析、编译期检查接口、自定义格式化。

### std.io
- `[ ]` **STD-12** `print`、`read`、stdout/stderr、文件读写、缓冲 I/O。

### std.math / std.bit
- `[ ]` **STD-13** `sqrt` 等基础数学；浮点分类/舍入/常量；整数数学工具。
- `[ ]` **STD-14** `std.bit`：popcount/clz/ctz/rotl/rotr/bitcast。

### std 其他 〔新〕
- `[ ]` **STD-15** `std.parse`/`std.convert`：字符串↔数字、进制转换。
- `[ ]` **STD-16** `std.sort`、`std.algorithm`。
- `[ ]` **STD-17** `std.hash`。
- `[ ]` **STD-18** `std.ascii`、`std.unicode`、`std.path`。
- `[ ]` **STD-19** `std.fs`（文件系统）。
- `[ ]` **STD-20** `std.time`。
- `[ ]` **STD-21** `std.os`（进程/env/args/syscall 封装）。
- `[ ]` **STD-22** `std.testing`（语言内断言/测试；runner 由 `smc test` 提供）。
- `[ ]` **STD-23** `std.c`：手写 libc 绑定层（配合 MOD-09）。

### std.atomic / std.thread
- `[ ]` **STD-24** 原子类型封装、load/store/CAS/fence、内存序常量。
- `[ ]` **STD-25** `Thread`、join/detach、`thread_local` 支持；可选 mutex/condvar。

---

## 17. LLVM 代码生成与后端（CG）

- `[ ]` **CG-01** IR 类型映射：基础类型/指针/数组/struct/class/slice/Optional/Result。
- `[ ]` **CG-02** `(已实现)` 全局变量/常量/字符串字面量；补测试。
- `[ ]` **CG-03** `(已实现)` 函数定义与调用；补测试。
- `[ ]` **CG-04** `(已实现)` 控制流：if/while/do-while/for/switch/break/continue；补测试。
- `[ ]` **CG-05** `(已实现)` 表达式 codegen（含指针 GEP、fneg、前缀/后缀 `++/--`）；补测试。
- `[ ]` **CG-06** class 方法降级 `Vector_length(Vector* self)`、`static` 成员变量降级。
- `[ ]` **CG-07** **模板单态化代码生成**（GEN-05）、实例去重缓存。
- `[ ]` **CG-08** **继承布局与 `this`/`static_cast` codegen**（INH）。
- `[ ]` **CG-09** **位域 codegen**：`iN` 与读-改-写位操作。
- `[ ]` **CG-10** **内联 `asm`** → LLVM `InlineAsm`（操作数约束/clobber/volatile/目标限定）。
- `[ ]` **CG-11** **lambda codegen**：无捕获→函数指针；有捕获→匿名 struct + 函数。
- `[ ]` **CG-12** `compile_time` 常量折叠后 codegen；与 LLVM 常量集成。
- `[ ]` **CG-13** 原子操作与 `thread_local`。
- `[ ]` **CG-14** 调试信息：DWARF、源码位置。
- `[ ]` **CG-15** 优化级别 O0/O1/O2/O3/Os/Oz。
- `[ ]` **CG-16** 目标三元组与交叉编译。
- `[ ]` **CG-17** 链接：启动代码、libc、静态/动态库；无强制运行时。
- `[ ]` **CG-18** ABI 一致性测试（含位域、继承、模板符号）。

---

## 18. 工具链与驱动（TOOL）

- `[ ]` **TOOL-01** `(已实现)` 编译器 CLI：`check/build/run`；补 `smc test` runner（STD-22）。
- `[ ]` **TOOL-02** `(改)` 移除 C 预处理器相关：`-E/-I/-D`、`src/preprocessor/`、`MacroTable` 及测试（Non-goals）。
- `[ ]` **TOOL-03** 调试器集成：DWARF、断点、变量查看。
- `[ ]` **TOOL-04** 交叉编译工具链配置。
- `[ ]` **TOOL-05** 构建系统集成：Make/Ninja/CMake。
- `[ ]` **TOOL-06** 迁移工具：C → SafeModern C 渐进迁移（注意：无 `#include`/goto，需说明限制）。
- `[ ]` **TOOL-07** **不做**（Non-goals）：`smc fmt`、`smc doc`、LSP、包管理器、依赖解析、registry、manifest。

---

## 19. 测试、验证与发布（TST）

- `[ ]` **TST-01** 词法/语法单元测试；类型系统单元测试。
- `[ ]` **TST-02** 语义分析与诊断快照测试。
- `[ ]` **TST-03** 每个语言特性的集成测试（含模板/CRTP/位域/lambda/asm/匿名类型）。
- `[ ]` **TST-04** ABI/FFI 测试；泛型单态化测试；comptime 测试；格式字符串测试；并发测试。
- `[ ]` **TST-05** `(新)` 数组/Slice 边界检查与整数溢出检查测试（Debug/Release 两种行为）。
- `[ ]` **TST-06** `(新)` **ASan / UBSan** 集成；UB 清单（SEM-13）对应用例。
- `[ ]` **TST-07** `(新)` **覆盖率**（llvm-cov）报告与阈值。
- `[ ]` **TST-08** 跨平台测试 Linux/macOS/Windows；交叉编译测试。
- `[ ]` **TST-09** 可选：差分测试（对照 clang/gcc）、模糊测试（解析器/comptime/格式串）。
- `[ ]` **TST-10** 性能基准：编译速度、运行速度、代码大小（含方法学）。
- `[ ]` **TST-11** 回归测试套件；标准库文档测试。
- `[ ]` **TST-12** 发布流程：二进制包、变更日志（**无版本管理/edition**，见 Non-goals）。

---

## 20. 待定决策（DEC）

- `[ ]` **DEC-01** class 默认访问级别：默认 `public` 还是 `private`。
- `[ ]` **DEC-02** `import math;` 后符号访问语法：`math.add` 还是直接 `add`。
- `[ ]` **DEC-03** `Result<T,E>` 精确布局与 `.error`/`.value` 语义（显式访问，无 `?`）。
- `[ ]` **DEC-04** 默认参数范围；**确认不做**命名参数。
- `[ ]` **DEC-05** `constexpr` 与 `compile_time` 的关系（是否合并）。
- `[ ]` **DEC-06** Slice/数组边界检查默认策略：静态可证 or 运行时检查，Debug/Release 差异。
- `[ ]` **DEC-07** 整数溢出默认行为：Debug 检查 / Release 回绕。
- `[ ]` **DEC-08** 位域布局对齐哪个 C ABI（System V / MSVC）；与 `[[packed]]`/`[[align]]` 交互。
- `[ ]` **DEC-09** 内联 `asm` 的语法与约束模型；是否仅限特定目标。
- `[ ]` **DEC-10** lambda 精确语法与捕获模型（默认按值/按引用？）。
- `[ ]` **DEC-11** `atomic` 是类型限定符还是类型构造器。
- `[ ]` **DEC-12** `thread_local` 存储类语法细节。
- `[ ]` **DEC-13** enum 穷尽检查是警告还是错误。
- `[ ]` **DEC-14** 模块缓存格式与稳定性。
- `[ ]` **DEC-15** 是否默认链接 libc（当前 `print` 依赖系统 `printf`）。

---

## 21. 明确不做（Non-goals）

- `[ ]` **NG-01** `goto` / 标签（现有 `GotoStmtAST`/`LabelStmtAST` 与解析分支需移除）。
- `[ ]` **NG-02** 传统 C 预处理器与宏（`#include/#define/#if/#ifdef/#pragma once`、`-E/-I/-D`）。
- `[ ]` **NG-03** trait / interface / concept（泛型无约束，靠运算符重载与扩展点）。
- `[ ]` **NG-04** 所有权 / 借用 / 生命周期模型（安全能力仅限边界检查 + 空指针检查 + UB 检查）。
- `[ ]` **NG-05** 错误传播算子 `?`（Result/Optional 一律显式判断）。
- `[ ]` **NG-06** 版本管理 / edition。
- `[ ]` **NG-07** 包管理器、依赖解析、registry、manifest。
- `[ ]` **NG-08** LSP、`smc fmt`、`smc doc`（保留 `smc test`）。
- `[ ]` **NG-09** 多继承 / 虚继承 / 菱形继承 / 虚函数 / vtable / RTTI / 构造析构 / GC。
- `[ ]` **NG-10** 模板特化/偏特化、SFINAE、可变参数模板、模板模板参数、concepts、默认模板实参。
- `[ ]` **NG-11** `fn` 关键字与 Rust 风格命名（统一 C 风格声明）。
- `[ ]` **NG-12** `type_info(T)` 与独立 `static_assert` 关键字（统一由 `compile_time` 承载）。
- `[ ]` **NG-13** 命名参数。

---

## 22. 建议实现顺序（里程碑）

### P0：最小可用编译器（已有大量基础，补齐缺口）
- `[ ]` **P0-01** 清理与决策一致的冲突：移除预处理器、goto/label、`-E/-I/-D`（NG-01/02/TOOL-02）。
- `[ ]` **P0-02** C 互操作绑定层（MOD-09/STD-23）——无预处理器后的刚需。
- `[ ]` **P0-03** 变量初始化检查（SEM-01/02）。
- `[ ]` **P0-04** module/import/export（MOD-04~07）。
- `[ ]` **P0-05** 最小 `std.core` / `std.io`（STD-01/12）。
- `[ ]` **P0-06** 诊断系统与测试设施（INF-03/04）。

### P1：核心现代能力
- `[ ]` **P1-01** class / enum / union / 数组 / Slice（AGG、TYP-11/12）。
- `[ ]` **P1-02** Optional / Result 显式访问（TYP-13/14/STD-02）。
- `[ ]` **P1-03** **泛型 + CRTP**（GEN、INH-05、PAR-17/18、CG-07/08）。
- `[ ]` **P1-04** `compile_time` 与反射（CT；取代 type_info/static_assert）。
- `[ ]` **P1-05** 注解系统（ANN）。
- `[ ]` **P1-06** str / String / format（FMT、STD-10/11）。
- `[ ]` **P1-07** 位域、匿名类型、指定初始化器、lambda（AGG-03~06、PAR-10~13）。
- `[ ]` **P1-08** `static_cast` / 内联 `asm`（PAR-18、CG-10）。
- `[ ]` **P1-09** std.mem / std.string / std.format / std.math / **std.collections**（STD-05~14）。

### P2：工程化与并发
- `[ ]` **P2-01** atomic / thread_local / std.thread（CON、STD-24/25）。
- `[ ]` **P2-02** 标准库补齐：parse/sort/algorithm/hash/bit/ascii/unicode/path/fs/time/os/testing/c（STD-15~23）。
- `[ ]` **P2-03** 增量编译与模块缓存（MOD-10）。
- `[ ]` **P2-04** ABI/FFI 完整测试（CG-18）。
- `[ ]` **P2-05** 跨平台与交叉编译（CG-16、TOOL-04）。
- `[ ]` **P2-06** ASan/UBSan/覆盖率（TST-05~07）。

### P3：1.0 稳定
- `[ ]` **P3-01** 性能优化（CG-15、TST-10）。
- `[ ]` **P3-02** ABI 冻结（无 edition，靠 ABI 测试 + 文档）。
- `[ ]` **P3-03** 规范定稿与文档（INF-06）。
- `[ ]` **P3-04** 标准库稳定；迁移指南（TOOL-06）。
- `[ ]` **P3-05** freestanding 目标（MEM-13，可选）。

---

## 23. 验收总原则

- `[ ]` **ACC-01** 所有特性映射到可预测的 C 内存模型与 ABI。
- `[ ]` **ACC-02** class 只是语法封装：无 vtable、构造、析构、GC、隐藏生命周期。
- `[ ]` **ACC-03** 无隐藏分配、无隐藏控制流、无强制运行时。
- `[ ]` **ACC-04** `compile_time` 不产生运行时开销。
- `[ ]` **ACC-05** 保持 C 风格调用模型与 FFI 能力（经 `extern` 绑定层）。
- `[ ]` **ACC-06** 静态分析优先；运行时检查可配置且不默认破坏性能。
- `[ ]` **ACC-07** UB 行为对齐 C 标准（SEM-13），并由 ASan/UBSan 验证。
