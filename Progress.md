# 进展记录

## 背景

- 任务来源：用户要求「根据 TODO 继续完善」。
- 项目：My_LLVM_C / SafeModern C 1.0，基于 LLVM 的 C 风格编译器；`TODO.md` 为唯一需求与进度清单。
- 本次选择：TODO 中的 **LEX-13 + LEX-17** —— 词法诊断与错误恢复。诊断码 `E0001`（非法字符）、`E0002`（未闭合字面量）、`E0003`（整型溢出）此前已在 `src/sema/Diagnostic.cpp` 注册，但词法器从未产出。

## 目标

- 为词法器增加结构化诊断：非法字符（E0001）、未闭合字面量（E0002）、整型字面量超过 64 位（E0003）。
- 出错后能恢复继续扫描，不抛异常、不进入死循环。
- 驱动层（`CompilerDriver` / `ModuleLoader` / `StdPrelude`）上报诊断并使编译失败。
- 验收：新增测试先 RED 后 GREEN，且全量测试无回归。

---

## 2026-09-27 21:35 完成 LEX-13 + LEX-17 词法诊断

- 改动摘要（文件路径）：
  - `src/frontend/Lexer.h` / `src/frontend/Lexer.cpp`：
    - 新增 `getDiagnostics()` 与 `report()`（替换原抛异常的 `error()`）。
    - **E0001**：未知字符报错后跳过该字符并继续扫描。
    - **E0002**：未闭合字符串 / 字符 / 原始字符串 / 块注释；同时修复了未闭合字符串、字符在 EOF 处**死循环**的缺陷。
    - **E0003**：二 / 八 / 十六 / 十进制均按 64 位无符号累加并检测溢出，超过 64 位报错一次。
    - 新增 `skipTrivia()` 统一跳过空白与注释，修复「行注释后的空行被误判为非法字符」的潜在缺陷。
  - `src/driver/CompilerDriver.cpp`、`src/driver/ModuleLoader.cpp`、`src/driver/StdPrelude.cpp`：上报词法诊断并中止编译。
  - 测试：`tests/frontend/test_lexer.cpp`（12 项）、`tests/driver/test_compiler_driver.cpp`（2 项）、`tests/sema/test_diagnostic_snapshot.cpp` 及快照 `tests/diagnostics/snapshots/lexical_invalid_character.txt`。
  - 文档：`TODO.md`（LEX-13 → `[x]`；LEX-17 更新为部分完成；进度快照更新为 698 测试）、`docs/spec/conversions.md §7`。
- 验证方式与结果：
  - 按 TDD：先写失败测试（6 项断言失败、2 项超时挂起），实现后全绿。
  - `ctest` 全量 **698** 通过（原 683 + 新增 15），0 失败。
  - 驱动诊断格式实测：`file:line:col: error[E0001]: invalid character '@'`。
- 遗留问题 / 下一步：
  - LEX-17 的**数字分隔符位置规则**（`1__2` / `_1` / `1_` 是否合法）尚未校验。
  - LEX-15（字面量默认类型与后缀映射）未做；当前 `TokenValue` 仅存 `int`，大于 `int` 的字面量仍会被截断，属独立条目。

---

## 2026-09-27 22:40 收尾词法：LEX-17 分隔符规则 + LEX-15 后缀/类型映射（词法层）

- 背景：上一轮 LEX-13/17 遗留「`_` 位置规则未校验」，且 LEX-15 未做。经 brainstorming 分类为 bounded，用户选择「仅词法层」范围并批准设计。
- 改动摘要（文件路径）：
  - `src/frontend/Token.h` / `Token.cpp`：新增 `enum class LiteralKind`（`Int/UInt/Long/ULong/Float16/Float32/Float64/Float128`）、`Token::literalKind` 字段与 `literalKindName()` 映射表。
  - `src/frontend/Lexer.h` / `Lexer.cpp`：
    - **LEX-17**：新增 `validateDigitSeparators()`，`_` 仅当左右均为当前进制数字时合法；违规报 **E0004** 并继续（恢复）。
    - **LEX-15**：`consumeIntegerSuffix()`（`u`/`l`/`ul`/`lu`，各进制通用）与 `consumeFloatSuffix()`（`f`/`F`、`f16`/`f32`/`f64`/`f128`，兼容 `l`/`L`）；无后缀默认整型 `int`、浮点 `float64`；重写 `scanNumber()` 以在计算值前剥离后缀。
  - `src/sema/Diagnostic.h` / `Diagnostic.cpp`：注册 `LexInvalidDigitSeparator` → `E0004`。
  - 测试：`tests/frontend/test_lexer.cpp` 新增 `LexerNumberSuffixTest.*`（5 项）与 `LexerSeparatorTest.*`（6 项）；`tests/sema/test_diagnostic_snapshot.cpp` + 快照 `lexical_digit_separator.txt`。
  - 文档：`docs/spec/grammar.ebnf`（分隔符产生式收紧、后缀去 `[plan]`、补裸 `f`）、`keywords.md`、`conversions.md §7`、`TODO.md`（LEX-17 → `[x]`，LEX-15 → `[~]` 并注明消费端待补）。
- 验证方式与结果：
  - TDD：先写测试观察 RED（9 失败），实现后全绿。
  - `ctest` 全量 **710** 通过（上轮 698 + 新增 12），0 失败；编译无警告。
  - 实测评测：`snapshot.c:2:19: error[E0004]: misplaced digit separator '_'`。
- 遗留问题 / 下一步：
  - **LEX-15 消费端**：AST/sema/codegen 仍按 `int`/`double` 处理字面量（`NumberExprAST::value` 为 `int`，codegen 固定 `APInt(32)`），大整数/无符号字面量的真实类型未生效，`TokenValue` 也未扩宽——应在后续条目（如 TYP-23 收口）实现。
  - 未做无效后缀诊断（避免与尚未实现的指数 `1e10` 冲突）；浮点指数仍未实现（LEX-05）。

---

## 2026-09-27 23:30 补齐浮点指数（LEX-05）

