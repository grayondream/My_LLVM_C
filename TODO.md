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
- `[x]` **BASE-04** 控制流：if/else、while、do-while、for、switch/case/default（含贯穿）、break、continue、return、defer。（`goto`/label 已按 NG-01 移除）
- `[x]` **BASE-05** 语义：作用域/符号表、函数重载（`OverloadSet`）、运算符重载（`operator`）、左值/右值、隐式类型转换（部分）、常量折叠 `constexpr`、`print/println` 内建与 `to_string` 分派。
- `[x]` **BASE-06** 代码生成：基础类型/指针/数组/struct/class/union/enum/函数指针、全局变量、字符串字面量、指针算术（GEP）、负浮点（fneg）、非 void 函数补 `unreachable`。
- `[x]` **BASE-07** 驱动：`-c/-o/-S/-M/-O/-g/-v/-Wall/-Werror/-std/-fsyntax-only/-l/-L`、JIT、`cc` 链接系统 libc。（`-E/-I/-D` 已按 TOOL-02 移除；模块搜索路径改用 `-M/--module-path`）
- `[x]` **BASE-08** 自带标准库 **libsafec**（`src/libsafec/`，`namespace safec`，独立目标 `safec` → `build/lib/libsafec.a`）：`printf/sprintf`、`malloc/free/calloc/realloc`、`strlen/strcmp/strcpy/memcpy/memset/memcmp`。
- `[x]` **BASE-09** `resources/main.c` 可完整编译运行。全量 **528** 测试通过。
- `[x]` **BASE-10** `(新)` 既有实现已在 `src/libsafec/` 使用 `namespace safec`，但语言层未定义 `namespace`（规范缺口，见 PAR-22/MOD-12）。现已实现 `namespace`（值 + 类型）。

---

## 1. 基础设施与工程（INF）

- `[ ]` **INF-01** 锁定 LLVM 版本与集成层：CMake `find_package(LLVM)`、最低版本断言、版本不匹配诊断。
- `[ ]` **INF-02** 构建系统：Debug/Release、跨平台（Linux/macOS/Windows）、交叉编译目标配置。
- `[x]` **INF-03** **诊断系统**：统一 `Diagnostic`（源码位置/错误码/严重级别/修复建议）、诊断快照测试设施。已实现错误码注册表 + `formatWithSeverity` + fix-it + 快照设施（`tests/diagnostics/snapshots/`）+ 警告独立通道（`getWarnings`，`-Werror` 可控）；词法器记录记号**起始**位置，表达式/声明节点由解析器打点，`file:line:col` 对解析与语义诊断均可用（`tests/frontend/test_source_location.cpp`）。
- `[ ]` **INF-04** 测试框架完善：单元、集成、黄金文件（IR/输出）、诊断快照；CTest 分组与标签。
- `[ ]` **INF-05** CI：Linux/macOS/Windows 矩阵、交叉编译、ABI 测试、性能基准。（外围仅限构建/测试，不含 LSP/包管理）
- `[x]` **INF-06** 文档系统：语言规范（EBNF 语法）、类型/ABI 文档、`compile_time` API 文档。（**提前到 P0**，见 P0-07；`docs/spec/` 六分册 + 一致性测试已建）
- `[x]` **INF-09** `(新)` **规范性文法（EBNF）**：完整产生式覆盖声明/类型/表达式/语句/注解/模块/`namespace`；作为规范唯一权威来源。（`docs/spec/grammar.ebnf`；ABI/转换/语义分册见 `docs/spec/`；一致性由 `test_spec_conformance` 绑定）
- `[x]` **INF-10** `(新)` **运算符优先级/结合性总表**：表驱动，并与解析器实现绑定一致性测试。（表在 `docs/spec/grammar.ebnf` §9，实现于 `smc::getOperatorInfo`；`tests/frontend/test_operator_precedence.cpp` 10 项，全量 538 测试通过）
- `[x]` **INF-11** `(新)` **关键字与保留字总表**：数据类型/存储类/控制流/模块/注解/限定符；区分已用与保留未用。（见 `docs/spec/keywords.md`；DEC-18 已冻结，`goto`/`#` 标记为已移除）
- `[ ]` **INF-12** `(新)` **语言版本标识**：`-std=` 取值、默认版本、特性门控（不含 edition，见 NG-06）。
- `[~]` **INF-13** `(新)` **诊断格式规范**：错误码命名、位置格式、严重级别、稳定输出（配合 INF-03、TOOL-09）。注册表（E0xxx/E1xxx/E2xxx/W3xxx）与 `file:line:col: severity[CODE]: msg` 格式已落地。
- `[ ]` **INF-07** 可复现构建：确定性输出、路径无关、固定优化流程。
- `[ ]` **INF-08** 最小命令行驱动保留：`smc check/build/run/test`（不含 fmt/doc/lsp，见 Non-goals）。

