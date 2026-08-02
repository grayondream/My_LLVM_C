# cxxopts 参数解析集成设计

## 概述
使用 cxxopts 库替换手写的参数解析代码，支持 clang 风格的命令行参数。

## 支持的参数

| 参数 | 说明 | 示例 |
|------|------|------|
| `-c` | 仅编译，不链接 | `my_llvm_c -c test.c` |
| `-o <file>` | 指定输出文件 | `my_llvm_c -c -o out.o test.c` |
| `-S` | 仅输出汇编/IR | `my_llvm_c -S test.c` |
| `-E` | 仅运行预处理器 | `my_llvm_c -E test.c` |
| `-I <path>` | 添加头文件搜索路径 | `my_llvm_c -I/usr/include test.c` |
| `-D <macro>=<value>` | 定义宏 | `my_llvm_c -DDEBUG=1 test.c` |
| `-O <level>` | 优化级别 (0/1/2/3) | `my_llvm_c -O2 test.c` |
| `-g` | 包含调试信息 | `my_llvm_c -g test.c` |
| `-v` | 详细输出 | `my_llvm_c -v test.c` |
| `-Wall` | 启用所有警告 | `my_llvm_c -Wall test.c` |
| `-Werror` | 将警告视为错误 | `my_llvm_c -Werror test.c` |
| `-std=<standard>` | C 标准 (c99/c11/c17) | `my_llvm_c -std=c11 test.c` |
| `-fsyntax-only` | 仅语法检查 | `my_llvm_c -fsyntax-only test.c` |
| `-l <lib>` | 链接库 | `my_llvm_c -l m test.c` |
| `-L <path>` | 库搜索路径 | `my_llvm_c -L/usr/lib test.c` |
| `-h, --help` | 显示帮助 | `my_llvm_c -h` |

## 实现步骤

1. 添加 cxxopts 依赖到 vcpkg.json
2. 修改 CompilerDriver 类使用 cxxopts
3. 更新测试用例
4. 验证所有测试通过

## 文件变更

- `vcpkg.json`: 添加 cxxopts 依赖
- `src/CMakeLists.txt`: 链接 cxxopts
- `src/driver/CompilerDriver.h`: 更新类定义
- `src/driver/CompilerDriver.cpp`: 使用 cxxopts 解析参数
- `tests/driver/test_compiler_driver.cpp`: 更新测试用例
