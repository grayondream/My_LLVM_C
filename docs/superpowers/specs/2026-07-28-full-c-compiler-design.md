# 完整 C11 子集编译器设计

**日期**: 2026-07-28
**状态**: 已批准
**目标**: 支持完整预处理器、内置 libc、多文件编译的 C11 子集编译器

---

## 1. 概述

将现有的基于 LLVM 的 C 编译器从当前状态（只能编译 `int main() { return 1+3; }`）扩展为一个完整的 C11 子集编译器，能够编译真实程序。

### 当前状态

- **词法分析器**: 已完成，支持约 50 种 token 类型（标识符、数字、字符串、关键字、运算符）
- **AST**: 已有 7 种表达式、4 种语句、4 种声明的基础架构
- **代码生成**: 已有所有现有 AST 节点的代码生成基础设施，支持 JIT 执行和目标文件输出
- **语法分析器**: 瓶颈 —— 只能解析 `int main() { return <number> + <number>; }`

### 目标状态

- 完整 C11 子集：变量、控制流、函数、结构体、联合体、枚举、数组、指针、所有运算符
- 完整预处理器：`#define`、`#include`、`#ifdef`、token 拼接、字符串化
- 内置 libc：printf、malloc、字符串函数等
- 多文件编译：`-c`、`-o`、`-I`、`-D` 选项，分离编译和链接
- 带文件/行/列的错误报告

### 非目标（v1 版本）

- 完整 C11 特性：`_Generic`、`_Atomic`、`_Static_assert`、`_Alignas`、C11 线程
- 超出 LLVM O2 的优化
- 交叉编译
- 汇编输入/输出
- 内联汇编
- GNU 扩展

---

## 2. 架构

### 编译流水线

```
源文件
    ↓
[词法分析器] → 原始 Token 流
    ↓
[Token 流] → 带缓冲的前瞻流
    ↓
[预处理器] → 展开后的 Token 流（宏已展开，#include 已内联）
    ↓
[语法分析器] → AST（递归下降，消费 Token 流）
    ↓
[语义分析器] → 带注解的 AST（类型检查、名称解析）
    ↓
[代码生成] → LLVM IR
    ↓
[优化] → O2 优化流水线
    ↓
[目标文件输出 / JIT 执行]
```

### 目录结构

```
src/
├── frontend/
│   ├── Token.h / Token.cpp          （已有，扩展）
│   ├── Lexer.h / Lexer.cpp          （已有，扩展）
│   ├── TokenStream.h / TokenStream.cpp  （新增）
│   └── Parser.h / Parser.cpp        （已有，大幅重写）
├── preprocessor/
│   ├── Preprocessor.h / Preprocessor.cpp  （新增）
│   └── MacroTable.h / MacroTable.cpp      （新增）
├── ast/
│   ├── Expr.h / Expr.cpp            （已有，扩展）
│   ├── Stmt.h / Stmt.cpp            （已有，扩展）
│   ├── Decl.h / Decl.cpp            （已有，扩展）
│   ├── Type.h / Type.cpp            （已有，扩展）
│   └── Symbol.h / Symbol.cpp        （已有）
├── sema/
│   ├── SemanticAnalyzer.h / SemanticAnalyzer.cpp  （新增）
│   └── Diagnostic.h / Diagnostic.cpp              （新增）
├── codegen/
│   └── CodegenContext.h / CodegenContext.cpp       （已有，扩展）
├── libc/
│   ├── libc.h                       （新增）
│   ├── printf.c                     （新增）
│   ├── malloc.c                     （新增）
│   ├── string.c                     （新增）
│   └── stdlib.c                     （新增）
├── driver/
│   ├── CompilerDriver.h / CompilerDriver.cpp  （新增）
│   └── Linker.h / Linker.cpp                  （新增）
├── support/
│   ├── Log.h                        （已有）
│   ├── File.h / File.cpp            （已有）
│   ├── ScopeGuard.h                 （已有）
│   └── Utils.h / Utils.cpp          （已有）
└── main.cpp                         （已有，扩展）

tests/
├── CMakeLists.txt                   （测试构建配置）
├── frontend/
│   ├── test_lexer.cpp               （词法分析器单元测试）
│   ├── test_token_stream.cpp        （Token 流单元测试）
│   └── test_parser.cpp              （语法分析器单元测试）
├── preprocessor/
│   ├── test_preprocessor.cpp        （预处理器单元测试）
│   └── test_macro_table.cpp         （宏表单元测试）
├── ast/
│   ├── test_expr.cpp                （表达式 AST 单元测试）
│   ├── test_stmt.cpp                （语句 AST 单元测试）
│   ├── test_decl.cpp                （声明 AST 单元测试）
│   └── test_type.cpp                （类型系统单元测试）
├── sema/
│   ├── test_semantic_analyzer.cpp   （语义分析器单元测试）
│   └── test_diagnostic.cpp          （诊断系统单元测试）
├── codegen/
│   └── test_codegen_context.cpp     （代码生成单元测试）
├── libc/
│   ├── test_printf.cpp              （printf 内置函数测试）
│   ├── test_malloc.cpp              （malloc 内置函数测试）
│   └── test_string.cpp              （字符串函数测试）
├── driver/
│   ├── test_compiler_driver.cpp     （编译器驱动单元测试）
│   └── test_linker.cpp              （链接器单元测试）
├── integration/
│   ├── test_expressions.cpp         （表达式集成测试）
│   ├── test_statements.cpp          （语句集成测试）
│   ├── test_declarations.cpp        （声明集成测试）
│   └── test_end_to_end.cpp          （端到端编译测试）
└── resources/
    ├── test_input/                  （测试输入文件）
    └── expected/                    （预期输出文件）

resources/
├── main.c                           （综合测试文件）
└── main_min.c                       （最小测试文件）
```