---

## 2. 词法分析（LEX）

- `[ ]` **LEX-01** `(已实现)` 关键字与基础类型关键字词法；核对与规范一致。
- `[~]` **LEX-02** `(新)` 新增关键字 token：`template`、`typename`、`this`；弃用未使用的 `generic`。（`generic`/`comptime` token 已按 DEC-18 移除；`template`/`typename`/`this` 待补）
- `[x]` **LEX-03** `(改)` 移除/废弃：C 预处理器与宏相关 token（`#include/#define/#if`）与 `-E/-I/-D`（见 Non-goals）。`#` 仍作为 `TOKEN_HASH` 词法识别，由解析器以“不支持预处理指令”报错。
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
- `[~]` **LEX-14** `(新)` **关键字/保留字总表落地**（配合 INF-11）：`register`/`cast`/`typeof` 已按 DEC-18 移除 token；`namespace` 关键字与 `::` token 已新增；`template`/`typename`/`this` 待补。
- `[ ]` **LEX-15** `(新)` **字面量默认类型与后缀映射**：无后缀整型/浮点的默认类型，`u`/`l`/`f16`…后缀 → 底层类型表。
- `[ ]` **LEX-16** `(新)` **转义字符全集、原始字符串与多行字符串**的精确词法规则（细化 LEX-06）。
- `[ ]` **LEX-17** `(新)` **数字分隔符位置规则、进制前缀、整型字面量溢出诊断**（细化 LEX-04/13）。
- `[ ]` **LEX-18** `(新)` **标识符 UTF-8/Unicode 规则**：允许范围、NFC 规范化、与关键字冲突处理。

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
- `[x]` **PAR-09** `(改)` 语句：块、if/else、while、do-while、for、switch/case、return、break、continue、声明。（**不含 goto/label**，见 Non-goals；已移除 goto/标签解析）
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
- `[x]` **PAR-22** `(新)` `namespace` 声明与限定名 `ns::name` 语法（现缺失；`src/libsafec` 已使用 `namespace safec`）。函数/变量成员、嵌套命名空间、限定类型名（struct/class/union/enum/typedef）均已实现。
- `[x]` **PAR-23** `(新)` 运算符优先级/结合性在解析器中的显式实现与表驱动测试（配合 INF-10）。`smc::getOperatorInfo` 表驱动 + `test_operator_precedence` + `test_spec_conformance`。
- `[ ]` **PAR-24** `(新)` 别名声明规范形式收敛：`typedef`/`using`/`type` 的取舍与统一 AST（见 DEC-16）。

---

## 4. 名称解析与模块系统（MOD）

