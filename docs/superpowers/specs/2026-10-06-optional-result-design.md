# P1-02 Optional / Result 设计（TYP-13/14、STD-02、DEC-03）

日期：2026-10-06
状态：已获用户批准的设计（对话修订版）；本文档为实施权威。
前置：Redef 轮收官（902/902）。基线见 §2。

## 1. 背景与目标

TODO P1-02 要求 `Optional<T>`（TYP-13）与 `Result<T,E>`（TYP-14/STD-02/DEC-03）
显式访问语义。探查证实骨架散落各层但未成体系：

- `T?` 后缀已可解析（`Parser.cpp:1743`，grammar.ebnf §4 `[impl] OptionalType`）；
- `OptionalType/ResultType` 已存在于 `src/ast/Type.h`，`TypeContext` 有单例缓存；
- Mangle 已有 `<T>opt` / `<T>res<E>`；
- LLVM 布局分支已存在但 **Optional 分支把类型误转换为 `SliceType*`**（恰因
  `elementType` 偏移相同而侥幸工作）；`Result` 布局为无判别标志的 `{T,E}`；
- `Result<T,E>` 语法**无解析路径**（template-instance 属 GEN，未实现）；
- sema `typesEqual` 无 Optional/Result 分支（同 kind 泛化规则会误判
  `Optional<int32> == Optional<float64>` 相等）；成员访问、初始化列表、
  拷贝赋值、传参/返回均未接入。

目标：四类收口——完整语法、修正布局、显式访问语义、端到端 codegen，并钉进测试。

## 2. 现状矩阵（探查实证，2026-10-06）

| 能力 | 现状 | 实证 |
|---|---|---|
| `T?` 语法 | ✅ | `Parser.cpp:1743` |
| `Optional<T>` 显式语法 | ❌ | 无 `Optional<` 解析 |
| `Result<T,E>` 语法 | ❌ | `getResultType` 无 parser 调用方 |
| LLVM 布局 | ⚠️ | Optional `{T,i1}` + SliceType 误转换；Result `{T,E}` 无判别位 |
| sema 类型相等 | ❌ | `typesEqual` 同 kind 泛化误判 |
| 成员访问 `.valid/.value/.error` | ❌ | MemberAccess 无分支 |
| 初始化列表 / 拷贝 / 传参返回 | ❌ | 未接入 struct 路径 |
| Mangle | ✅ | `<T>opt` / `<T>res<E>` |

## 3. 核心裁决（对话冻结，2026-10-06）

- **DS1 实现载体**：内建魔术类型（编译器硬编码，同 Slice 先例）；parser 特判
  类型实参表。P1-03 GEN 落地后可迁移，不冲突。
- **DS2 语法**：`Result<T,E>` 尖括号，仅在类型位置（`parseType`）特判；与
  grammar.ebnf template-instance 一致，GEN 后无缝衔接。`?` 只作类型后缀，
  表达式无传播含义（NG-05），与三元 `?:` 无冲突。
- **DS3 语义强度**：C 语义自由访问——伪字段可自由读写、无运行时检查、无
  强制先判断；「显式」= 无糖、无传播算子、无自动解包。P0-03 未初始化检查
  自然覆盖「未初始化就使用」。
- **DS4 判别标志（DEC-03 裁决）**：
  - `T? = { bool valid; T value; }`（LLVM `{i1, T}`，valid 在前）
  - `Result<T,E> = { bool ok; T value; E error; }`（LLVM `{i1, T, E}`）
  - 与 Optional 对称、语义无歧义；abi.md 草案 `{T,i1}`/`{T,E}` 随之修订。

## 4. 设计

### 4.1 语法

- Optional 双形式同型：`T?`（既有）与 `Optional<T>`（新增，`parseType` 中
  named-type `Optional` + `<` type `>` 特判），映射同一 `OptionalType` 单例。
- `Result<T,E>`：`parseType` 中 named-type `Result` + `<` type `,` type `>`。
  实参可嵌套：`Result<int32, int32?>`、`Optional<Optional<int32>>`（即
  `int32??`）。只动类型上下文，不碰表达式解析。
- 保留既有 `?` 后缀在 pointer 之后的位置（grammar：base-type { pointer } [ ? ]）。

### 4.2 布局与 ABI

