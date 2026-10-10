# P1-08 内联 asm 设计（2026-10-10）

## §0 决策表

| 决策 | 内容 |
|---|---|
| D1 | **语法形态**：GCC/C++ 完整形态 `asm [volatile] ("tpl" : outputs : inputs : clobbers)`，操作数约束逐一映射 LLVM `InlineAsm`；volatile 可选。 |
| D2 | **目标限定**：通用透传——编译器不解析约束语义，模板+约束串原样交给 LLVM `InlineAsm`，合法性由目标后端验证；宿主 x86-64 为测试基线（JIT `detectHost`），非 x86 目标由后端报错。 |
| D3 | **绑定模型**：仅语句（GCC 兼容）——outputs 绑定到可写左值，调用后回写；asm 不产生表达式值（`void` 类型），表达式形态（GNU 返回第一输出）不做。 |

## §1 背景与目标

- P1-08 = `static_cast` / 内联 `asm`。`static_cast`/`reinterpret_cast` 已落地（LEX-11/PAR-18，CastKind::Static/Reinterpret）；本 spec 完成内联 `asm`（LEX-12 / PAR-16 / CG-10 / DEC-09）。
- 现状：`asm` 是保留关键字但无 token（`keywords.md` §7 `[plan]`，未入 `keywordMap`）；`volatile` 已是 `TOKEN_VOLATILE`；codegen 未使用 LLVM `InlineAsm`。
- 目标：`asm volatile("tpl" : out : in : clobbers)` 语句在 lexer/parser/sema/codegen 全链路可用，宿主 x86-64 上经 LLJIT 可执行，约束/clobber/volatile/目标限定语义与 GCC 对齐。
- 验收：TODO LEX-12 / PAR-16 / CG-10 / DEC-09 收口；P1-08 全轮 `[x]`；全量测试绿。

## §2 语法与词法

- **词法**：`asm` 加入 `keywordMap` → `TOKEN_ASM`（`src/frontend/Lexer.cpp`）。
- **语法产生式**（GCC/Clang 兼容）：
  ```
  asmStmt     := 'asm' ['volatile'] '(' template [ ':' outputs [ ':' inputs [ ':' clobbers ] ] ] ')' ';'
  template    := 字符串字面量
  outputs     := constraint '(' 左值 ')' ( ',' constraint '(' 左值 ')' )*
  inputs      := constraint '(' 表达式 ')' ( ',' constraint '(' 表达式 ')' )*
  clobbers    := 字符串字面量 ( ',' 字符串字面量 )*
  constraint  := 字符串字面量
  ```
- **模板串**：必须是字符串字面量（动态模板编译期拒绝，同 print 格式串先例）。模板语法为 GCC 方言：`%N` 引用第 N 个操作数、`%%` 转义为字面 `%`（格式校验在 sema，转义按 LLVM 约定原样透传——LLVM 模板直接收 `%N`，`%%` 由后端消化）。
- **volatile**：`TOKEN_VOLATILE` 复用；语义 = LLVM `HasSideEffects`。无 volatile 时 LLVM 可假设无副作用（优化/消除）；有 volatile 时禁止消除与重排（GCC 一致）。

## §3 AST 节点与解析

- **新节点** `AsmExprAST`（`src/ast/Expr.h`，表达式导向，经 `ExprStmtAST` 作语句使用——与 print/panic 同模式）：
  ```
  struct AsmOperand { std::string constraint; std::unique_ptr<ExprAST> expr; };
  class AsmExprAST : public ExprAST {
      std::string asmTemplate;
      bool isVolatile;
      std::vector<AsmOperand> outputs;
      std::vector<AsmOperand> inputs;
      std::vector<std::string> clobbers;
      std::vector<Type*> operandTypes;   // sema 填，codegen 构造 InlineAsm 签名
      llvm::Value* codegen(CodegenContext&) override;
  };
  ```
- **解析**（`Parser::parseUnaryImpl` 前缀分支，仿 `static_cast` 先例，`Parser.h` 增加 `parseAsmExpr()`）：遇 `TOKEN_ASM` → 可选 `volatile` → `(` 模板 `)`，其后可选 `:` 段逐段解析（段内 `constraint ( expr )` 逗号列表）；空段（如 `asm("tpl" : : "r"(x))`）合法。
- 节点 `type = void`、`isLValue = false`。

