# 标准库与内建（stdlib）

本册描述**已实现**（`[impl]`）的标准库表面与语言内建。对应 `TODO.md` 的
P0-05（STD-01 / STD-12 / STD-23 / STD-27）与 DEC-21。

> 与 `print`/`assert`/`panic` 相关的能力是**语言内建**而非可导入的库：它们需要
> 调用点的编译期信息（格式类型、源码位置），而无预处理器（NG-02）意味着不能像 C
> 那样用宏实现。内建与用户同名声明共存时，用户声明优先（见 §1）。

## 1. 内建识别规则 `[impl]`

以下名字在**用户未声明同名函数**时作为内建处理（沿用 `print`/`println` 的策略，
见 `src/sema/SemanticAnalyzer.cpp:visit(CallExprAST)`）：

| 名字 | 形式 | 说明 |
|---|---|---|
| `print` | `print("fmt {}", a, b)` | 格式串字面量 + 若干参数；无换行 |
| `println` | `println("fmt {}", a, b)` | 同上，末尾追加 `\n` |
| `assert` | `assert(cond)` | `cond` 为标量；为假则报错并终止 |
| `panic` | `panic(msg)` | `msg` 为 C 字符串；总是报错并终止 |

命名空间限定名（如经 `namespace std` 导出的函数）**不会**被内建拦截：内建判定发生
在限定名解析之后，因此 `std::panic(...)` 调用的是模块里定义的真实函数。

## 2. `assert(cond)` `[impl]`

- `cond` 必须是**标量**（算术或指针/数组，见 `semantics.md`）；否则为错误。
- 求值 `cond`：为真则继续执行；为假则向 **stderr** 打印
  `"<file>:<line>: assertion failed"` 并调用 C 库 `abort()`。
- `file`/`line` 取**调用点**的位置，在编译期嵌入（INF-03/P0-06）。
- 无 `NDEBUG` 概念（无预处理器），断言**始终启用**。
- 类型为 `void`。

```smc
int32 main() {
    int32 x = compute();
    assert(x > 0);   // 失败: foo.smc:3: assertion failed
    return x;
}
```

## 3. `panic(msg)` `[impl]`

- `msg` 必须是 C 字符串（指针/数组）；否则为错误。
- 向 **stderr** 打印 `"<file>:<line>: panic: <msg>\n"`，随后 `abort()`。
- 类型为 `void`；控制流不会返回。

## 4. `abort` `[impl]`

`abort` 是 **C 绑定层**（§5）暴露的 libc 符号，全局可用；`panic`/`assert` 内部即调用
它。它**不**放在 `namespace std` 下：SafeModern C 的非限定名解析优先匹配外层
namespace 前缀，`namespace std { void abort() { abort(); } }` 会自递归，故 `abort`
作为 C 绑定保留在全局作用域。

## 5. `std.c` 绑定层（`libs/std/c.smc`）`[impl]`

无预处理器下访问 libc 的方式：`extern` 声明 + 链接期解析（MOD-09 / STD-23）。该文件
是权威来源，编译器内嵌同文本作为兜底；默认预置于每个编译单元，`--no-prelude` 关闭。

已声明（节选）：

- 输出/输入：`printf`、`puts`、`putchar`、`getchar`、`scanf`、`dprintf`
- 内存：`malloc`、`calloc`、`realloc`、`free`
- 字符串/内存：`strlen`、`strcmp`、`strncmp`、`strcpy`、`memcpy`、`memset`、`memmove`、`memcmp`
- 进程：`exit`、`abort`
- 文件 I/O（`FILE*` 以不透明 `void*` 传递）：`fopen`、`fclose`、`fread`、`fwrite`、
  `fgets`、`fputs`、`fgetc`、`fputc`、`fprintf`
- 数值：`abs`
- 回调（`MEM-10`）：`qsort`、`bsearch`、`atexit`——`extern` 声明支持函数指针形参，
  用户函数可直接作为回调传入

`dprintf(fd, ...)` 用于访问 stderr（fd = 2），从而**无需暴露 C 的 `FILE` 类型**。

## 6. `std.core` `[impl]`

`import std.core;`，经 `export namespace std` 导出：