---

## 3. Token 流

**新增文件**: `src/frontend/TokenStream.h`、`src/frontend/TokenStream.cpp`

### 接口

```cpp
class TokenStream {
    Lexer& lexer;
    std::vector<Token> buffer;
    size_t pos;

public:
    TokenStream(Lexer& lexer);

    const Token& peek(size_t offset = 0);  // 前瞻 N 个 token
    Token consume();                         // 前进并返回当前 token
    Token expect(TokenType type);           // 消费或报错
    bool match(TokenType type);             // 前瞻并消费（如果匹配）
    bool atEnd() const;
    const Token& current() const;
};
```

### 设计决策

- **惰性求值**: token 按需词法分析，不是全部预先分析
- **`peek(n)`**: 按需将最多 `pos + n` 个 token 词法分析到缓冲区
- **预处理器位于词法分析器和 Token 流之间**: 在语法分析器看到之前转换 token
- **不支持回退**: 语法分析器只需要前向前瞻

---

## 4. 预处理器

**新增文件**: `src/preprocessor/Preprocessor.h`、`src/preprocessor/Preprocessor.cpp`、`src/preprocessor/MacroTable.h`、`src/preprocessor/MacroTable.cpp`

### 支持的指令

| 指令 | 说明 |
|------|------|
| `#define NAME value` | 类对象宏 |
| `#define NAME(args) value` | 类函数宏 |
| `#define NAME(args...) value` | 可变参数宏（`__VA_ARGS__`） |
| `#undef NAME` | 取消宏定义 |
| `#include "file"` | 包含源文件（搜索相对路径） |
| `#include <file>` | 包含系统文件（搜索 include 路径） |
| `#ifdef NAME` | 条件编译（是否已定义？） |
| `#ifndef NAME` | 条件编译（是否未定义？） |
| `#elif expr` | else-if 条件 |
| `#else` | else 条件 |
| `#endif` | 结束条件 |
| `#pragma once` | 包含守卫约定 |
| `##` | token 拼接运算符 |
| `#` | 字符串化运算符 |

### 核心类型

```cpp
struct Macro {
    std::string name;
    std::vector<std::string> params;  // 类对象宏为空
    std::vector<Token> body;
    bool isVariadic;
};

class MacroTable {
    std::unordered_map<std::string, Macro> macros;
public:
    void define(const Macro& macro);
    void undefine(const std::string& name);
    const Macro* lookup(const std::string& name) const;
    bool isDefined(const std::string& name) const;
};

class Preprocessor {
    MacroTable macros;
    std::vector<std::string> includeStack;  // 循环包含守卫
    std::string sourceDir;
    std::vector<std::string> includePaths;

public:
    std::vector<Token> preprocess(Lexer& lexer);
};
```

### 实现方案

1. 预处理器从词法分析器接收原始 token 流
2. 扫描行首的 `#`
3. 遇到 `#define`: 将宏存入 MacroTable，后续出现时展开
4. 遇到 `#include`: 打开文件，词法分析，递归预处理，内联 token
5. 遇到 `#ifdef`/`#ifndef`: 求值条件，跳过假分支中的 token
6. 输出: 所有指令已解析的扁平 token 流

### Include 路径搜索

1. 当前文件相对路径（用于 `#include "file"`）
2. `-I` 指定的目录（按顺序）
3. 系统目录（用于 `#include <file>`）

---

## 5. 语法分析器扩展

语法分析器从消费原始 `Lexer` 切换为消费 `TokenStream`。

### 表达式解析 —— Pratt 优先级攀升