- 背景：上轮收尾词法时发现 `1e10` 会被切成 `1` + 标识符 `e10`；LEX-05（浮点字面量含指数）虽有规范但未实现。经 brainstorming 分类为 bounded，用户批准设计。
- 改动摘要（文件路径）：
  - `src/frontend/Lexer.cpp`（`scanNumber` 十进制分支）：新增指数扫描——`e`/`E` + 可选 `+`/`-` + 十进制数字（数字间允许 `_`）；`isfloat = 有小数点 || 有指数`；传给 `std::stod` 的字符串改为按位置截取数值段（后缀前）并剔除 `_`，从而支持 `1e10`/`1.5e-3`/`2E+4`。
  - 宽容策略（不新增诊断码）：`e` 后不足一个数字则不构成指数（但后跟 `_` 时会捕获以便报 E0004），故 `1e`→`NUMBER 1`+`IDENT e`、`1e+x` 保持切分；`0x1e10` 因 `e` 是十六进制数字不受影响。
  - 测试：`tests/frontend/test_lexer.cpp` 新增 `LexerFloatExponentTest`（5 项：无小数点指数、指数+后缀、十六进制 `0x1e10`、不完整指数宽容、指数内分隔符 `1e1_0` 合法 / `1e_10` 报 E0004）。
  - 文档：`TODO.md`（LEX-05 → `[~]` 并注明剩余“结尾点”形式；进度快照 715）、`docs/spec/grammar.ebnf`（补 LEX-05 `[impl]` 注释与已知差异）、`docs/spec/conversions.md §7`（指数规则）。
- 验证方式与结果：
  - TDD：先写测试观察 RED（3 项失败：两个 token 数不符、一个缺 E0004），实现后全绿。
  - `ctest` 全量 **715** 通过（上轮 710 + 新增 5），0 失败；编译无警告。
- 遗留问题 / 下一步：
  - 「结尾点」浮点 `1.` / `1.e3` 与十六进制浮点 `0x1p3` 仍未实现（已在 TODO LEX-05 注明）。
  - LEX-15 消费端（AST/sema/codegen 按字面量类型处理）仍待后续条目。

---

## 2026-09-28 定宽类型统一 + LEX-15 消费端（设计/计划阶段）

- 背景：推进 LEX-15 消费端时，用户决定把数值类型统一为定宽集合，删除 `int`/`float`/`double` 关键字与 `TypeKind::Int/Float/Double`，并新增 `Float16/Float128`。
- 产出：
  - 设计文档 `docs/spec/fixed-width-types.md`（已提交 `3e2119f`）。
  - 实施计划 `docs/superpowers/plans/2026-09-28-fixed-width-types.md`（本地，`docs/superpowers/` 被 gitignore）。
- 计划要点：Phase 1 迁移语言写法（别名暂留、测试全绿）→ Phase 2 分小步删除别名/非定宽类型（每步可编译可测）→ Phase 3 LEX-15 消费端（Token 扩宽→AST 带 kind→sema 映射→codegen 定宽），共 13 个 Task。
- 状态：等待用户选择执行方式（subagent-driven / native）后开始实现。
- 遗留问题 / 下一步：按计划执行；完成后追加 Progress 并更新 TODO/规范文档。

## 2026-09-28 22:40 Task 2 完成：测试内语言源码迁移到定宽写法

- 背景：定宽类型统一计划 Phase 1 第二任务，把测试 `.cpp` 中嵌入字符串（`"..."` / `R"(...)"`）的 SafeModern C 源码 `int/float/double` 迁移为 `int32/float32/float64`，为 Task 7 删除旧关键字铺路。
- 完成事项（提交 `c444391`，33 个文件，592+/592-）：
  - 用只改字符串字面量内容的脚本批量迁移，并保留非语言源码字符串：`literalKindName(LiteralKind::Int), "int"`（Task 10）、`message.find("int"/"float")`（Task 6）、`conv.find("enum ↔ int")`（Task 3）。
  - 连带改动（测试全绿所需）：`test_parser.cpp` 的 `TypeKind::Int/Float/Double` 断言 → `Int32/Float32/Float64`；`test_lexer.cpp` 的 `TOKEN_INT`→`TOKEN_INT32` 及列号；`test_source_location.cpp` 列号；4 个诊断快照用 `SMC_UPDATE_SNAPSHOTS=1` 重新生成。
  - Phase 1 变通：类方法实参字面量改显式强转 `f.setX((int32)42)`（7 处，因 `resolveMethod` 指针相等匹配而字面量仍为 `int`），Task 5 后可回退。
- 验证方式与结果：
  - `cd build && cmake --build . -j$(nproc) && ctest` → **100% passed, 0 failed, 715/715**（2 个 ConstexprFunctionE2E 按既有基线 Skip）。
  - `grep -rn --include=*.cpp 'int main' tests` 无输出；字符串内旧写法无残留。
- 遗留问题 / 下一步：
  - Task 3 迁移文档示例；后续 Task 5/6 使字面量定宽后简化类方法调用变通。

## 2026-09-28 22:10 Task 1 完成：产品语言源迁移到定宽写法

- 背景：定宽类型统一计划（`docs/superpowers/plans/2026-09-28-fixed-width-types.md`）Phase 1 首任务。
- 完成事项：
  - 迁移 `libs/std/{core,c,io}.smc`、`resources/{main,main_min}.c` 的语言源码：`int→int32`、`float→float32`、`double→float64`（提交 `91cd2fc`）。
  - 控制器裁定 R1：`src/sema/SemanticAnalyzer.cpp` `visit(FunctionDeclAST&)` 的 constexpr 字面量类型检查改用 `isIntegerType(t) || isFloatType(t)`（原硬编码 `TypeKind::Int/Float/Double/Char`）。
- 验证方式与结果：
  - `cmake --build build -j$(nproc)` 成功；`ctest` 全量 **715/715 通过，0 失败**。
  - R1 负例 `ConstexprFunctionWithNonLiteralReturnType/Parameter` 仍通过；`CoreMinMax/CoreClamp/CoreConstexprUse` 通过。
  - `grep -rnE '\b(int|float|double)\b' libs resources` 无输出。
- 遗留问题 / 下一步：
  - 环境修复：重配置 build（禁用系统 GTest find_package、清掉残留 `FETCHCONTENT_SOURCE_DIR_GOOGLETEST`）以恢复可构建状态，未改源码。
  - 后续 Task 2 迁移测试内语言源码。

## 2026-09-28 22:38 Task 3 完成：文档示例迁移到定宽写法