- `[ ]` **MOD-01** 符号表：全局/模块/类型/函数/变量/字段/方法。
- `[ ]` **MOD-02** 作用域规则：块/函数/类型/模块。
- `[ ]` **MOD-03** 前向声明：函数/struct/class/enum/union/类型别名。
- `[x]` **MOD-04** `module` 声明与文件映射。点分/`::` 模块名 → `TranslationUnitAST::moduleName`；`import` 点分名解析 + 文件↔模块名校验（MOD-13）。
- `[x]` **MOD-05** `export` 导出规则；`import` 导入规则与访问语法（DEC-02 已冻结：直接非限定访问）。参与模块默认私有，`export`/`public` 后可见。
- `[x]` **MOD-06** 模块可见性与 `public/private`。每模块私有作用域 + `export` 提升到全局；顶层 `public` 与 `export` 同义（类成员访问级别仍待 PAR-04）。类型名可见性见 `docs/spec/modules.md` §11 限制。
- `[x]` **MOD-07** 模块循环依赖检测。`ModuleLoader` DFS `active` 栈检出导入环并报错（列出环路径）；`loaded` 去重支持菱形依赖。
- `[ ]` **MOD-08** 名称修饰：C ABI 符号、class 方法、**模板单态化符号**、`static` 成员。
- `[~]` **MOD-09** `(改)` **C 互操作（无预处理器方案）**：不解析真实 C 头文件，改用 `extern` 声明 + `std.c` 绑定层。绑定层已文件化到 `libs/std/c.smc`（`src/driver/StdPrelude.*` 优先读文件、内嵌兜底；`--no-prelude` 关闭）+ 修复 `void*` 形参解析。
- `[ ]` **MOD-10** 增量编译：模块/AST/类型/IR 缓存与失效策略（内容哈希）。
- `[x]` **MOD-11** 导入符号访问语法待定并实现：**直接非限定访问**（DEC-02 冻结）；限定名由模块内 `namespace` 提供。
- `[x]` **MOD-12** `(新)` `namespace` 语义：嵌套/开放命名空间、限定查找、与模块的关系（DEC-17 已冻结：正交）。（嵌套查找 + 名称隔离 + 限定类型名已实现；规范见 `docs/spec/modules.md`）
- `[x]` **MOD-13** `(新)` 模块/编译单元文件布局：文件 ↔ 模块映射、导出单元、单文件多模块规则。点分名↔目录文件映射 + 声明名与导入说明符一致性校验已实现（`module` 首部/重复声明约束待补）。
- `[x]` **MOD-14** `(新)` `import` 搜索路径与名称解析顺序、循环导入诊断（细化 MOD-07）。（搜索顺序：导入者目录 → `-M/--module-path` → `STD_DIR`；循环报错并列出环路径；规范见 `docs/spec/modules.md`）
- `[~]` **MOD-15** `(新)` **名称修饰方案（规范性）**：C ABI、方法、模板单态化、`static` 成员、namespace 的完整规则（细化 MOD-08）。（现状+目标见 `docs/spec/abi.md`）

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
- `[~]` **TYP-22** `(新)` **整数提升与常规算术转换**：转换等级、signed×unsigned 混合规则（细化 TYP-19）。（规范草案见 `docs/spec/conversions.md` §3；实现仍为弱规则，待落地）
- `[~]` **TYP-23** `(新)` **隐式/显式转换矩阵**：标量、指针、数组、struct/class、enum、Optional/Result 的完整转换表。（规范矩阵见 `docs/spec/conversions.md` §4；`[plan]` 项待实现）
- `[~]` **TYP-24** `(新)` **空指针常量语义**：`nullptr`/`NULL`/`0` 与指针/bool 的转换规则。（草案见 `docs/spec/conversions.md`；`null`/`0` 的 int↔ptr 转换、指针比较、指针真值判断已实现；`nullptr` token 待补）
- `[~]` **TYP-25** `(新)` enum 默认底层类型、枚举常量作用域与限定访问（细化 TYP-09）。枚举常量已可作为值/常量表达式参与运算，支持限定访问 `A::Red`；显式底层类型 `:u8` 待补。
- `[ ]` **TYP-26** `(新)` 字符串字面量类型、`str`/`String` 生命周期与所有权（细化 FMT-01/02）。
- `[~]` **TYP-27** `(新)` 聚合类型 ABI：按值传参/返回（byval/sret）、端序、默认对齐与成员内边距（细化 MEM-09）。（草案见 `docs/spec/abi.md`）

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

