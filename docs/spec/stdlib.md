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
int main() {
    int x = compute();
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

`dprintf(fd, ...)` 用于访问 stderr（fd = 2），从而**无需暴露 C 的 `FILE` 类型**。

## 6. `std.core` `[impl]`

`import std.core;`，经 `export namespace std` 导出：

| 名字 | 签名 | 说明 |
|---|---|---|
| `std::min` | `constexpr int min(int, int)` | 较小值 |
| `std::max` | `constexpr int max(int, int)` | 较大值 |
| `std::clamp` | `int clamp(int v, int lo, int hi)` | 区间截断 |

## 7. `std.io` `[impl]`

`import std.io;`，经 `export namespace std` 导出：

| 组 | 名字 | 签名 |
|---|---|---|
| stdout | `std::print_int` / `std::print_char` / `std::print_str` / `std::print_bool` | `void (int)` / `void (char)` / `void (char*)` / `void (int)` |
| stderr | `std::print_err_str` / `std::print_err_int` | `void (char*)` / `void (int)` |
| stdin | `std::read_char` / `std::read_int` / `std::read_line` | `int ()` / `int ()` / `int (char*, int)` |
| 文件 | `std::file_open` / `std::file_close` | `void* (char*, char*)` / `int (void*)` |
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

## 9. 待办 `[plan]`

- `std.mem`（STD-05）、`std.collections`（STD-06~08）、`std.string`（STD-10）、
  `std.format`（STD-11）、`std.math`/`std.bit`（STD-13/14）、`std.fs`（STD-19）等。
- `assert` 的释放模式（Release 关闭）若需要，将随 DEC-06/07（边界/溢出检查策略）
  一并裁决；当前**始终启用**。