- 背景：定宽类型统一计划 Phase 1 第三任务，把 `docs/spec` 中作为 SafeModern C 源码的 `int/float/double` 迁移为 `int32/float32/float64`。
- 完成事项（提交 `5c054db`，4 文件，37+/37-）：`abi.md`（标量表/名称修饰表）、`conversions.md`（§2/§3/§4/§5/§9）、`modules.md`（namespace 示例块）、`stdlib.md`（assert 示例 + std.core/std.io 签名表）。`semantics.md`/`compile_time.md`/`README.md` 无旧写法，未改。
- 保留项：`conversions.md` 的 `enum ↔ int` 与 §7（后者归 Task 8），`abi.md` 的 `enum E : uint8` 与 LLVM 类型列 `float`/`double`；未触碰 grammar.ebnf/keywords.md/fixed-width-types.md。
- 验证方式与结果：`cmake --build build -j$(nproc)` 成功；`ctest` → **715/715 通过，0 失败**（2 个既有 Skip）；`SpecConformance.*` 12/12 通过；`git status` 仅 4 个文档文件。
- 遗留问题 / 下一步：Task 8 处理 conversions.md §7、grammar.ebnf、keywords.md、TODO.md。

## 2026-09-28 22:50 Task 4 完成：新增 Float16/Float128 类型与 float16/float128 关键字

- 背景：fixed-width-types 计划 Task 4，新增定宽浮点 `float16`（half）/`float128`（fp128），纯增量，不删除 int/float/double 或 TypeKind::Int/Float/Double。
- 完成事项（提交 `068919e`，12 文件，71+/1-）：
  - `src/ast/Type.h`/`Type.cpp`：`TypeKind::Float16/Float128` + `TypeContext::getFloat16()/getFloat128()`。
  - `src/ast/Symbol.cpp`：`floatWidth` 加 `Float16→16`、`Float128→128`。
  - `src/codegen/CodegenContext.cpp`：`getLLVMType` 加 `getHalfTy`/`getFP128Ty`。
  - `src/sema/SemanticAnalyzer.cpp`：`typeToString` 加 `"float16"`/`"float128"`。
  - `src/frontend/Token.h`/`Lexer.cpp`/`Parser.cpp`：`TOKEN_FLOAT16/TOKEN_FLOAT128` + keywordMap + `isTypeStart`/`parseBaseType`。
  - `src/support/Utils.cpp`：token 名表补 `float16`/`float128`。
  - 测试：`test_codegen_context.cpp`（half/fp128）、`test_parser.cpp`（声明→TypeKind）、`test_lexer.cpp`（关键字）各 1 项。
- 验证方式与结果：
  - TDD：先写测试 RED（编译报 `getFloat16`/`Float16`/`TOKEN_FLOAT16` 不存在），实现后聚焦 3 项全绿。
  - `cd build && cmake --build . -j$(nproc) && ctest` → **100% passed, 0 failed, 718/718**（基线 715 + 新增 3；2 个 ConstexprFunctionE2E 既有 Skip）。
- 遗留问题 / 下一步：
  - `SemanticAnalyzer::isFloatType()` 未纳入 `Float16/Float128`（brief 未要求），sema 算术类型检查暂不识别这两类；建议在删除旧浮点类型/收口 LEX-15 消费端时一并更新。
  - 报告见 `.superpowers/sdd/2026-09-28-fixed-width-types/task-4-report.md`。

## 2026-09-28 22:55 Task 5 完成：src 引用改写为定宽类型（保留别名）

- 背景：fixed-width-types 计划 Task 5（R2 裁定扩大作用域）。让 `src/` 不再产出/依赖 `TypeKind::Int/Float/Double`，
  `int`/`float`/`double` 关键字现产出 `Int32/Float32/Float64`；保留枚举成员与三个 getter（未使用，Task 6 删除）。
- 完成事项（提交 `c834904`，17 文件，171+/179-）：
  - `src/frontend/Parser.cpp`：`parseBaseType` 关键字分支 → `getInt32()/getFloat32()/getFloat64()`。
  - `src/sema/SemanticAnalyzer.cpp`：getter 调用点改定宽；`isIntegerType` 去 `Int`；`isFloatType` 去 `Float/Double` 并补 `Float16/Float128`；`typesCompatible`/`typeToString` 去旧标签。
  - `src/ast/Symbol.cpp`：`arithmeticUnderlying`/`promoteArithmeticType`/`isUnsignedArithmeticType` 定宽化，删除 `integerWidth`/`floatWidth`/`integerRank` 旧标签。
  - `src/ast/Mangle.cpp`：删旧 `Int/Float/Double` 标签；`Enum` 修饰 `"int"`→`"int32"`（解决 Task 3 遗留 abi.md:86 漂移）。
  - `src/codegen/CodegenContext.cpp`：`getDIType`/`getLLVMType` 删旧标签。
  - `src/support/Utils.cpp`：`TypeToString` 补齐定宽整型/浮点（含 Float16/Float128）+ Bool/Enum。
  - `src/ast/PrintFormat.cpp`/`Expr.cpp`/`Decl.cpp`：删旧分组标签、`promoteVarArg` 保留 Float32 提升、constexpr 折叠构造定宽。
  - 测试：7 文件机械改写 getter/TypeKind 引用；移除 7 处 `(int32)` 方法实参强转（Task 2 carry-forward，字面量现为 Int32）。
- 验证方式与结果：
  - `cd build && cmake --build . -j$(nproc) && ctest` → **100% passed, 0 failed, 718/718**（2 个 ConstexprFunctionE2E 既有 Skip）。
  - `grep -rnE 'getInt\(\)|getFloat\(\)|getDouble\(\)' src` 仅命中保留的 6 行签名（Type.h 声明 + Type.cpp 定义），零调用点。
- 遗留问题 / 下一步：
  - Task 6 删除 `TypeKind::Int/Float/Double` 与三个 getter（届时该 grep 才会为空）。
  - 报告见 `.superpowers/sdd/2026-09-28-fixed-width-types/task-5-report.md`。

## 2026-09-28 23:00 Task 6 完成：删除旧类型枚举与 getter

- 背景：fixed-width-types 计划 Task 6。Task 5 已把 `src`/`tests` 全部调用点改写到定宽类型，旧
  `TypeKind::Int/Float/Double` 与 `getInt()/getFloat()/getDouble()` 已无调用点，本次做纯删除收口。