- `[x]` **SEM-01** 变量未初始化检查：数据流分析、分支合并、循环。已实现 definite-assignment 分析（if/else 取交集、循环保守、do-while 至少一次、地址取址假定已初始化），产出 W3001 警告（`SemanticAnalyzer::checkInitialization`）。
- `[x]` **SEM-02** 指针未初始化检查；解引用前必须赋值。同一分析覆盖指针（`int* p; *p` → W3001）。
- `[ ]` **SEM-03** 指针对空性检查：`[[nonnull]]`、可选运行时非空断言。
- `[ ]` **SEM-04** 访问控制检查：`public/private/protected`（单继承链）。
- `[ ]` **SEM-05** 类型检查：表达式/赋值/调用/返回/字段访问。
- `[ ]` **SEM-06** 泛型实例化检查（GEN-06）。
- `[ ]` **SEM-07** `compile_time` 条件求值与死代码消除。
- `[ ]` **SEM-08** 格式字符串类型检查：`{}`、`{:x}`、`{:f}`、`{:02}`、`{:.2f}`（`print/println`）。
- `[ ]` **SEM-09** 数组/Slice 边界检查策略：静态可证明或运行时检查（见 DEC-06）。
- `[ ]` **SEM-10** 整数溢出检查策略：Debug 检查 / Release 行为（见 DEC-07）。
- `[~]` **SEM-11** 警告：未使用变量、不可达代码、弃用 API、enum 穷尽性（未初始化变量已实现 W3001；其余待补）。
- `[ ]` **SEM-12** 位域语义检查：宽度合法、跨存储单元规则。
- `[ ]` **SEM-13** UB 清单（**对齐 C 标准**）：为 SEM-09/10/11 与 UBSan 提供依据；不引入所有权/生命周期模型。
- `[~]` **SEM-14** 静态分析诊断：错误码、源码位置、修复建议。主要检查点已带错误码与 fix 字段（见 `src/sema/Diagnostic.*`）。
- `[~]` **SEM-15** `(新)` **求值顺序与序列点**：表达式/函数实参求值顺序，对齐 C 标准（细化 SEM-13）。（草案见 `docs/spec/semantics.md`）
- `[~]` **SEM-16** `(新)` **重载解析规则**：可行候选、最佳匹配、歧义诊断、模板实例参与（细化 FUN-07/GEN-06）。（现状+目标见 `docs/spec/semantics.md`）
- `[~]` **SEM-17** `(新)` **可重载运算符集合与签名约定**：允许重载的运算符、参数/返回约束。（草案见 `docs/spec/semantics.md`）
- `[~]` **SEM-18** `(新)` **`defer` 语义**：注册顺序、作用域退出执行顺序、与 `return`/`break`/`continue` 的交互（细化 FUN-10）。（现状+待冻结见 `docs/spec/semantics.md`）

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
- `[~]` **AGG-14** 底层类型 `:u8` 等；值/作用域/名称解析。隐式/显式值、枚举常量名称解析（含 namespace 限定）、switch 标签已实现；显式底层类型待补。
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
- `[ ]` **FUN-12** `(新)` 程序入口约定：`main` 签名/参数/返回值，JIT 与原生一致（见 DEC-19）。
- `[ ]` **FUN-13** `(新)` 重载解析实现入口与诊断（实现 SEM-16）。
- `[~]` **FUN-14** `(新)` `to_string`/`to_hash`/`equals` 扩展点的精确签名与查找规则（自由函数/成员方法两条路径）。（草案见 `docs/spec/semantics.md`）

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
- `[~]` **MEM-14** `(新)` 聚合传参/返回 ABI（byval/sret）与栈布局（配合 TYP-27）。（草案见 `docs/spec/abi.md`）
- `[~]` **MEM-15** `(新)` 端序与默认对齐规则；`[[packed]]`/`[[align]]` 交互（细化 MEM-09/DEC-08）。（草案见 `docs/spec/abi.md`）
- `[~]` **MEM-16** `(新)` 调用约定：C 默认约定细节、可变参数约定、参数寄存器/栈规则（细化 MEM-11）。（草案见 `docs/spec/abi.md`）

---

## 12. 编译期与反射（CT）〔新〕

> 以 `compile_time` 命名空间统一承载；**不再使用 `type_info(T)` 与独立 `static_assert` 关键字**。

- `[~]` **CT-01** `compile_time` 语法入口：`comptime` 关键字统一并入 `compile_time`（旧名 token 已移除）。（草案见 `docs/spec/compile_time.md`）
- `[~]` **CT-02** `compile_time.static_assert(cond, msg)`：编译期断言，失败带源码位置诊断。（草案见 `docs/spec/compile_time.md`）
- `[~]` **CT-03** `compile_time.if(cond) { ... }`：条件编译与死代码消除。（草案见 `docs/spec/compile_time.md`）
- `[~]` **CT-04** 目标查询：`compile_time.target.os/arch/cpu`。（草案见 `docs/spec/compile_time.md`）
- `[~]` **CT-05** 构建查询：`compile_time.build.debug` 等。（草案见 `docs/spec/compile_time.md`）
- `[~]` **CT-06** 编译期求值器：解释 AST、常量折叠、字符串比较/整数运算/布尔逻辑。（草案见 `docs/spec/compile_time.md`）
- `[~]` **CT-07** 反射 API（取代 `type_info`）：类型名、大小、对齐、字段、偏移、属性。（草案见 `docs/spec/compile_time.md`）
- `[~]` **CT-08** 类型作为值：编译期与类型系统交互。（草案见 `docs/spec/compile_time.md`）
- `[~]` **CT-09** 编译期缓存：求值结果缓存、增量编译。（草案见 `docs/spec/compile_time.md`）
- `[~]` **CT-10** 编译期错误诊断：位置、求值栈、原因。（草案见 `docs/spec/compile_time.md`）
- `[~]` **CT-11** 编译期沙箱：限制文件/网络/系统访问。（草案见 `docs/spec/compile_time.md`）
- `[~]` **CT-12** 与 LLVM 常量集成。（草案见 `docs/spec/compile_time.md`）
- `[~]` **CT-13** `(新)` `compile_time` 反射 API 的精确签名与返回类型（取代 `type_info`，细化 CT-07）。（草案见 `docs/spec/compile_time.md`）
- `[~]` **CT-14** `(新)` `constexpr` 与 `compile_time` 的边界与互操作（落实 DEC-05）。（草案见 `docs/spec/compile_time.md`）

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
- `[ ]` **ANN-09** `(新)` 各注解语义与默认行为：`repr(C)`/`packed`/`align`/`inline`/`cold`/`nonnull`/`deprecated`（细化 ANN-02~05）。
- `[ ]` **ANN-10** `(新)` 无注解时的默认行为规范：默认布局、默认可见性、默认调用约定（配合 DEC-01/DEC-08）。

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
- `[ ]` **FMT-13** `(新)` `print/println` 与 `printf` 的关系与统一语义；数值宽度/精度/类型的精确格式化规则（细化 FMT-08/SEM-08）。
- `[ ]` **FMT-14** `(新)` 自定义格式化 `to_string` 的签名与分派规则（细化 FMT-10/FUN-14）。

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
- `[ ]` **CON-09** `(新)` `atomic` 语义模型：可原子化类型/大小限制、对齐要求、与 LLVM atomic 映射（细化 CON-01/02）。
- `[ ]` **CON-10** `(新)` `thread_local` 存储模型与初始化/销毁规则（细化 CON-04）。

