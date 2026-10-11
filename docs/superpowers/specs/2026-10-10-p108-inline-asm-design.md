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
- **volatile**：`TOKEN_VOLATILE` 复用；语义 = LLVM `HasSideEffects`。**无输出操作数的 asm 无论是否写 `volatile` 都视为 volatile**（GCC 语义，防优化期被 DCE）；有输出且未写 `volatile` 时 LLVM 可假设无副作用（优化/消除），有 `volatile` 时禁止消除与重排。

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

> **实现修正**（plan 原案「outputs 传左值地址 + 约束原样」被 LLVM 21 拒绝，实证见 ledger Task 4 Rulings；本节约定实现后的真实映射）。

- **输出 → call 返回值**（LLVM 21 强制）：单输出返回类型即该标量；多输出返回 `{T0,T1,...}` StructType（按约束顺序），调用后 `ExtractValue` 逐位 `store` 回写左值；零输出返回 void。LLVM 对「`=r` 传左值地址」报 `number of input constraints does not match`，不可行。
- **读-写输出（`+`）**：LLVM 21 无 `+` 前缀（`ConstraintPrefix` 只认 `~`/`=`/`!`）。翻译为 `=r`（输出）+ 追加在**全部普通输入之后**的 tied 数字约束（`"=r,r,0"`，参数顺序 = 普通输入值 + 读-写输出初值），使 GCC 模板编号（`%N`）与 LLVM `$N` 完全对齐（tied 槽不被模板引用）。
- **模板翻译（GCC → LLVM 方言）**：LLVM 只认 `$N`（操作数引用）与 `$$`（字面 `$`），`%` 不处理。顺序：`$`→`$$`（先转义字面 `$`）→ `%%`→`%` → `%N`→`$N`。
- **输出约束限定**：仅寄存器类输出（`r`/`q`/`Q`/具体寄存器等）。**内存类输出（`m`/`o`/`V`，含 `Um` 等多字母内存码）sema 拒绝（E2023）**——LLVM 21 对其返回-值模型走 `SelectionDAGBuilder` 崩溃路径（SEGV，实证 rc=139）。`p`/`g`/`X`/`v` 等由后端干净报错，不拒绝。
- **输入**：一律传值。`m`（内存）输入由 LLVM spill 到栈临时并传其地址——**只读模板与 GCC 语义一致；模板若经 `m` 输入写内存，写的是副本而非原变量**（GCC 写原变量；写回须用 `=`/`+` 输出）。
- **clobbers**：`~{name}` 追加在约束串末尾（逗号分隔）。
- **构造**：`llvm::InlineAsm::get(FT, tpl, constraintStr, HasSideEffects = isVolatile || outputs.empty(), IsAlignStack = false, AD_ATT)`。
- **发射**：`CreateCall(InlineAsm, args)`；output 左值地址求值一次（供 tied 输入与回写共用）。

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