- 完成事项（提交 `bf5e293`，2 文件，27-）：
  - `src/ast/Type.h`：`TypeKind` 删除 `Int/Float/Double` 三成员；`TypeContext` 删除三个 getter 声明。
  - `src/ast/Type.cpp`：删除三个 getter 定义（含 `m_types[...]` 惰性构造体）。
  - `src/sema/SemanticAnalyzer.cpp`：`typeToString` 无旧 case（Task 5 已删），未改动。
- 验证方式与结果：
  - 改动前 `grep -rnE 'TypeKind::(Int|Float|Double)\b|getInt\(\)|getFloat\(\)|getDouble\(\)' src tests`
    仅命中 `Type.h`/`Type.cpp` 自身声明与定义；改动后同命令 exit 1（零匹配）。
  - `cd build && cmake --build . -j$(nproc) && ctest` → **100% passed, 0 failed, 718/718**（2 个既有 Skip）。
  - `git status --short` 仅两处预期修改。
- 遗留问题 / 下一步：无（旧类型已彻底移除）。
  - 报告见 `.superpowers/sdd/2026-09-28-fixed-width-types/task-6-report.md`。

## 2026-09-28 23:01 Task 9 完成：抽离 LiteralKind 到 support 层

- 背景：fixed-width-types 计划 Task 9。`LiteralKind` 与 `literalKindName()` 原定义在
  `frontend/Token.{h,cpp}`，需供 `ast/` 复用而不引入 `ast -> frontend` 依赖。
- 完成事项：
  - 新建 `src/support/LiteralKind.h`：迁入 `enum class LiteralKind : int32_t`（成员与底层类型不变）
    及 `const char* literalKindName(LiteralKind);` 声明。
  - `src/support/Utils.cpp`：迁入 `literalKindName` 定义（映射表逐字未改）。
  - `src/frontend/Token.h`：改 `#include "support/LiteralKind.h"`，删除本地枚举与声明。
  - `src/frontend/Token.cpp`：移除已迁出的定义。
  - `tests/frontend/test_lexer.cpp`：新增 `LexerLiteralKindTest.NameIsAvailableFromSupportHeader`。
- 验证方式与结果：
  - `cd build && cmake --build . -j$(nproc) && ctest` → **100% passed, 0 failed, 722/722**
    （2 个 `ConstexprFunctionE2E` 既有 Skip）。基线 721 + 新增 1。
  - 无行为变更：`literalKindName(LiteralKind::Int)` 仍为 `"int"`（Task 10 才改）。
- 遗留问题 / 下一步：Task 10 把 `Int` 映射改为 `"int32"`。
  - 报告见 `.superpowers/sdd/2026-09-28-fixed-width-types/task-9-report.md`。

## 2026-09-29 00:20 Task 7/8 完成：删除关键字 + 规范文档同步

- 改动摘要：
  - Task 7（提交 `8c14f8b`）：删除 `int`/`float`/`double` 关键字与 `TOKEN_INT`/`TOKEN_DOUBLE`（保留 `TOKEN_FLOAT` 作浮点字面量）；`src/frontend/{Lexer.cpp,Token.h,Parser.cpp}`、`src/support/Utils.cpp`。
  - 补齐前序遗漏的嵌入式语言源：`src/driver/StdPrelude.cpp`（内建 std.c 预置串）与 `tests/resources/test_input/*.c`、`tests/e2e/resources/test_const.c`。
  - 新增负例/正例：`tests/frontend/test_lexer.cpp`（`int float double` 为标识符）、`tests/frontend/test_parser.cpp`（`int x` 报错、`int32 int = 5` 合法）。
  - Task 8（提交 `841840b`）：`docs/spec/{keywords.md,grammar.ebnf,conversions.md}`、`TODO.md` 同步定宽类型集与默认字面量类型。
- 验证方式与结果：`cd build && cmake --build . -j$(nproc) && ctest` → **100% passed, 0 failed, 721/721**；
  `SpecConformance` 12/12；`grep` 无残留 `TOKEN_INT`/`TOKEN_DOUBLE`。
- 遗留问题 / 下一步：进入 Phase 3（LEX-15 消费端）。

## 2026-09-29 00:40 Task 10-13 完成：LEX-15 消费端（Token→AST→sema→codegen）

- 改动摘要：
  - Task 10（`cba2de1`）：`TokenValue` 整数改 `long long`；`Lexer.cpp` 存 64 位；`literalKindName(Int)`→`"int32"`；新增不截断测试。
  - Task 11（`9e182be`）：`NumberExprAST` 持 `long long value` + `literalKind`；`FloatExprAST` 持 `literalKind`；`Parser.cpp` 用 token 的 kind 构造；新增 parser 测试。
  - Task 12（`715a37d`）：`SemanticAnalyzer::typeForLiteralKind` 映射；`ConstValue::intVal` 改 `long long`；无后缀浮点默认 `float64`；新增 sema 测试。
  - Task 13（`6c618ab`）：`src/ast/Expr.cpp` 整数常量按宽度/符号、浮点按 APFloat 语义（half/single/double/quad）生成；新增 codegen 与 e2e 不截断测试；`conversions.md §7`、`TODO.md` 收尾。
  - 相关文件：`src/frontend/{Token.h,Lexer.cpp,Parser.cpp}`、`src/support/{LiteralKind.h,Utils.cpp}`、`src/ast/{Expr.h,Expr.cpp}`、`src/sema/{SemanticAnalyzer.h,SemanticAnalyzer.cpp}`。
- 验证方式与结果：`cd build && ctest` → **100% passed, 0 failed, 727/727**（2 个既有 Skip）；
  最终 review 结论 `MERGE: READY`、无新增 findings。
  - `grep -rnE '\b(int|float|double)\b' libs resources` → 空；
  - `grep -rnE 'TypeKind::(Int|Float|Double)\b|getInt\(\)|getFloat\(\)|getDouble\(\)' src tests` → 空。
- 遗留问题 / 下一步：
  - `tests/frontend/test_parser.cpp` 既有恒真断言（pre-existing，可接受）；
  - `EnumUnderlyingE2E.Int8EnumNegativeValue` 曾偶发一次 SEGFAULT（复跑 0/20，疑似 JIT flake）；
  - half/fp128 软件兜底与跨目标差异属 TYP-04，未纳入本轮。
  - 分支 `feature/fixed-width-types`（13 commits，HEAD `6c618ab`）待合并。

## 2026-09-30 遗留小项清理：ParserErrorTest 恒真断言