---

## 16. 标准库（STD）

> 容器采用 **C 语义**：显式 `new/destroy`，不自动释放元素；泛型容器通过扩展点 `to_hash`/`equals`/`to_string` 适配用户类型（无 trait）。

### std.core
- `[x]` **STD-01** 基础类型导出；`panic/assert/abort`。`libs/std/core.smc` 已建（`min/max/clamp`）。`panic`/`assert` 实现为**语言内建**（需调用点编译期信息，无预处理器；见 `docs/spec/stdlib.md`）：`assert(cond)` 失败打印 `file:line: assertion failed` 后 `abort()`，`panic(msg)` 打印 `file:line: panic: <msg>` 后 `abort()`；`abort` 由 `std.c` 绑定层暴露。
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
- `[x]` **STD-12** `print`、`read`、stdout/stderr、文件读写、缓冲 I/O。`libs/std/io.smc` 已提供 `print_*`、`print_err_*`（stderr，经 `dprintf`）、`read_char/read_int/read_line`、`file_open/close/read/write`、`file_read_line/read_char/write_char/write_str`；基于 libc。

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
- `[x]` **STD-23** `std.c`：手写 libc 绑定层（配合 MOD-09）。`libs/std/c.smc`（内嵌兜底）已扩展：`abort`、`dprintf`（stderr，fd=2）、`fopen/fclose/fread/fwrite/fgets/fputs/fgetc/fputc/fprintf`、`scanf`。