## §4 Sema 校验与诊断

- `visit(AsmExprAST)`：置 `node.type = getVoid()`；遍历操作数 `getExprType`。
- **outputs 必须可写左值**（变量 / 成员访问，`isLValue` 判定）；inputs 任意值表达式。
- **操作数类型**：标量（整型 / 指针 / 浮点）；聚合（struct/class/array/slice/str/string）→ 诊断。
- **约束与操作数匹配**：每段约束串个数 == 括号表达式个数；约束串空 → 诊断。
- **模板校验**：字符串字面量；`%` 后非 `%` 且非数字 → 非法转义诊断。
- **clobbers**：任意字符串字面量（`"memory"`/`"cc"`/寄存器名），不解析语义（透传）。
- **诊断码**：新增 `E2023`（asm 操作数/约束错误：非左值、聚合类型、数量不匹配、空约束）与 `E2024`（asm 模板非法：非字面量、非法转义），登记 `src/sema/Diagnostic.{h,cpp}`（现至 E2022）。

## §5 Codegen：LLVM `InlineAsm` 映射

- **签名**：`FunctionType::get(voidTy, operandTypes...)`，参数类型 = outputs（按序）+ inputs（按序）。LLVM 的 InlineAsm 参数列表即操作数列表；outputs 的 `=`/`+` 前缀写在约束串里。
- **构造**：`llvm::InlineAsm::get(FT, constraintsJoined, asmTemplate, HasSideEffects = isVolatile, IsAlignStack = false, InlineAsm::AD_ATT)`。
  - 约束串 = outputs 约束 + inputs 约束 + clobbers，`;` 为段分隔、`,` 为段内分隔，拼成 LLVM 约束格式（`"=r,r,~{memory}"`）。
  - `AD_ATT`（AT&T 方言）为宿主 x86-64 默认。
- **发射**：`CreateCall(InlineAsm, args)`；outputs 实参传**地址**（`Alloca` 或左值地址），调用后 `load` 回写 output 左值（GCC 语义）；inputs 传值。
- **零操作数退化**：`asm("nop")` → 零参数 InlineAsm，正常。
- **模板透传**：`asmTemplate` 原样传给 InlineAsm（`%%`/`%N` 由 LLVM 处理）。

## §6 测试

- **lexer**：`asm`/`volatile` token（`tests/lexer` 或 `test_lexer`）。
- **parser**：全形态——无操作数 / 单多 outputs / inputs / clobbers / volatile / 空段（`: : "r"(x)`）/ 缺省尾段。
- **sema**（`tests/sema/test_semantic_analyzer.cpp`）：outputs 非左值 → E2023；聚合操作数 → E2023；约束/操作数数量不匹配 → E2023；动态模板 → E2024；`%z` 非法转义 → E2024；合法用例 `analyzeFullyOk`。
- **e2e**（`tests/e2e/`，JIT 执行，宿主 x86-64）：
  - `asm("nop")` 空跑；
  - `asm volatile("mov $7, %0" : "=r"(out));` → `out == 7`；
  - `asm("add %1, %0" : "+r"(x) : "r"(y));` → `x += y`；
  - `asm volatile("" : : : "memory")` barrier 冒烟；
  - `asm("mov %%eax, %0" : "=r"(out));` 转义冒烟。

## §7 文档

- `docs/spec/keywords.md`：`asm` `[plan]` → `[impl]`。
- `docs/spec/grammar.ebnf`：asm 产生式。
- 文档小节（grammar.ebnf 或 stdlib 旁）：GCC 兼容语义、volatile 副作用契约、操作数约束透传说明、目标限定（后端验证）、宿主 x86-64 测试基线。
- `TODO.md`：LEX-12 / PAR-16 / CG-10 / DEC-09 收口，P1-08 全轮 `[x]`。

## §8 边界与不做

- 不做表达式形态（GNU asm 表达式返回第一输出）——D3。
- 不做编译期约束语义校验（寄存器名/约束合法性由后端验证）——D2。
- 不做跨平台测试（仅宿主 x86-64 基线）。
- asm 与 `compile_time` 正交（DEC-09 原文）；不引入目标查询新 API（CT-04 已有）。
- 内联 asm 天生不可移植且可能破坏安全不变量——文档明示"使用者自担"，不设语言级沙箱。