| 名字 | 签名 | 说明 |
|---|---|---|
| `std::min` | `constexpr int32 min(int32, int32)` | 较小值 |
| `std::max` | `constexpr int32 max(int32, int32)` | 较大值 |
| `std::clamp` | `int32 clamp(int32 v, int32 lo, int32 hi)` | 区间截断 |

`optional<T>`/`result<T,E>`（STD-02，2026-10-06）由**语言内建**承载：伪字段
访问器 `.valid`/`.value`（optional）、`.ok`/`.value`/`.error`（result）可自由
读写（显式判断，无 `?` 传播，NG-05）；无 trait/泛型前 stdlib 不另设包装函数。

## 7. `std.io` `[impl]`

`import std.io;`，经 `export namespace std` 导出：

| 组 | 名字 | 签名 |
|---|---|---|
| stdout | `std::print_int` / `std::print_char` / `std::print_str` / `std::print_bool` | `void (int32)` / `void (char)` / `void (char*)` / `void (int32)` |
| stderr | `std::print_err_str` / `std::print_err_int` | `void (char*)` / `void (int32)` |
| stdin | `std::read_char` / `std::read_int` / `std::read_line` | `int32 ()` / `int32 ()` / `int32 (char*, int32)` |
| 文件 | `std::file_open` / `std::file_close` | `void* (char*, char*)` / `int32 (void*)` |
| 文件 | `std::file_read` / `std::file_write` | `usize (void*, void*, usize)` |
| 文件 | `std::file_read_line` / `std::file_read_char` / `std::file_write_char` / `std::file_write_str` | 见 `libs/std/io.smc` |

- `read_int` 返回解析值，输入非法时返回 `0`（类似 `atoi`；无异常通道）。
- `read_line`/`file_read_line` 读入缓冲区并保证 NUL 结尾，不含换行。
- 文件句柄是不透明 `void*`（底层 `FILE*`）。

## 8. DEC-21：`panic` 的语义 `[impl]`（已冻结）

- `panic`/`assert` 失败**不可恢复**、**不可捕获**：语言无异常（NG-05）、无栈展开；
  直接调用 `abort()` 终止进程。
- 诊断输出到 stderr，格式固定为 `file:line: panic: <msg>` / `file:line: assertion failed`。
- 与 `[[nonnull]]` 等边界检查属性（MEM-*）的关系：二者都属"安全失败即终止"策略；
  `[[nonnull]]` 违规的默认处理将复用本节的内建终止路径（待实现）。

## 9. `std.string`（P1-09 / STD-10）`[impl]`

**分层**：原语（内建，无需 import）+ 组合（`libs/std/string.smc`，`import std.string;`）。
原语之所以是内建：`.smc` 层无法触达 `str` 内部字段、也无法从 `ptr+len` 构造切片值。

### 9.1 内建原语 `[impl]`
| API | 语义 | 所有权 |
|---|---|---|
| `s.find(needle: str\|string) -> isize` | 首现字节下标；无 → `-1`；空 needle → `0` | 视图 |
| `s.rfind(needle) -> isize` | 末现字节下标；无 → `-1`；空 needle → `len` | 视图 |
| `s.sub(begin, end) -> str` | 字节区间零拷贝视图 `{ptr+begin, end-begin}` | 视图（见下） |
| `split(s, sep) -> str[]` | 贪心非重叠分割；元素为源数据视图；无分隔符 → 全串 1 段 | 数组 malloc（见下） |
| `split_destroy(parts)` | 释放 split 的段数组（不触碰元素） | — |

- `sub` 越界（`begin < 0 || end < begin || end > len`）→ 运行时 panic `str.sub: out of bounds`。
- `split` 空 sep：**字面量**在编译期拒绝（E2022 `split: empty separator`）；**动态**运行时 panic 同文案。
- **视图生存期（用户责任）**：`sub`/`split` 的元素引用源数据字节；源 `string` 先 `destroy()` 则视图悬挂
  （与 `str` = `string` 视图的所有权模型一致，挂账 I4 同族）。`split_destroy` 只释放段数组本身。