| 优先级 | 运算符 | 结合性 |
|--------|--------|--------|
| 1（最低） | `,` | 左 |
| 2 | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` | 右 |
| 3 | `? :`（三元） | 右 |
| 4 | `\|\|` | 左 |
| 5 | `&&` | 左 |
| 6 | `\|` | 左 |
| 7 | `^` | 左 |
| 8 | `&` | 左 |
| 9 | `==` `!=` | 左 |
| 10 | `<` `>` `<=` `>=` | 左 |
| 11 | `<<` `>>` | 左 |
| 12 | `+` `-` | 左 |
| 13 | `*` `/` `%` | 左 |
| 14（最高） | `!` `-` `+` `*` `&` `++` `--` `(类型转换)` `sizeof` | 右（一元） |

### 新增表达式 AST 节点

- `AssignmentExprAST` —— 所有复合赋值（`=`、`+=`、`-=`、`*=`、`/=`、`%=`、`&=`、`|=`、`^=`、`<<=`、`>>=`）
- `TernaryExprAST` —— `条件 ? 真值 : 假值`
- `CastExprAST` —— `(类型)表达式`
- `CommaExprAST` —— `表达式1, 表达式2`
- `PostfixIncDecExprAST` —— `i++`、`i--`
- `ArrayAccessExprAST` —— `数组[下标]`
- `MemberAccessExprAST` —— `结构体.字段`、`指针->字段`
- `SizeofExprAST` —— `sizeof(类型)`、`sizeof 表达式`
- `InitializerListExprAST` —— `{ 值1, 值2, ... }`

### 新增语句 AST 节点

- `IfStmtAST` —— `if (条件) 语句 else 语句`
- `WhileStmtAST` —— `while (条件) 语句`
- `DoWhileStmtAST` —— `do 语句 while (条件);`
- `ForStmtAST` —— `for (初始化; 条件; 更新) 语句`
- `SwitchStmtAST` —— `switch (表达式) { case 值: 语句 ... default: 语句 }`
- `BreakStmtAST` —— `break;`
- `ContinueStmtAST` —— `continue;`
- `GotoStmtAST` —— `goto 标签;`
- `LabelStmtAST` —— `标签: 语句`
- `NullStmtAST` —— `;`（空语句）

### 新增声明 AST 节点

- `ArrayDeclAST` —— `int a[10]`、`int a[] = {1,2,3}`
- `StructDeclAST` —— `struct S { int x; char y; }`
- `UnionDeclAST` —— `union U { int x; float y; }`
- `EnumDeclAST` —— `enum E { A, B, C }`
- `TypedefDeclAST` —— `typedef int MyInt`
- `ParamDeclAST` —— 扩展支持数组、指针、函数指针
- `ForwardDeclAST` —— `struct S;`（前向声明）

### 新增类型 AST 节点

- `ArrayType` —— 基础类型 + 大小（未指定大小时为 nullptr）
- `StructType` —— 名称 + 字段
- `UnionType` —— 名称 + 字段
- `EnumType` —— 名称 + 枚举值
- `FunctionType` —— 返回类型 + 参数类型
- `TypedefType` —— 另一个类型的别名
- `PointerType` —— 已有，扩展

### 语法分析器方法

```cpp
class Parser {
    TokenStream& stream;

    // 顶层
    std::unique_ptr<TranslationUnitAST> parseTranslationUnit();
    std::unique_ptr<DeclAST> parseDeclaration();
    std::unique_ptr<FunctionDeclAST> parseFunctionDecl(Type* returnType, std::string name);
    std::unique_ptr<DeclAST> parseVariableDecl(Type* type, std::string name);

    // 类型
    Type* parseType();
    Type* parseBaseType();
    Type* parseDerivedType(Type* base);

    // 语句
    std::unique_ptr<StmtAST> parseStmt();
    std::unique_ptr<StmtAST> parseCompoundStmt();
    std::unique_ptr<StmtAST> parseIfStmt();
    std::unique_ptr<StmtAST> parseWhileStmt();
    std::unique_ptr<StmtAST> parseForStmt();
    std::unique_ptr<StmtAST> parseSwitchStmt();

    // 表达式（Pratt）
    std::unique_ptr<ExprAST> parseExpr(int minPrec = 0);
    std::unique_ptr<ExprAST> parsePrimary();
    std::unique_ptr<ExprAST> parseUnary();
    std::unique_ptr<ExprAST> parsePostfix(std::unique_ptr<ExprAST> lhs);
    int getPrecedence(TokenType op);
};
```

---

## 6. 语义分析

**新增目录**: `src/sema/`

### 接口

```cpp
class SemanticAnalyzer {
    TypeContext& typeCtx;
    std::vector<Diagnostic> errors;

public:
    void analyze(TranslationUnitAST& ast);