### std.atomic / std.thread
- `[ ]` **STD-24** 原子类型封装、load/store/CAS/fence、内存序常量。
- `[ ]` **STD-25** `Thread`、join/detach、`thread_local` 支持；可选 mutex/condvar。
- `[~]` **STD-26** `(新)` 扩展点库约定：`to_string`/`to_hash`/`equals` 标准签名与示例（落实 STD-09/FUN-14）。（接口草案见 `docs/spec/semantics.md`）
- `[x]` **STD-27** `(新)` `panic/assert/abort` 语义与实现策略：**不可恢复、不可捕获、直接 `abort`**（DEC-21 已冻结）；诊断输出到 stderr 且带 `file:line`；`assert`/`panic` 为内建（用户同名声明优先）。规范见 `docs/spec/stdlib.md`。与 `[[nonnull]]` 的关系（复用终止路径）待 MEM-* 实现。

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
- `[x]` **TOOL-02** `(改)` 移除 C 预处理器相关：`-E/-I/-D`、`src/preprocessor/`、`MacroTable` 及测试（Non-goals）。已删除源码与测试；`-I` 由 `-M/--module-path` 取代。
- `[ ]` **TOOL-03** 调试器集成：DWARF、断点、变量查看。
- `[ ]` **TOOL-04** 交叉编译工具链配置。
- `[ ]` **TOOL-05** 构建系统集成：Make/Ninja/CMake。
- `[ ]` **TOOL-06** 迁移工具：C → SafeModern C 渐进迁移（注意：无 `#include`/goto，需说明限制）。
- `[ ]` **TOOL-07** **不做**（Non-goals）：`smc fmt`、`smc doc`、LSP、包管理器、依赖解析、registry、manifest。
- `[ ]` **TOOL-08** `(新)` `-std=`/语言版本标志与默认版本（实现 INF-12）。
- `[ ]` **TOOL-09** `(新)` 诊断输出格式与错误码注册表（实现 INF-13）。

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
- `[x]` **DEC-02** `import math;` 后符号访问语法：**直接非限定访问**（`add`）。模块是物理边界，不引入 `math.add` 式运算符；限定名由模块内 `namespace` 提供。
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
- `[ ]` **DEC-16** `(新)` 类型别名规范形式：`typedef`/`using`/`type` 何者为准、是否全部保留。
- `[x]` **DEC-17** `(新)` `namespace` 与模块（`module`/`import`）的关系与共存方式：**模块=物理边界，namespace=逻辑边界，二者正交**；同一模块可含多个 namespace，同一 namespace 可跨模块。
- `[x]` **DEC-18** `(新)` `register`/`cast`/`typeof` 等既有 token 的废弃或保留。→ **决定移除**（一并移除 `comptime`/`generic`）；显式转换改用 `static_cast`/`reinterpret_cast`（LEX-11），类型查询改用 `compile_time` 反射（CT-07）。已同步 `Token.h`/`Lexer.cpp`/`Utils.cpp`/`keywords.md`。
- `[ ]` **DEC-19** `(新)` `main` 入口签名与返回值约定。
- `[ ]` **DEC-20** `(新)` 字符串字面量所有权/生命周期与 `str`/`String` 边界。
- `[x]` **DEC-21** `(新)` `panic` 默认可否被捕获、是否直接 `abort`。→ **决定**：不可捕获（语言无异常/无栈展开，NG-05），直接调用 `abort()`；`assert`/`panic` 走同一终止路径，输出带调用点 `file:line`（见 `docs/spec/stdlib.md` §8）。

---

## 21. 明确不做（Non-goals）

- `[x]` **NG-01** `goto` / 标签（现有 `GotoStmtAST`/`LabelStmtAST` 与解析分支需移除）。已移除 AST/解析/语义/代码生成与测试。
- `[x]` **NG-02** 传统 C 预处理器与宏（`#include/#define/#if/#ifdef/#pragma once`、`-E/-I/-D`）。已移除 `src/preprocessor/`、`MacroTable`、`-E/-I/-D` 及测试；`#` 换以明确报错。
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
- `[x]` **P0-01** 清理与决策一致的冲突：移除预处理器、goto/label、`-E/-I/-D`（NG-01/02/TOOL-02）。已完成；`-I` 由 `-M/--module-path` 取代。
- `[~]` **P0-02** C 互操作绑定层（MOD-09/STD-23）——无预处理器后的刚需。文件化 `libs/std/c.smc` + `extern` 已可用。
- `[x]` **P0-03** 变量初始化检查（SEM-01/02）。已实现（W3001，含分支合并/循环；`-Werror` 可将警告变失败）。
- `[x]` **P0-04** module/import/export（MOD-04~07）。源文件 `import`（搜索路径/点分名/循环报错）、`module` 文件绑定与校验、`export` 可见性、每模块私有作用域均已实现。
- `[x]` **P0-05** 最小 `std.core` / `std.io`（STD-01/12）。`std::min/max/clamp`、`std::print_*`、`std::read_*`、`std::file_*` 可用；`assert`/`panic` 内建、`abort` 经 `std.c`；规范 `docs/spec/stdlib.md`。
- `[x]` **P0-06** 诊断系统与测试设施（INF-03/04）。诊断核心 + 快照设施 + 词法/语法/声明源码位置均已完成；INF-04 的其余测试框架项（黄金 IR/输出、CTest 分组标签）由 INF-04 单独跟踪。
- `[x]` **P0-07** `(新)` **语言规范骨架**：EBNF + 关键字表 + 优先级表 + 转换矩阵（INF-06/09~11、PAR-23、TYP-22/23）。四件套位于 `docs/spec/`，并由 `tests/spec/test_spec_conformance.cpp` 绑定实现；各 `[plan]` 细节仍由对应条目跟踪。
- `[x]` **P0-08** `(新)` **namespace 支持**（PAR-22、MOD-12、DEC-17）——现有 `libsafec` 已依赖，属刚需。函数/变量、嵌套、限定访问、限定类型名均已实现。

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
