# P1-06 设计：`str` / `string` / 内建类型小写化

日期：2026-10-08
状态：已批（六问 + 三节设计，2026-10-08）
范围：FMT-01/02/03/04（核心）、FMT-05 部分、DEC-20、TYP-26、内建类型小写化。
另轮：FMT-06/07（format）、STD-10 高层操作（查找/分割/替换/构建器）、CT-07/ANN-07 反射（本轮解锁依赖）。

## 1. 类型与表示

### 1.1 `str`（视图，FMT-01）

- 新 `TypeKind::Str`，布局 `{ptr, len(字节)}`，与 `T[]` 胖指针同构（DataLayout 可查，CT `size_of` = 16）。
- 不拥有、非 `\0` 结尾、UTF-8 有效序列保证（见 §2.4）。
- 字符串字面量即 `str`：静态全局常量（现有 `CreateGlobalString` 产物），生命周期全局（DEC-20）。

### 1.2 `string`（动态，FMT-02）

- 新 `TypeKind::String`，布局 `{ptr, len(字节), capacity}`（`size_of` = 24）。
- `new` 时 malloc，`destroy()` 时 free；C 语义显式管理：无析构、无隐式堆分配、悬垂是用户责任。
- 与 `str` 的边界（DEC-20）：`string` 拥有其 `ptr` 指向的缓冲区；`str` 一律不拥有。字面量赋给 `string` 必须显式 `string.new`。

### 1.3 内建类型名小写化（硬改）

- `Optional<T>` → `optional<T>`（`T?` 糖保留）；`Result<T, E>` → `result<T, E>`；旧大写拼写成为未知标识符（DEC-18 移除 `register`/`cast` 先例）。
- `Slice` 无大写名（语法本就是 `T[]` 后缀），无需改；`string`/`str` 从一开始即小写。
- 实现点：Parser 的 `isTypeStart`/`parseBaseType` 名字特判；测试 ~30 处、文档 3 处同步。

### 1.4 字面量重定型与互操作（FMT-03、DEC-20）

- `StringExprAST` 类型由 `char*` 改为 `str`。
- `str → char*`：隐式允许（字节视图，放弃 UTF-8 保证与长度信息）。
- `char* → str`：不自动；仅经内建 `str_from_c(cstr)`（按 `\0` 求长，含 UTF-8 验证）。
- 受影响面：字面量传 `char*` 形参的既有代码（libc 互操作、测试），全量回归暴露后迁移。

## 2. 操作与语义

### 2.1 `str` 方法（内建，MethodCall 机制）

| 方法 | 语义 |
|---|---|
| `len()` | 字节长度（`usize`） |
| `char_count()` | 码点数（`usize`） |
| `char_at(i)` | 字节索引处的 `char`（不校验码点边界） |
| `char_len_at(i)` | 该字节处码点宽度（1~4；无效序列处为 0） |

`==`/`!=`：字节比较（有效 UTF-8 下与码点比较等价）。

### 2.2 `string` 方法

| 方法 | 语义 |
|---|---|
| `string.new(s: str)` | 分配并拷贝（容量 = `s.len()`） |
| `destroy()` | free 缓冲区（只读 ptr，可按值接收） |
| `append(s: str)` | 容量不足时 realloc 增长 |
| `push(c: char)` | 单字节追加 |
| `len()` / `capacity()` | 字节长 / 容量 |
| `s[i]` | 字节索引取 `char`；边界策略同 `T[]`（DEC-06 未决，本轮不加运行时检查） |

### 2.3 拼接与高层操作

- 不提供 `str + str` 运算符（无隐式堆分配原则）：拼接 = `string.new(a).append(b)`。
- 比较/查找/分割/替换/构建器：stdlib 轮（STD-10）。

### 2.4 UTF-8（FMT-04）

- 验证点：字面量（parse/sema 期）、`str_from_c`、一切 `char* → str` 路径。无效序列 → 新诊断码 **E2015**（invalid UTF-8 sequence）。
- 遍历：步进 API（§2.1），语言本轮不引入 for-in 迭代语法。

### 2.5 编译期边界

- `string` 不参与 constexpr（堆分配），挂账。
- 字面量 `str` 的 CT `==`/`!=` 沿用 CT-06 既有实现（类型改为 `str` 后保持）。
- CT `+`（CT-06 对字面量的历史能力）随语言层不提供 `str + str` 而一并移除，挂账记录。

## 3. 诊断

| 码 | 含义 |
|---|---|
| E2015 | invalid UTF-8 sequence（字面量/转换点） |

## 4. 实现落点（预估）

- `src/ast/Type.h`：`StrType`/`StringType` + TypeContext 工厂；`TypeKind` 两枚举值。
- `src/frontend/Parser.{h,cpp}`：`str`/`string` 类型识别（同 `optional`/`result` 的 identifier 特判机制，非关键字）；小写化改名；`string.new` 静态构造的解析。
- `src/sema/SemanticAnalyzer.{h,cpp}`：字面量定型、互转规则、内建方法验证（MethodCall 先例）、E2015。
- `src/ast/Expr.cpp`、`src/codegen/CodegenContext.cpp`：字面量 codegen（返回 `str` 胖指针）、`string` 布局与方法降级（malloc/free/realloc）。
- `src/sema/CompileTimeEvaluator.cpp`：`Str`/`String` 的 `size_of` 等。
- 测试：parse 层、sema 层、e2e 一致性矩阵（布局、方法、互转、回归 pin）。

## 5. 明确不做（本轮）

- format（FMT-06/07）、STD-10 高层操作、字符串构建器。
- for-in 迭代语法、字符串反射（CT-07/ANN-07 依赖本类型，另轮）。
- SSO（小串优化）、`string` 的 constexpr、运行时边界检查（随 DEC-06）。