- 改动摘要：`tests/frontend/test_parser.cpp` 两处 `ASSERT_TRUE(errors.empty() || !errors.empty())`
  改为有意义断言：
  - `MissingEqualsInArrayInitReportsError`：改为 `ASSERT_FALSE(errors.empty())`
    （`int32 arr[10] {1,2,3}` 实证会报错，恢复路径报 `expected ';' after variable declaration` 等）。
  - `UnexpectedTokenInExpression`：改为断言错误信息含 `expected expression`
    （`return ;` 实证报 `unexpected token '', expected expression after 'return'`）。
- 行为边界实证：`void g() { return; }` 同样报错——当前解析器要求 `return` 后必有表达式，
  裸 `return;` 一律不支持（属 PAR-02/FUN-02 既有行为，本轮不改）。
- 验证方式与结果：聚焦测试 2/2 通过；全量 `ctest` → **727/727**；提交 `c50f993`。
- 遗留问题 / 下一步：裸 `return;` 是否应支持（void 函数）待在 PAR-02/FUN-02 条目下决策；
  继续推进 LEX-05 收尾（`1.`/`1.e3` 尾点形式、十六进制浮点规范决策）。

## 2026-09-30 LEX-05 收尾：结尾点浮点形式

- 改动摘要（提交 `55667f8`）：
  - `src/frontend/Lexer.cpp::scanNumber`：数字后紧跟 `.` 一律进入小数部分
    （原条件要求点后必有数字），解锁文法既有的 `1.` / `1.e3` / `1.f32` 形式；
    指数、后缀、LEX-17 分隔符校验逻辑复用，`1_.` / `1._5` 自动报 E0004。
  - `tests/frontend/test_lexer.cpp`：新增 `LexerTrailingDotFloatTest`（5 项：裸尾点、
    带指数、带后缀、`1.foo` 切分为 FLOAT+IDENT、分隔符负例）。
  - `tests/sema/test_semantic_analyzer.cpp`：新增 `TrailingDotFloatLiteralsAreAccepted`
    （lexer→parser→sema 冒烟）。
  - `docs/spec/grammar.ebnf`：删除"结尾点暂未接受"的已知差异注释，改为已实现说明
    （含 `1.foo` 切分语义）；`TODO.md` LEX-05 置 `[x]`，十六进制浮点 `0x1p3` 明确延后（YAGNI）。
- 决策：十六进制浮点不入本轮规范（用户批准）；`1.foo` 切分语义为有意行为（Rust 风格，
  与既有 `1.5.foo` 切分同类）。
- 验证方式与结果：TDD——6 个新用例先 RED（6/6 失败）后 GREEN；
  SpecConformance 12/12；全量 `ctest` → **733/733**。
- 遗留问题 / 下一步：裸 `return;`（void 函数）支持与否待 PAR-02/FUN-02 决策；
  接下来推进 TYP-04（浮点软件兜底）与 P1-01 剩余项的 brainstorm。

## 2026-09-30 TYP-04（部分）：print 全格式接入 f16/f128 + varargs 提升

- 探查结论：x86-64 上 f16/f128 的算术/比较/混合运算/转换、形参/返回、全局变量、
  struct 字段**均已可用**（LLVM 原生支持承担兜底）；真实缺口仅 print 格式化与 varargs 提升。
- 改动摘要（提交 `59c4bd3`）：
  - `src/ast/PrintFormat.cpp::builtinPrintKind`：`Float16`/`Float128` → `PrintArgKind::Float`。
  - `src/ast/Expr.cpp::promotePrintArg`：half → `FPExt` double（无损）；fp128 → `FPTrunc` double（精度截断）。
  - `src/ast/Expr.cpp::promoteVarArg`：新增 `Float16 → float64` 提升（C23）；`float128` 原样传递（C23 `__float128` 不提升）。
  - `docs/spec/conversions.md` §9 表格更新 + 新增 §9.1（print 浮点格式化与精度说明）。
  - `TODO.md`：TYP-04 置 `[~]` 并记录完成/延后边界；快照 735。
- 测试：`EndToEndTest.PrintFormatsHalfAndQuad`（f16/f128 × `{}`/`{:f}`/`{:.2f}`/`{:e}`）、
  `EndToEndTest.Float16VarargPromotesToDouble`（extern printf varargs）。
  实施中一次"失败"实为测试期望值笔误（第 4 参为 `1.5f128` 而非 2.5），归约排查后修正期望，非产品缺陷。
- 验证方式与结果：TDD 2 用例先 RED 后 GREEN；SpecConformance 12/12；全量 `ctest` → **735/735**。
- 遗留问题 / 下一步：跨目标（非 x86-64）软兜底验证挂靠 INF-05/TST-08；f128 精确十进制输出挂靠 FMT-13；
  接下来 brainstorm P1-01 剩余项（class 成员访问段 / union / Slice）。

## 2026-09-30 P1-01 之 C：union 补齐（AGG-17/AGG-03）

- 探查实证：union 声明/初始化/拷贝/布局已可用；真实缺口为匿名 union 成员不提升、
  `typedef struct/union` 别名成员访问失效（通用 bug，struct 同样命中）、聚合按值传参/返回整体未实现（struct 同样命中）。
- 改动摘要（提交 `28c6ecc`）：
  - `src/frontend/Parser.cpp`：`parseStructDecl`/`parseClassDecl`/union 体循环支持匿名
    struct/union 成员提升（C11 语义，parseType 内联定义路径已消费 `;`），名字冲突报错；
    新增 `isAnonymousAggregate`/`promoteAnonymousMembers` 助手。
  - `src/sema/SemanticAnalyzer.cpp` `visit(MemberAccessExprAST)`：`.`/`->` 均剥离
    `TypedefType` 后再判定聚合类型与查找成员。
  - `src/ast/Expr.cpp` `MemberAccessExprAST::codegen`：分派同样剥离 typedef（含指针 base）。
  - `tests/e2e/test_union.cpp`（新增 8 项）：初始化/拷贝/sizeof 最大成员/嵌套 union/
    数组成员/指针访问/tag-union/匿名提升（union+struct）/typedef 聚合。
- 实施注记：首次编辑的 else-if 落入 parseClassDecl（循环结构相同）而 parseStructDecl
  是简版循环——经 CLI 变体排查（匿名 union 在前时后续成员全丢）定位后补齐，union 体循环一并支持。