    // 各 AST 节点的访问方法
    void visit(FunctionDeclAST& node);
    void visit(VarDeclAST& node);
    void visit(BinaryExprAST& node);
    void visit(CallExprAST& node);
    // ... 等
};
```

### 诊断类型

```cpp
struct Diagnostic {
    enum Level { Error, Warning };
    Level level;
    std::string message;
    SourceLocation loc;  // 文件、行、列
};
```

### 类型检查规则

| 运算 | 规则 |
|------|------|
| 算术运算（`+`、`-`、`*`、`/`、`%`） | 两个操作数必须是算术类型；结果为提升后的类型 |
| 比较运算（`==`、`!=`、`<`、`>`、`<=`、`>=`） | 两个操作数必须类型兼容；结果为 `int` |
| 逻辑运算（`&&`、`\|\|`） | 两个操作数必须是标量类型；结果为 `int` |
| 位运算（`&`、`\|`、`^`、`<<`、`>>`） | 两个操作数必须是整数类型；结果为提升后的类型 |
| 赋值（`=`、`+=` 等） | 右值必须可赋值给左值类型 |
| 函数调用 | 参数个数和类型必须与声明匹配 |
| 数组访问 | 下标必须是整数；操作数必须是数组或指针 |
| 指针运算 | 指针 +/- 整数；指针 - 指针 |
| 类型转换 | 显式标量类型之间的转换 |
| 解引用（`*ptr`） | 操作数必须是指针 |
| 取地址（`&x`） | 操作数必须是左值 |
| 成员访问（`s.field`） | 操作数必须是结构体/联合体 |
| 箭头访问（`p->field`） | 操作数必须是指向结构体/联合体的指针 |

### 名称解析

- 变量必须在使用前声明
- 函数必须在调用前声明（或通过 `extern`/前向声明）
- 作用域嵌套：全局 → 函数 → 块
- `typedef` 名称进入类型命名空间
- `struct`/`union`/`enum` 标签进入标签命名空间

---

## 7. 内置 libc

**新增目录**: `src/libc/`

### 核心函数

| 类别 | 函数 |
|------|------|
| I/O | `printf`、`fprintf`、`sprintf`、`snprintf`、`puts`、`putchar`、`fopen`、`fclose`、`fread`、`fwrite`、`fseek`、`ftell`、`feof`、`ferror` |
| 内存 | `malloc`、`free`、`calloc`、`realloc` |
| 字符串 | `strlen`、`strcmp`、`strncmp`、`strcpy`、`strncpy`、`strcat`、`strncat`、`strchr`、`strstr`、`memcpy`、`memmove`、`memset`、`memcmp` |
| 转换 | `atoi`、`atol`、`atof`、`strtol`、`strtoul`、`strtod` |
| 工具 | `exit`、`abs`、`rand`、`srand`、`qsort`、`bsearch` |
| 可变参数 | `va_start`、`va_arg`、`va_end`、`va_list`（类型） |

### 实现策略

- 每个函数用 C 语言实现，编译为 LLVM bitcode
- 链接为 `libc_builtin.a` 静态库
- 编译器在生成可执行文件时自动链接此库
- 内置函数会遮蔽系统 libc 中的同名函数

### 可变参数实现

`va_list` 实现为一个结构体，封装平台的可变参数传递机制：
```c
typedef struct {
    void* stack_ptr;
    int reg_count;
} va_list;
```

---

## 8. 多文件编译

**新增文件**: `src/driver/CompilerDriver.h`、`src/driver/CompilerDriver.cpp`、`src/driver/Linker.h`、`src/driver/Linker.cpp`

### 命令行接口

```
my_llvm_c [选项] 文件...
  -c              仅编译为 .o（不链接）
  -o <文件>       输出文件名
  -O <级别>       优化级别（0、1、2、3）
  -g              包含调试信息
  -E              仅预处理（输出展开后的源码）
  -S              仅输出 LLVM IR
  --emit-obj      输出目标文件
  --jit           JIT 执行（单文件且无 -c 时的默认行为）
  -v              详细输出
  -I <目录>       添加 include 搜索路径
  -D <名称>=<值>  定义宏
  -std=c11        语言标准（供将来使用）
```

### 编译流程

多文件编译流程：
1. 解析命令行参数
2. 对每个 `.c` 文件：
   - 词法分析 → 预处理 → 语法分析 → 语义分析 → 代码生成 → 优化 → 输出 `.o`
3. 如果不是 `-c`：链接所有 `.o` 文件 + 内置 libc → 最终可执行文件

### 链接器集成

- 检测系统链接器（`ld`、`ld.lld`、`ld.gold`）
- 构造正确的链接命令
- 链接内置 libc
- 主要支持 Linux，次要支持 macOS

---

## 9. 错误处理

### 策略

- 报告错误 + 文件/行/列信息，停止编译
- 不做错误恢复（按用户选择）
- 错误消息使用英文，人类可读

### 错误格式

```
error: <消息>
  --> 文件.c:行:列