### 9.2 组合库 `[impl]`（`import std.string;`）
| API | 语义 | 所有权 |
|---|---|---|
| `std::contains(s, sub) -> bool` | `find >= 0` | — |
| `std::starts_with(s, prefix) -> bool` | `find == 0` | — |
| `std::ends_with(s, suffix) -> bool` | 尾对齐比较 | — |
| `std::trim_left/right/trim(s) -> str` | `isspace` 扫描 + `sub` | 视图 |
| `std::join(parts: str[], sep) -> string` | 空数组 → 空 string（cap≥1） | **新分配，调用方 destroy** |
| `std::concat(a, b) -> string` | 新分配 | 同上 |
| `std::repeat(s, n) -> string` | `n=0` → 空 string | 同上 |
| `std::utf8_sub(s, start_cp, len_cp) -> str` | 码点步进（`char_len_at`）+ `sub` | 视图 |

**字符串构建**：`string` 自身即 builder（`string.new("")` + `append`/`push`/`len`，P1-06）。
独立 `StrBuilder` 类挂账：**class 无法跨模块导出**——类型名在 parse 期注册、先于 import
处理（`std::StrBuilder` 与模块级 `export class` 均不可见）；语言侧修复后另行落地。
不做（spec §8）：字典序 `<`/`>`、`to_lower`/`to_upper`、`replace`。

## 10. `format` 与 FMT-08 规格集（P1-09 / STD-11）`[impl]`

`format(fmt: str, args...) -> string` 为内建（user-defined 同名函数优先），
与 `print`/`println` 共享同一规格解析器（`PrintSpec`，`src/ast/PrintFormat.cpp`）。
`format` 返回**新分配 string**（单缓冲一次 `malloc`，无增长路径——长度可预知，
FMT-12 以此满足），调用方 `destroy()`。

### 10.1 规格文法（`{:...}`）`[impl]`
```
spec := [fill] align? sign? '0'? width? ('.' precision)? type?
fill := 除 `{` `}` `:` 数字外的任意字符（仅当后随 align 时生效）
align := '<' | '>' | '^'        sign := '+' | '-' | ' '
width := 1..200                 precision := 0..200
type  := 'x' | 'X' | 'o' | 'b' | 'f' | 'e' | 's'
```
- 语义：`'<'` 左对齐、`'>'` 右对齐、`'^'` 居中（或自定义 fill）走渲染路径；
  `'0'` 补零（仅数值、仅右对齐）；`'+'`/`' '` 符号 flag；`{:.2}` 浮点无 type 推断为 `f`；
  空 `{}` 保持旧行为（FMT-06）：整数 `%d`、浮点 `%g`、str `%.*s`、bool `true/false`。
- 字符串带 width 时**默认左对齐**；数值默认右对齐。
- `{:b}` 依赖 C23 `%b`（glibc ≥ 2.35 基线）。

### 10.2 拒绝矩阵（编译期）`[impl]`
| 诊断 | 条件 |
|---|---|
| E2020 参数计数不匹配 | 占位符数 ≠ 实参数 |
| E2021 规格与实参不符 | `x/X/o/b` 用于非整数；`f/e` 用于非浮点；`s` 用于非字符串类；precision/`0` 用于不支持者；Char/`char*`/指针带任何规格 |
| E2022 规格非法 | 未知 type、`{:d}`（十进制只用 `{}`）、`0` 与 `<`/`^` 冲突、`0` 无 width、align 无 width、width/precision > 200、文法错误 |

### 10.3 实参降级（FMT-10）`[impl]`
无内建转换的类型走 `to_string`（类方法/自由函数两条路径，P1-06 已有）；
仍无可行转换 → 编译期错误 `cannot format value of type ...`。

### 10.4 动态格式串契约（非字面量 fmt）`[impl]`
编译期**只检查实参可格式化**，不解析规格。运行时（`smc.format.dyn`）：
`{{`/`}}` 转义；`{...}` 消费下一实参并按其静态类型**默认转换**（规格文本被忽略）；
占位符多于实参 → 多余忽略；少于 → 缺位读空串即停；未终止 `{` → 扫描即止。
以上动态行为**不做检查不 panic**，格式串自担（stdlib 边界，spec §2.3.3）。

## 11. 待办 `[plan]`

- `std.mem`（STD-05）、`std.collections`（STD-06~08）、`std.string`（STD-10）、
  `std.format`（STD-11）、`std.math`/`std.bit`（STD-13/14）、`std.fs`（STD-19）等。
- `assert` 的释放模式（Release 关闭）若需要，将随 DEC-06/07（边界/溢出检查策略）
  一并裁决；当前**始终启用**。