- 决策：union/struct 按值传参/返回归 MEM-14/TYP-27 单独项（非 union 特有）；
  文件作用域匿名 union 延后。
- 验证方式与结果：TDD——8 个新用例 3 个 RED（与探查缺口精确一致）后全 GREEN；
  SpecConformance 12/12；全量 `ctest` → **743/743**。
- 遗留问题 / 下一步：P1-01 剩余 B（class 访问段诊断/protected/static 成员）与
  A（Slice 完整实现）待 brainstorm。

## 2026-09-30 P1-01 之 B：class 访问段与访问控制（PAR-04/SEM-04/DEC-01）

- 探查实证（修正前轮误判）：`private 生效`是**假象**——真实 bug 是访问说明符截断类体
  （`public:`/`private:` 处 break，其后成员/方法全部丢失）；方法 + `this->field` 机制本身完好。
- 决策（用户批准）：**DEC-01 冻结——class 默认 private，struct 默认 public**；
  protected 本轮 ≡ private（继承落地前）；static 成员/嵌套类型延后（AGG-10/11）。
- 改动摘要（提交 `8f9d40a`）：
  - `src/ast/Type.h`：`enum class AccessLevel`；`ClassType::memberAccess` 平行表（fields 结构与 codegen 索引零影响）。
  - `src/ast/Decl.h`：`StructDeclAST::memberAccess` + `isClassDecl` 标记。
  - `src/frontend/Parser.cpp::parseClassDecl`：访问说明符分支（`protected` 为上下文关键字，需后随 `:` 消歧）；
    每成员记录级别（含匿名提升成员）；类体不再截断。
  - `src/sema/SemanticAnalyzer`：`isClass` 采纳 `isClassDecl`（修复无方法 class 被当 struct 的 bug）；
    `currentClass` 栈跟踪；类外访问 private/protected 字段/方法报 **E2009**（带 fix 建议）；
    未知成员仍报 "no member"。
  - `src/sema/Diagnostic.*`：注册 `SemPrivateMemberAccess` → **E2009**。
  - 快照：`tests/diagnostics/snapshots/private_member_access.txt`。
  - 既有测试迁移（不弱化断言）：8 处 class 补显式 `public:`（原意图=成员可访问，按新默认语义显式化）。
- 测试教训：单跑通过、合跑失败 → 全局 `TypeContext` 单例跨测试泄漏，新测试改用唯一类名（CA/CC/CD/SE）规避；
  首次 sed 迁移漏掉行尾带空格的 `class Math {`，全量回归抓出后手动补。
- 验证方式与结果：TDD——7 个新用例先 RED 后 GREEN；迁移后全量 `ctest` → **751/751**；SpecConformance 12/12。
- 遗留问题 / 下一步：protected 派生类放开（随 INH）；static 成员（AGG-10）、嵌套类型（AGG-11）；
  P1-01 之 A（Slice 完整实现）待 brainstorm。

## 2026-09-30 P1-01 之 A：TYP-12 Slice 完整实现

- 流程：brainstorming（architectural）→ spec（`docs/superpowers/specs/2026-09-30-slice-design.md`，`9b97c23`）→ writing-plans（`docs/superpowers/plans/2026-09-30-slice-implementation.md`，`9a6645e`）→ executing-plans 内联 7 任务。
- 冻结决策（用户批准）：D1 范围=下标读写+.len+退化（无范围切片/显式构造）；D2 越界不检查（UB）；D3 零初始化 `{null,0}`；D4 方案 A（内联转换点+规范 LLVM 类型）。
- 实现要点（`8f599b1`..`9d74a2f` 共 7 commits）：
  - `CodegenContext::getLLVMType` Slice 分支单例化（修复 "Slice"/"Slice.0" 类型分裂，Task 1 RED 实证 Verifier 拒绝）。
  - sema：`visit(ArrayAccessExprAST)`/`visit(MemberAccessExprAST)` 加 slice 分支；`conversionRank`/`typesCompatible`/`checkAssignmentTypes` 加 Array→Slice 退化；`typesEqual` 加 Slice 元素递归比较。
  - codegen：`CodegenContext::emitArrayToSliceDecay`（{ptr,N} 视图）；下标（load→extractvalue 0→GEP）；`.len`（extractvalue 1）；局部零初始化（store null）。
  - parser：`parseType` 的 `[]` 后缀循环化，支持 `int32[][]`。
- 测试：SliceE2ETest 6 项 + SliceSemTest 10 项 + 快照 1 项；全量 **768/768**（基线 751）。
- Rulings（执行中）：①MethodCall 传参退化不做（resolveMethod/mangle 以 arg->type 匹配，sema 不支持退化，单改 codegen 前后端不一致）→ TODO；②`int32 arr[2]` 形参语法不解析（TYP-11 缺口）且返回局部数组为悬垂 UB → 删该用例、退化分支保留；③typesCompatible 的 slice 分支须置于 kind==kind 早退之前；④全局变量 codegen 缺口（`int32 g;` 不生成符号，CLI 实证通用问题）→ 移除全局用例、挂 TODO。
- 终审：subagent 评审两次失败（R7/R7b 沿袭）→ 作者自审：无 Critical/Important；deferred minor 2 条（嵌套 slice codegen e2e 缺口、stripTypedefs 重复）。
- 遗留：MethodCall 退化、形参数组语法（TYP-11）、全局变量 codegen 缺口、范围切片/显式构造（YAGNI）——均挂 TODO。

## 2026-10-01 全局变量 codegen 缺口修复

- 根因：`VarDeclAST::codegen` 全局分支把 `InitVal=nullptr` 传给 `GlobalVariable`，LLVM 视为 external 声明（不分配存储）→ `int32 g;` 链接 undefined reference；有初始化器则正常（TYP-12 轮实证残留项）。
- 修复：`src/ast/Decl.cpp` 全局无初始化器时回退 `Constant::getNullValue`（零初始化定义，对齐 C tentative definition）；`extern` 变量语义范围外（语言无该标志，YAGNI）。
- 测试：新建 `tests/e2e/test_globals.cpp`（GlobalsE2E 4 项，含恢复的 TYP-12 全局 slice `{null,0}` 用例）；RED（3 FAIL/1 OK 精确预测）→ GREEN；全量 **772/772**。
- 验证：CLI 复现用例 `int32 g2;` 编译链接运行 exit=0。
- 遗留：MethodCall 传参退化、`return` 退化待 TYP-11 形参数组语法（见 TODO.md）。