```

### 错误示例

```
error: expected ';' after expression
  --> main.c:5:15

error: implicit declaration of function 'printf'
  --> main.c:3:1

error: cannot assign to const variable
  --> main.c:7:5

error: array subscript must be an integer
  --> main.c:10:8
```

---

## 10. 实现阶段

### 阶段 1：语法分析器 + 代码生成（核心语言）

**目标**: 解析并编译所有 C11 子集构造

**任务**:
1. 实现 `TokenStream` 类
2. 重写语法分析器使用 `TokenStream`
3. 添加 Pratt 表达式解析
4. 添加所有新 AST 节点（表达式、语句、声明、类型）
5. 扩展代码生成支持所有新 AST 节点
6. **编写单元测试**:
   - `test_token_stream.cpp` —— Token 流单元测试
   - `test_parser.cpp` —— 语法分析器单元测试（表达式、语句、声明）
   - `test_expr.cpp` —— 表达式 AST 单元测试
   - `test_stmt.cpp` —— 语句 AST 单元测试
   - `test_decl.cpp` —— 声明 AST 单元测试
   - `test_type.cpp` —— 类型系统单元测试
7. 用 `resources/main.c` 集成测试

**预估规模**: 约 2000-3000 行新增/修改代码 + 1500-2000 行测试代码

### 阶段 2：语义分析

**目标**: 类型检查和名称解析

**任务**:
1. 实现 `SemanticAnalyzer` 类
2. 实现 `Diagnostic` 系统
3. 添加所有运算符的类型检查规则
4. 添加名称解析和作用域管理
5. 添加带文件/行/列的错误报告
6. **编写单元测试**:
   - `test_semantic_analyzer.cpp` —— 语义分析器单元测试（类型检查、名称解析、作用域）
   - `test_diagnostic.cpp` —— 诊断系统单元测试（错误格式、位置信息）

**预估规模**: 约 1500-2000 行 + 1000-1500 行测试代码

### 阶段 3：预处理器

**目标**: 完整的 C11 预处理器

**任务**:
1. 实现 `MacroTable` 类
2. 实现 `Preprocessor` 类
3. 添加 `#define`（类对象、类函数、可变参数）
4. 添加 `#include`（含 include 路径搜索）
5. 添加 `#ifdef`/`#ifndef`/`#elif`/`#else`/`#endif`
6. 添加 token 拼接（`##`）和字符串化（`#`）
7. 添加 `#pragma once`
8. **编写单元测试**:
   - `test_preprocessor.cpp` —— 预处理器单元测试（宏展开、条件编译、包含指令）
   - `test_macro_table.cpp` —— 宏表单元测试（定义、查找、取消定义）

**预估规模**: 约 1500-2000 行 + 1200-1500 行测试代码

### 阶段 4：内置 libc

**目标**: 核心 C 库函数

**任务**:
1. 实现 `printf` 系列
2. 实现 `malloc`/`free`/`calloc`/`realloc`
3. 实现字符串函数
4. 实现转换函数
5. 实现工具函数
6. 实现可变参数支持
7. 将内置 libc 链接到编译器输出
8. **编写单元测试**:
   - `test_printf.cpp` —— printf 内置函数测试（格式化、参数）
   - `test_malloc.cpp` —— malloc 内置函数测试（分配、释放、边界）
   - `test_string.cpp` —— 字符串函数测试（所有字符串操作函数）

**预估规模**: 约 2000-3000 行 + 1500-2000 行测试代码

### 阶段 5：多文件编译

**目标**: 分离编译和链接

**任务**:
1. 实现 `CompilerDriver` 类
2. 添加命令行参数解析
3. 添加 `-c`、`-o`、`-I`、`-D` 选项
4. 实现 `Linker` 类
5. 添加目标文件链接
6. **编写单元测试**:
   - `test_compiler_driver.cpp` —— 编译器驱动单元测试（参数解析、文件处理）
   - `test_linker.cpp` —— 链接器单元测试（链接命令、符号解析）
7. 用多文件程序集成测试

**预估规模**: 约 1000-1500 行 + 800-1200 行测试代码

---

## 11. 测试策略

### 测试框架：Google Test

引入 Google Test（gtest）作为单元测试框架，通过 vcpkg 安装：

```bash
vcpkg install gtest:x64-linux
```

**CMake 集成**：
```cmake
find_package(GTest REQUIRED)
target_link_libraries(test_target PRIVATE GTest::gtest_main GTest::gmock)
```

### 构建配置变更

**根 CMakeLists.txt 新增**：
```cmake
# 测试选项
option(BUILD_TESTS "Build unit tests" ON)

# 添加测试子目录
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

**vcpkg.json 新增依赖**：
```json
{
    "dependencies": [
        "llvm",
        "spdlog",
        "gtest"
    ]
}
```

**构建命令**：
```bash
# 仅构建编译器
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# 构建编译器 + 测试
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
cmake --build build