| 类型 | 规范结构 | LLVM |
|---|---|---|
| `T?` | `{ bool valid; T value; }` | `{ i1, T }` |
| `Result<T,E>` | `{ bool ok; T value; E error; }` | `{ i1, T, E }` |

- 大小/对齐按 LLVM struct 常规（i1 存储占 1 字节，value 偏移按 T 对齐补垫）。
- `getLLVMType`：修 Optional 分支 SliceType 误转换；Result 分支补 `i1` 头。
- 命名单例：沿用 `Optional`/`Result` 命名 LLVM 结构体（可按元素 mangle 区分
  实例名，避免不同 T 共享同名 `Optional` 结构体——LLVM 结构体创建即定，
  需唯一命名，如 `Optional_int32`）。
- Mangle 沿用既有 `<T>opt` / `<T>res<E>`。

### 4.3 语义（sema）

- **伪字段**：`.valid`/`.ok` → `bool`；`.value` → T；`.error` → E。可读可写，
  无访问级别约束。MemberAccess 解析时按 `OptionalType/ResultType` 查表。
- **名义相等**：`typesEqual` 在同 kind 泛化规则**之前**加严格分支——
  Optional 比较元素类型，Result 比较两个实参类型；同 kind 不同实参不相等。
- **无隐式转换**：`T?` ↔ `T` 不互通；`Optional<T>` ↔ `Optional<U>`（T 不同）、
  `Result<T,E>` 实参不同皆不相容。`checkAssignmentTypes`/实参/返回路径由
  typesEqual 严格化自然覆盖；无从 T 包装到 `T?` 的糖（构造一律初始化列表）。
- **初始化**：struct 式列表 `{true, 5}`、`{false, 0}`；零初始化 = 全零
  （`valid=false`）；P0-03 局部未初始化诊断照常生效。
- **拷贝/传参/返回**：整体按值，走 struct 聚合路径（同布局）。
- `bool` 判断 `if (o.valid)` / `if (r.ok)` 为普通 bool 字段语义。

### 4.4 Codegen

- MemberAccess：Optional/Result 分支 GEP 字段读写（`valid`=0/`value`=1；
  `ok`=0/`value`=1/`error`=2）。
- 初始化列表、拷贝赋值、按值传参/返回：接入现有 struct 聚合路径。

### 4.5 文档同步

- `docs/spec/abi.md`：修订两行布局（§27/28）与 §97 mangle 表核对。
- `docs/spec/conversions.md` §4：Optional/Result 行标注「无隐式转换」。
- `TODO.md`：TYP-13/14、STD-02 勾选 + 摘要；DEC-03 记录 DS4 裁决；
  P1-02 勾选。
- `docs/spec/stdlib.md`：STD-02 由语言内建承载（伪字段即访问器），stdlib
  不另设包装函数（无 trait/泛型，多态辅助函数做不了）。

## 5. 测试计划（TDD，RED 先行）

- **sema**（前缀 `OPT`/`RES`，防 TypeContext 单例跨测试碰撞）：
  - 伪字段类型正确（`.valid` bool、`.value` T、`.error` E）；
  - 名义相等 pin（同实参赋值/传参/返回通过）；
  - 名义不等负例（`Optional<int32>` vs `Optional<float64>`、异参 Result）；
  - `T?` ↔ `T` 不互通负例；
  - 初始化列表、零初始化、未初始化使用诊断；
  - 嵌套 `int32??`、`Optional<T>` 与 `T?` 同型 pin；
  - `Result<int32, int32?>` 嵌套实参。
- **e2e**（`OptionalResultE2E.*`）：`if (r.ok)` 分支取 `.value`/`.error`、
  Result 返回错误码、Optional 传参/返回、嵌套。
- **既有测试**：随布局字段序修订更新（`{T,i1}` → `{i1,T}` 影响手工索引
  GEP 之处）；`tests/test_new_features.cpp` 中既有 Optional 相关用例核对。

## 6. 范围外

- `?` 传播算子（NG-05，永久 Non-goal）、模式匹配、`unwrap` 内建；
- 泛型 std 辅助函数（STD-02 由内建承载）；
- Optional/Result 与 trait/扩展点适配（无 trait）；
- GEN 落地后的实现迁移（P1-03 另立项）。

## 7. 遗留

- 预计无。若实施中发现 LLVM 结构体命名/单例与 TypeContext 缓存冲突，
  按 §4.2 唯一命名方案处理并在 Progress.md 记录。