## 2026-10-01 TYP-11 数组形参去糖 分支评审（subagent）

- 评审包：`.superpowers/sdd/2026-10-01-typ11-array-params/review-1639446..0c51cd0.diff`（4 commits，HEAD=0c51cd0，工作树一致）。
- 实证验证：e2e 4 项 + 快照绿；全量 ctest 计数 **783**；CLI 复核 `int32 a[]` 空括号去糖、多维诊断、悬垂拒绝（含全局数组同拒）均正确。
- 结论：实现本身无 Critical/Important 缺陷（D1–D4 一致，守卫 typedef 剥离正确，回归面干净）。
- **Important 1**：`FunctionPointerArrayParamDesugared` 空转通过——`int32 cb(int32 a[2])` 直接函数类型形参 parser 根本不支持（CLI 实证 `expected ')'`），analyzeOk 忽略 parse 错误 → AST 只剩 main → 恒真；Review Focus 4 未被真正钉住。真实共用路径（函数指针 `int32 (*cb)(int32 a[2])`，经 `parseFunctionPointerType`→`parseParamDecl`）去糖正确但无任何测试。
- **Important 2**：多维形参诊断文案（"multi-dimensional array parameters are not supported"）无测试断言，`MultiDimArrayParamRejected` 改前改后均绿，Review Focus 2 的差异化行为未被验证。
- Minor：诊断文案 spec/plan 不一致（"(dangling)" vs "(dangling view)"）且对全局数组同样报 "local array"；TODO.md P1-01（:511）未按 Task 3 Step 4 同步（"数组形参"已完成仍列为剩余）；`T name[]` 空括号去糖属未记录扩展（spec/测试未提）；spec §3.2 "node.type = nullptr" 与实现不符（ReturnStmtAST 无该字段）。
- 下一步：作者修复两个 Important（改显式管线 + 函数指针拼写；补文案断言）与 Minor 文档项。

## 2026-10-02 MethodCall 传参退化（TYP-12 遗留）

- 流程：brainstorming → spec（`docs/superpowers/specs/2026-10-02-methodcall-decay-design.md`，`4b346c7`，D1–D5）→ plan（`docs/superpowers/plans/2026-10-02-methodcall-decay.md`，`208d173`）→ Native 3 任务。
- 根因：①sema `resolveMethod` 按 Type* 指针同一性匹配（数组实参对 slice 形参必失配）；②codegen 按实参类型 mangle（定义点按声明类型）。
- 实现：`MethodCallExprAST::resolvedParamTypes`（含 this）；`resolveMethod` 两阶段（先精确后 conversionRank>=0，null argType 淘汰）；codegen 按 resolvedParamTypes mangle + `emitArrayToSliceDecay` 退化 + `castValue`。
- 测试：sema 5 项 + e2e 2 项（`tests/e2e/test_method_args.cpp`，MethodArgsE2E）；全量 **791/791**（基线 784）。
- 裁定：RED 表现为段错误（未实现通道 calleeFn 空解引用，既有缺陷）——按 crash=RED 继续；方法声明但未定义仍崩溃（现状即如此）挂遗留。
- 遗留：calleeFn 为 null 时无防护（既有）；重载最优匹配打分（YAGNI）。

## 2026-10-02 MethodCall 传参退化 分支评审（subagent）

- 评审包：`.superpowers/sdd/2026-10-02-methodcall-decay/review-4b346c7..7e5aa72.diff`（4 commits，HEAD=7e5aa72）。
- 实证验证：构建 + 全量 ctest **791/791**（3 连绿；首跑 1 次 `EnumStrongE2E.ExplicitCastIntToEnumAllowed` 偶发 SegFault，与本分支无关）；CLI 探针 5 项：null argType（`m.g(nosuchvar)`）正确拒绝不崩溃、跨类精确匹配仍胜过派生类 rank 候选、数组→指针形参新放行可用、继承链数组实参写穿透正确。
- 结论：实现无 Critical 缺陷；D1–D5 一致，resolveMethod 阶段 1 原样保留（精确等价），基类方法先拷入派生类方法表保证 phase 1 先于 phase 2 覆盖基类精确匹配。
- **Important 1**：D4 null-argType 守卫分支（SemanticAnalyzer.cpp:2140 `!argTypes[i]`）零测试覆盖——`MethodNullArgGuard` 实际只测 arity 失配（`m.g()` 时 argTypes 为空，循环体不执行）；Review Focus 5 的真实场景无防护。
- **Important 2**：Review Focus 3 的 codegen 主张（resolvedParamTypes 为基类声明形参 → mangle 命中基类定义）仅有 sema 级测试，test_method_args.cpp 无继承 e2e（plan 自身映射缺口）。
- Minor：phase 2 放行面为 conversionRank 全谱系（加宽/指针/null→指针），文档口径偏窄为"数组→slice"；spec D5"arg->type 非空时"括号限定未按字面实现（不可达，castValue 自身容错）；EnumStrongE2E 偶发 SegFault 建议另行跟踪。
- 下一步：作者补 null-argType sema 测试与继承 e2e 各 1 项；Minor 文档口径可选。

## 2026-10-03 多维数组本体（TYP-11 剩余之一）

- 流程：brainstorming → spec（docs/superpowers/specs/2026-10-03-multidim-arrays-design.md，6652f2f，D1–D5）→ plan（docs/superpowers/plans/2026-10-03-multidim-arrays.md，8bc8490）→ Native 3 任务。
- 冻结决策：D1 嵌套 ArrayType（[2 x [3 x i32]]）；D2 parser 维度链从右向左（parseVariableDecl + parseMemberArraySuffix，sema 零改动）；D3 首维推断；D4 codegen 零改动（六条递归通路复用）；D5 整体多维→slice 不放行/悬垂守卫自动覆盖。
- 实现外发现：全局数组带初始化器为既有缺口（一维亦崩、无测试覆盖）——ArrayDeclAST::codegen 补全局分支（镜像 VarDeclAST），一维全局顺带修复。
- 测试：sema 6 项 + e2e 4 项（tests/e2e/test_multidim.cpp，MultiDimE2E）；全量 **803/803**（基线 793）。
- 遗留：多维形参（语义待定：行 slice 的 slice / 扁平化）、VLA；行级初始化长度校验（范围外，1-D 亦无）。