# 运行测试
ctest --test-dir build --output-on-failure

# 生成覆盖率报告
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="--coverage"
cmake --build build
ctest --test-dir build
gcovr --root .. --filter ../src/ --html-details coverage.html
```

### 测试目录结构

```
tests/
├── CMakeLists.txt                    # 测试构建配置
├── frontend/
│   ├── test_lexer.cpp                # 词法分析器单元测试
│   ├── test_token_stream.cpp         # Token 流单元测试
│   └── test_parser.cpp               # 语法分析器单元测试
├── preprocessor/
│   ├── test_preprocessor.cpp         # 预处理器单元测试
│   └── test_macro_table.cpp          # 宏表单元测试
├── ast/
│   ├── test_expr.cpp                 # 表达式 AST 单元测试
│   ├── test_stmt.cpp                 # 语句 AST 单元测试
│   ├── test_decl.cpp                 # 声明 AST 单元测试
│   └── test_type.cpp                 # 类型系统单元测试
├── sema/
│   ├── test_semantic_analyzer.cpp    # 语义分析器单元测试
│   └── test_diagnostic.cpp           # 诊断系统单元测试
├── codegen/
│   └── test_codegen_context.cpp      # 代码生成单元测试
├── libc/
│   ├── test_printf.cpp               # printf 内置函数测试
│   ├── test_malloc.cpp               # malloc 内置函数测试
│   └── test_string.cpp               # 字符串函数测试
├── driver/
│   ├── test_compiler_driver.cpp      # 编译器驱动单元测试
│   └── test_linker.cpp               # 链接器单元测试
├── integration/
│   ├── test_expressions.cpp          # 表达式集成测试
│   ├── test_statements.cpp           # 语句集成测试
│   ├── test_declarations.cpp         # 声明集成测试
│   └── test_end_to_end.cpp           # 端到端编译测试
└── resources/
    ├── test_input/                   # 测试输入文件
    └── expected/                     # 预期输出文件
```

### 单元测试编写规范

**每个模块必须包含**：
1. **正常路径测试** —— 验证正确输入产生正确输出
2. **边界条件测试** —— 空输入、极大值、极小值
3. **错误路径测试** —— 验证错误检测和报告
4. **回归测试** —— 防止已修复的 bug 再次出现

**测试命名规范**：
```cpp
TEST(LexerTest, SingleLineComment) { ... }
TEST(LexerTest, MultiLineComment) { ... }
TEST(ParserTest, BinaryExpressionPrecedence) { ... }
TEST(PreprocessorTest, ObjectLikeMacro) { ... }
TEST(SemanticAnalyzerTest, TypeMismatchError) { ... }
```

**测试辅助工具**：
```cpp
// 测试用 Token 流构造器
TokenStream createTokenStream(const std::string& source);

// 测试用 AST 构造器
std::unique_ptr<ExprAST> createTestExpr(const std::string& source);

// LLVM IR 验证辅助
bool verifyModule(llvm::Module& module);

// 输出比较辅助
std::string captureStdout(const std::string& source);
```

### 各模块测试要求

#### 词法分析器测试（test_lexer.cpp）

```cpp
// 基本 token 类型
TEST(LexerTest, IntegerLiteral) { ... }
TEST(LexerTest, FloatLiteral) { ... }
TEST(LexerTest, StringLiteral) { ... }
TEST(LexerTest, CharLiteral) { ... }
TEST(LexerTest, Identifier) { ... }

// 关键字
TEST(LexerTest, Keywords) { ... }  // 所有 27 个关键字

// 运算符
TEST(LexerTest, ArithmeticOperators) { ... }
TEST(LexerTest, ComparisonOperators) { ... }
TEST(LexerTest, LogicalOperators) { ... }
TEST(LexerTest, BitwiseOperators) { ... }
TEST(LexerTest, AssignmentOperators) { ... }

// 注释
TEST(LexerTest, SingleLineComment) { ... }
TEST(LexerTest, MultiLineComment) { ... }
TEST(LexerTest, NestedComment) { ... }

// 边界条件
TEST(LexerTest, EmptyInput) { ... }
TEST(LexerTest, UnterminatedString) { ... }
TEST(LexerTest, UnterminatedComment) { ... }
```

#### Token 流测试（test_token_stream.cpp）

```cpp
TEST(TokenStreamTest, PeekDoesNotConsume) { ... }
TEST(TokenStreamTest, ConsumeAdvancesPosition) { ... }
TEST(TokenStreamTest, ExpectValidToken) { ... }
TEST(TokenStreamTest, ExpectInvalidToken) { ... }
TEST(TokenStreamTest, MatchAndConsume) { ... }
TEST(TokenStreamTest, AtEndDetection) { ... }
```

#### 语法分析器测试（test_parser.cpp）

```cpp
// 表达式解析
TEST(ParserTest, PrimaryExpression) { ... }
TEST(ParserTest, BinaryExpressionAddition) { ... }
TEST(ParserTest, BinaryExpressionPrecedence) { ... }
TEST(ParserTest, UnaryExpression) { ... }
TEST(ParserTest, AssignmentExpression) { ... }
TEST(ParserTest, TernaryExpression) { ... }
TEST(ParserTest, CastExpression) { ... }
TEST(ParserTest, ArrayAccess) { ... }
TEST(ParserTest, MemberAccess) { ... }

