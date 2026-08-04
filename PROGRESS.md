# My_LLVM_C 项目进度

## 项目概述

将基于 LLVM 的 C 编译器从 `int main() { return 1+3; }` 扩展为完整 C11 子集编译器。

## 已完成功能

### 阶段 0: 测试基础设施
- [x] Google Test 框架集成
- [x] CMake 测试配置

### 阶段 1: Token 流
- [x] TokenStream 类实现（带缓冲的前瞻流）

### 阶段 2: 语法分析器
- [x] 新 AST 节点类型（表达式、语句、声明、类型）
- [x] Pratt 表达式解析器（优先级攀升）
- [x] 语句解析器（if/else、while、for、switch 等）
- [x] 声明和类型解析器（struct、enum、typedef 等）
- [x] 代码生成支持新 AST 节点

### 阶段 3: 语义分析
- [x] 诊断系统（错误/警告格式化）
- [x] 语义分析器（类型检查、名称解析）

### 阶段 4: 预处理器
- [x] 宏表（MacroTable）
- [x] 预处理器（#define、#ifdef、宏展开）

### 阶段 5: 内置 libc
- [x] printf/sprintf
- [x] malloc/free/calloc/realloc
- [x] strlen/strcmp/strcpy/memcpy/memset/memcmp

### 阶段 6: 多文件编译
- [x] CompilerDriver（命令行参数解析）
- [x] Linker（系统链接器检测和调用）

### 阶段 7: 测试
- [x] 端到端编译测试

### 阶段 8: 完善
- [x] 改进错误消息（20+ 处改进）
- [x] 代码覆盖率验证

### 阶段 9: const/constexpr 支持
- [x] Lexer: TOKEN_CONSTEXPR 关键字识别
- [x] Parser: const/constexpr 声明解析
- [x] Semantic Analyzer: const 赋值检查、constexpr 常量折叠
- [x] Codegen: const/constexpr 变量代码生成

## 测试结果

- **388/388 测试通过**
- **代码覆盖率 62%**

### 模块覆盖率

| 模块 | 覆盖率 |
|------|--------|
| ast/Stmt.cpp | 95% |
| ast/Type.cpp | 95% |
| ast/Symbol.cpp | 100% |
| frontend/TokenStream.cpp | 90% |
| frontend/Lexer.cpp | 81% |
| frontend/Parser.cpp | 77% |
| libc/string.cpp | 100% |
| libc/malloc.cpp | 95% |
| libc/printf.cpp | 78% |
| preprocessor/MacroTable.cpp | 100% |
| preprocessor/Preprocessor.cpp | 73% |
| sema/SemanticAnalyzer.cpp | 41% |
| codegen/CodegenContext.cpp | 60% |
| driver/CompilerDriver.cpp | 72% |
| driver/Linker.cpp | 77% |

## Git 提交历史

```
243b67b improve error messages in parser and semantic analyzer
241ef82 Add end-to-end compilation tests with JIT execution
e210f46 feat: implement Linker for multi-file compilation
0cdd8d1 feat: implement CompilerDriver for CLI argument parsing
2d4a145 feat(libc): implement core libc functions
9cf4fda feat(preprocessor): implement Preprocessor class
9ade251 feat: implement MacroTable for preprocessor
f4ac082 feat(sema): implement semantic analyzer
f1e31ea feat: add Diagnostic system for error reporting
49f9ca8 feat: extend codegen for new AST nodes
63cafa7 feat: implement declaration and type parsing
5876147 feat: implement statement parser
17d8667 feat: implement Pratt expression parser
bca28b2 feat: add new AST node types
666c2bf feat: add TokenStream with lookahead support
7d25549 feat: add Google Test framework
```

## 支持的 C11 子集特性

### 表达式
- 整数、浮点、字符、字符串字面量
- 变量引用
- 二元运算（算术、比较、逻辑、位运算）
- 一元运算（-、+、!、*、&）
- 赋值（=、+=、-=、*=、/= 等）
- 三元运算（? :）
- 类型转换（(type)expr）
- 逗号运算
- 后缀自增自减（i++、i--）
- 数组访问（a[i]）
- 成员访问（s.field、p->field）
- sizeof 运算符
- 函数调用

### 语句
- 表达式语句
- 复合语句（{ ... }）
- return 语句
- if/else 语句
- while 循环
- do-while 循环
- for 循环
- switch/case/default
- break、continue、goto
- 标签语句

### 声明
- 变量声明（带可选初始化）
- const 变量声明
- constexpr 变量声明（含常量折叠）
- 函数声明和定义
- 数组声明
- struct 声明
- union 声明
- enum 声明
- typedef
- 前向声明

### 类型
- void、int、float、double、char、bool
- 指针类型
- 数组类型
- 结构体类型
- 联合体类型
- 枚举类型
- 函数类型
- typedef 类型

### 预处理器
- #define（对象宏、函数宏、可变参数宏）
- #undef
- #ifdef / #ifndef / #elif / #else / #endif
- 宏展开

### 内置 libc
- printf、sprintf
- malloc、free、calloc、realloc
- strlen、strcmp、strcpy、memcpy、memset、memcmp

### 多文件编译
- -c（仅编译）
- -o（输出文件）
- -I（include 路径）
- -D（定义宏）
- 系统链接器检测和调用