## 2026-10-04 多维数组本体 分支评审（subagent）

- 评审包：`.superpowers/sdd/2026-10-03-multidim-arrays/review-6652f2f..5e1afda.diff`（4 commits，HEAD=5e1afda）。
- 实证验证：构建 + 全量 ctest **803/803**；IR 探针 3 项：`[2][3]` 建为 `[2 x [3 x i32]]`（维度顺序正确）、3-D `[2][3][2]` 初始化行主序且 `sizeof==48` 常量折叠正确、`a[2][]` 缺省内层维度被静默接受为 `[2 x [0 x i32]]`（OOB GEP）、`int32 g[2] = 5;` 全局被静默零初始化（`= 5` 丢弃，IR `zeroinitializer` 证实）。
- 结论：核心实现（D1–D5、维度右向左、1-D 等价、全局分支）无 Critical 缺陷；Review Focus 1–4 全部有布局敏感测试钉住。
- **Important 1**：非首维缺省维度被静默接受（`a[2][]`→`[2 x [0 x i32]]`，成员侧缺省为 1）——OOB 误码无诊断。
- **Important 2**：ArrayDeclAST::codegen 全局分支（Decl.cpp:407-415）静默丢弃非列表初始化器；sema :1741 以 elementType（而非数组类型）比对实参使 `g[2] = 5` 通过。
- **Important 3**：Review Focus 5 成员字段多维仅 sema 级验收，成员字段多维读写 codegen 无 e2e。
- Minor：全局 const 数组 ExternalLinkage 与 VarDeclAST const 标量 PrivateLinkage 不一致（镜像不完整）；parseVariableDecl 逆序循环 `i>=1` 依赖隐式保证；parseMemberArraySuffix 忽略 expect 失败（既有）；局部多维 sizeof / 3-D / 全局首维推断无回归钉；buildAggregateConstant 非常量元素静默补零（既有，VarDecl 同）。
- 下一步：作者补 1 个成员字段多维 e2e；Important 1/2 建议本分支或紧随轮修复（各 1 个拒绝用例 + 全局分支兜底）。
- 评审修复（dbcb685）：非首维缺省拒绝（防零长度行+越界 GEP）、数组标量初始化拒绝（防静默丢值）、成员字段多维 e2e。全量 806/806。

## 2026-10-04 多维数组形参（TYP-11 剩余之二）

- 流程：brainstorming（方案 A 行 slice，用户选定）→ spec（docs/superpowers/specs/2026-10-04-multidim-array-params-design.md，6f6acbe，D1'–D5'）→ plan（docs/superpowers/plans/2026-10-04-multidim-array-params.md，5c1c6e9）→ Native 执行。
- 语义：`T name[][3]` / `T name[2][3]` → `Slice<int32[3]>`；首维文档性、内维显式、行类型编译期严格比较；return 多维仍拒绝。
- 实现外发现：typesEqual 数组分支用恒 null 的 base 且不比长度——所有数组两两相等；修复为 size+elementType 比较（Symbol.cpp）。
- 测试：sema 5 项（MDP 前缀）+ 1 项旧 pin 改写（MultiDimParamParses）+ e2e 3 项；全量 **814/814**（基线 806）。
- 遗留：VLA（TYP-11 最后一项）；裸函数类型形参（parser 既有缺口，另立项）。

## 2026-10-04 多维数组形参——整分支评审（subagent）

- 评审包：`.superpowers/sdd/2026-10-04-multidim-array-params/review-6f6acbe..17bb427.diff`（4 commits，HEAD=17bb427）。
- 实证验证：构建 + 全量 ctest **814/814**（2 项 constexpr skip 属既有）；Review Focus 五条逐条核对均钉住（①写穿透 e2e ②行不匹配 sema ③1-D 既有 4 钉 + dims.size()==1 等价 ④内维缺省解析期拒 ⑤MethodCall e2e）。
- 裁定核查：a) typesEqual 数组分支修复成立——调用点仅 conversionRank/OverloadSet::resolve/Scope::declare（函数签名判重）与 SemanticAnalyzer 的 slice 元素比较（:250/:473），均无依赖旧"所有数组相等"语义的合法用例；结构化比较 + getSliceType 无缓存，无指针同一性陷阱。b) MultiDimParamParses 正向 pin 成立，语义接受由 MDPExplicitFirstDimAccepted 覆盖。
- **Important（既有，非本分支引入）**：数组-数组直接赋值 `a = b`（a: int32[2][3]，b: int32[2][4]）静默通过（SemanticAnalyzer.cpp:478 同 kind 兜底，不经 typesEqual），codegen 生成 `store ptr %decay, ptr %a`（把 b 的退化指针写进 a 的存储，覆盖 a[0][0..1]，无任何复制语义）——内存不安全。实测 IR 证实。建议另立项：sema 对 Array↔Array 赋值按形状相等拒绝。
- Minor：2-D 元素类型不匹配（float64[2][3]→int32[][3]）实测拒绝但无测试钉；3-D 形参无测试；`f(int32[][3])`/`f(int32[][4])` 现为两个可共存重载（typesEqual 修复的可见行为变化，spec 未规定）；MDPNonFirstDimRejected 未钉错误消息文本；`[4294967297]` 维度 int 截断（既有模式）；`m[2][0]` 零长度行接受（与声明侧 a[2][0] 一致）。
- 结论：核心去糖实现无 Critical 缺陷，裁定 a/b 均成立；数组赋值洞建议紧随轮修复。

## 2026-10-05 VLA 决策：明确不支持（TYP-11 完结）

- 流程：brainstorming → 用户质疑"不支持运行时长度是否更合理" → 技术论证支持 → 用户决策不支持 → 方案 1（专用诊断 + 关闭）。
- 论证：无界栈增长与安全定位冲突（CERT MSC34-C）；C23 已降为可选；Rust/Zig/Go/Swift 均无 VLA；动态长度由 Slice 承接（未来配合显式堆分配）。
- 实现：parseVariableDecl + parseParamDecl 两处 `[` 后非 NUMBER/`]` → "variable-length arrays are not supported: array size must be an integer literal"；pin 测试 2 项（VLA 前缀，钉消息文本）。816/816（基线 814）。
- TYP-11 完结（`[x]`）。遗留相关：数组-数组赋值缺陷（另立项，见 2026-10-04 条目）。