// 语句解析
TEST(ParserTest, IfStatement) { ... }
TEST(ParserTest, WhileStatement) { ... }
TEST(ParserTest, ForStatement) { ... }
TEST(ParserTest, SwitchStatement) { ... }
TEST(ParserTest, ReturnStatement) { ... }

// 声明解析
TEST(ParserTest, VariableDeclaration) { ... }
TEST(ParserTest, FunctionDeclaration) { ... }
TEST(ParserTest, StructDeclaration) { ... }
TEST(ParserTest, EnumDeclaration) { ... }
TEST(ParserTest, TypedefDeclaration) { ... }

// 错误处理
TEST(ParserTest, SyntaxErrorReporting) { ... }
TEST(ParserTest, UnexpectedToken) { ... }
```

#### 预处理器测试（test_preprocessor.cpp）

```cpp
// 宏定义
TEST(PreprocessorTest, ObjectLikeMacro) { ... }
TEST(PreprocessorTest, FunctionLikeMacro) { ... }
TEST(PreprocessorTest, VariadicMacro) { ... }
TEST(PreprocessorTest, MacroExpansion) { ... }
TEST(PreprocessorTest, RecursiveMacro) { ... }
TEST(PreprocessorTest, TokenPasting) { ... }
TEST(PreprocessorTest, Stringification) { ... }

// 条件编译
TEST(PreprocessorTest, IfDef) { ... }
TEST(PreprocessorTest, IfNDef) { ... }
TEST(PreprocessorTest, Elif) { ... }
TEST(PreprocessorTest, Else) { ... }

// 包含指令
TEST(PreprocessorTest, IncludeLocal) { ... }
TEST(PreprocessorTest, IncludeSystem) { ... }
TEST(PreprocessorTest, IncludeGuard) { ... }
TEST(PreprocessorTest, PragmaOnce) { ... }
```

#### 语义分析器测试（test_semantic_analyzer.cpp）

```cpp
// 类型检查
TEST(SemanticAnalyzerTest, ArithmeticTypeCheck) { ... }
TEST(SemanticAnalyzerTest, ComparisonTypeCheck) { ... }
TEST(SemanticAnalyzerTest, LogicalTypeCheck) { ... }
TEST(SemanticAnalyzerTest, AssignmentTypeCheck) { ... }

// 名称解析
TEST(SemanticAnalyzerTest, VariableDeclaration) { ... }
TEST(SemanticAnalyzerTest, FunctionDeclaration) { ... }
TEST(SemanticAnalyzerTest, UndeclaredVariable) { ... }
TEST(SemanticAnalyzerTest, RedeclarationError) { ... }

// 作用域
TEST(SemanticAnalyzerTest, NestedScope) { ... }
TEST(SemanticAnalyzerTest, FunctionScope) { ... }
TEST(SemanticAnalyzerTest, BlockScope) { ... }

// 错误报告
TEST(SemanticAnalyzerTest, ErrorLocation) { ... }
TEST(SemanticAnalyzerTest, MultipleErrors) { ... }
```

#### 代码生成测试（test_codegen_context.cpp）

```cpp
// 类型映射
TEST(CodegenTest, IntTypeMapping) { ... }
TEST(CodegenTest, FloatTypeMapping) { ... }
TEST(CodegenTest, PointerTypeMapping) { ... }

// 表达式生成
TEST(CodegenTest, BinaryExpression) { ... }
TEST(CodegenTest, UnaryExpression) { ... }
TEST(CodegenTest, FunctionCall) { ... }

// 语句生成
TEST(CodegenTest, ReturnStatement) { ... }
TEST(CodegenTest, IfStatement) { ... }
TEST(CodegenTest, LoopStatement) { ... }

// LLVM IR 验证
TEST(CodegenTest, ModuleVerification) { ... }
TEST(CodegenTest, Optimization) { ... }
```

#### 内置 libc 测试（test_libc.cpp）

```cpp
// printf 测试
TEST(LibcTest, PrintfInt) { ... }
TEST(LibcTest, PrintfFloat) { ... }
TEST(LibcTest, PrintfString) { ... }
TEST(LibcTest, PrintfFormat) { ... }

// malloc 测试
TEST(LibcTest, MallocFree) { ... }
TEST(LibcTest, Calloc) { ... }
TEST(LibcTest, Realloc) { ... }

// 字符串函数测试
TEST(LibcTest, Strlen) { ... }
TEST(LibcTest, Strcmp) { ... }
TEST(LibcTest, Strcpy) { ... }
TEST(LibcTest, Memcpy) { ... }
```

#### 集成测试（test_end_to_end.cpp）

```cpp
// 完整编译流程
TEST(EndToEndTest, SimpleProgram) { ... }
TEST(EndToEndTest, FunctionDefinition) { ... }
TEST(EndToEndTest, ControlFlow) { ... }
TEST(EndToEndTest, ArrayAccess) { ... }
TEST(EndToEndTest, StructUsage) { ... }

// 预处理器集成
TEST(EndToEndTest, MacroExpansion) { ... }
TEST(EndToEndTest, IncludeFile) { ... }

// 多文件编译
TEST(EndToEndTest, SeparateCompilation) { ... }
TEST(EndToEndTest, ExternalLinkage) { ... }
```

### 测试辅助工具

**测试数据生成器**：
```cpp
// 生成各种 C 代码片段用于测试
class TestCodeGenerator {
public:
    static std::string generateSimpleFunction();
    static std::string generateControlFlow();
    static std::string generateExpressions();
    static std::string generateDeclarations();
};
```

**LLVM IR 验证器**：
```cpp
// 验证生成的 LLVM IR 是否有效
bool validateLLVMIR(const std::string& ir);
bool validateModule(llvm::Module& module);
std::string optimizeModule(llvm::Module& module, int optLevel);
```

**输出捕获器**：
```cpp
// 捕获编译器输出用于测试
class OutputCapture {
public:
    std::string captureStdout(const std::string& source);
    std::string captureStderr(const std::string& source);
    int captureExitCode(const std::string& source);
};
```

### 测试运行配置

**CMakeLists.txt（tests 目录）**：
```cmake
find_package(GTest REQUIRED)
include(GoogleTest)

# 收集所有测试源文件
file(GLOB_RECURSE TEST_SOURCES "*.cpp")

# 创建测试可执行文件
add_executable(compiler_tests ${TEST_SOURCES})
target_link_libraries(compiler_tests
    PRIVATE
        my_llvm_c_lib  # 编译器库
        GTest::gtest_main
        GTest::gmock
        LLVM
)

# 注册测试
gtest_discover_tests(compiler_tests)
```

**测试覆盖率**：
```bash
# 启用覆盖率构建
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="--coverage"
cmake --build build
ctest --test-dir build

# 生成覆盖率报告
gcovr --root .. --filter ../src/ --html-details coverage.html
```

### 待创建的测试文件

- `tests/expressions/` —— 所有表达式类型
- `tests/statements/` —— 所有语句类型
- `tests/declarations/` —— 所有声明类型
- `tests/preprocessor/` —— 宏展开、#include、#ifdef
- `tests/libc/` —— 内置函数测试
- `tests/multi-file/` —— 分离编译测试

---

## 12. 风险评估

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 语法分析器复杂度失控 | 高 | Pratt 解析保持表达式处理的清晰性 |
| 预处理器与语法分析器的交互 | 中 | 清晰的 token 流分离 |
| 类型系统复杂度 | 中 | 从简单类型开始，逐步扩展 |
| 内置 libc 实现工作量 | 高 | 从 printf/malloc 开始，逐步添加函数 |
| 多文件链接正确性 | 中 | 使用系统链接器，先用简单案例测试 |
| LLVM API 版本间变更 | 低 | 固定使用 LLVM 18，尽早测试 |
| 单元测试覆盖不足 | 中 | 每个模块必须达到 80% 代码覆盖率 |
| 测试用例维护成本 | 中 | 使用参数化测试减少重复 |
| 集成测试复杂度 | 中 | 从简单端到端测试开始，逐步增加复杂度 |
| 测试环境依赖 | 低 | 使用 vcpkg 管理 gtest 依赖 |

---

## 13. 成功标准

编译器在以下情况下被视为"完成"：

1. 能无错误地编译 `resources/main.c`
2. 能运行编译后的程序并产生正确输出
3. 能处理本规范中列出的所有 C11 子集构造
4. 能通过预处理器处理标准 C 头文件
5. 能编译多文件程序并正确链接
6. 能为无效代码产生有意义的错误消息
7. **所有单元测试通过**（`ctest` 命令执行成功）
8. **代码覆盖率 ≥ 80%**（使用 gcovr 生成覆盖率报告）
9. **无内存泄漏**（使用 Valgrind 或 AddressSanitizer 验证）
10. **编译性能**：编译 1000 行 C 代码 < 1 秒
