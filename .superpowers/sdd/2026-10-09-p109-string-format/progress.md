# SDD ledger — plan: docs/superpowers/plans/2026-10-09-p109-string-format.md

BASE: 66ff5c5 (master)
Spec: docs/superpowers/specs/2026-10-09-p109-string-format-design.md（已读，权威）

Pre-flight: 5 组共享接口行——T1→T2（buildPrintFormat outSpecs/errKind）、T2→T3（isFormat/formatLiteral/formatSpecs/printArgKinds）、T1→T3（emitPaddedValue outLen）、T4→T5（find 扫描 helper 复用）、T4/T5→T6（find/rfind/sub/split 内建供 .smc 直用）——均一致，无冲突。

Task 1: complete — PrintSpec/parsePrintSpec/specToPrintfConversion 落地，print/println 接入渲染路径，E2020~22 注册。Ruling: `(is64?"ll":"")+spec.type` 是 char* 指针算术（教训：char 拼串须走 std::string）；'0' 不得作 fill 字符（与 zero flag 文法冲突，冲突检查提前到 type 解析前）。tests: PrintFormatTest 32/32 + FormatSpecDiagnosticCodes 1/1 + 全量 1140 绿。commits: 66ff5c5..HEAD
Task 1: complete (commits 66ff5c5..bd6bf57, tests: ctest --test-dir build → 	301 - ConstexprFunctionE2E.ConstexprNestedCall (Skipped))
Task 2: complete (commits bd6bf57..6beea96, tests: ctest --test-dir build → 	301 - ConstexprFunctionE2E.ConstexprNestedCall (Skipped))
Task 3: complete (commits 6beea96..615e191, tests: ctest --test-dir build → 	301 - ConstexprFunctionE2E.ConstexprNestedCall (Skipped))

Task 3: Ruling: 不得链式依赖 libc `@memcpy` 的返回值推进 dest——LLVM 将 memcpy 库函数调用降级为 intrinsic DAG 节点，返回值不保证（IR 正确但 JIT 运行时 dest 不前进，全链写到起点）。改为显式 GEP 偏移累加。成本若错：format 字面量路径输出错乱。tests: StringE2E.Format* + PrintRenderSlotSmoke 20/20，全量 1162 绿。
Task 4: complete (commits 615e191..579d1ab, tests: ctest --test-dir build → 	301 - ConstexprFunctionE2E.ConstexprNestedCall (Skipped))
Task 5: complete (commits 579d1ab..4eb4ea6, tests: ctest --test-dir build → 	301 - ConstexprFunctionE2E.ConstexprNestedCall (Skipped))

Task 5: Ruling: 切片声明语法 = `str[]`（元素类型在前），非 Go 式 `[]str`——测试源码修正。Ruling: 合成 helper 的 alloca 须在 SetInsertPoint(entry) 之后（否则落进调用者函数，模块校验失败）。Ruling: 切片元素字段为 SliceType::elementType（Type::base 不承载）。tests: BuiltinSplitSema + Split* 5/5，全量 1172 绿。commits 579d1ab..4eb4ea6
Task 6: complete (commits 4eb4ea6..3e63edc, tests: ctest --test-dir build → 	301 - ConstexprFunctionE2E.ConstexprNestedCall (Skipped))

Task 6: Ruling: class 无法跨模块导出——类型名在 parse 期注册、先于 import 处理（std::StrBuilder 与模块级 export class 均不可见）→ StrBuilder 降级为「string 自身即 builder」，stdlib.md 挂账模块类导出缺口。Ruling: &&/|| 原为按位 And/Or（高位真值截断为假、RHS 永远求值）→ 修复为 C 语义短路 + coerceToBool（e878bdb，随 Task 6 暴露）。Ruling: 派生→基类值赋值整结构 store 溢出目标（潜在栈腐败，被 && 改动重排帧布局暴露）→ castValue 沿字段 0 继承链 extractvalue 截取基子对象（同提交）。tests: StdStringLib 7/7 + LogicalOps e2e，全量 1180 绿。commits 4eb4ea6..3e63edc（含 e878bdb 编译器修复）
Task 7: complete (commits 3e63edc..efb2149, tests: ctest --test-dir build → 	301 - ConstexprFunctionE2E.ConstexprNestedCall (Skipped))
Task 7: complete (commits efb2149..062b2f2, tests: ctest --test-dir build → 	301 - ConstexprFunctionE2E.ConstexprNestedCall (Skipped))

## Final review（fresh reviewer, general subagent）
- 终审结论：不通过 → 修复 pass（062b2f2）→ 全量 1183/1183 绿。
- Final: fixed C1（&&/|| PHI incoming 硬编码 RHS 起始块，嵌套块型 RHS 即非法 IR/段错误）— LogicalOpsBlockProducingRhs RED(段错误)→GREEN；修复 = rhsEnd = GetInsertBlock()。
- Final: fixed C2（snprintf 截断返回 would-be 长度 → chunk 越界读栈）— FormatClampsTruncatedChunks RED→GREEN；修复 = clamp + %s 类（bool/to_string/char*）strlen 直通（顺带消除截断）。
- Final: fixed I1（split_destroy 接受 []string，数组 decay 后 free 栈指针）— SplitDestroyRejectsStringSlices RED→GREEN；sema 收紧 []str。
- Final: fixed I2（基类截取缺回归钉）— reviewer 实机核验 e878bdb 正确（1~3 级链、嵌套 string 基类、多继承拒绝、无关 struct 不受影响）；INHSliceE2E 即直接基类赋值回归钉，全量回归覆盖。
- 探针插曲：bit8 失败复为测试断言方向写反（!(c&&..) 在 c=0 时本应为真）——编译器无恙，IR 与运行时逐段核实一致。
- Final: minor (deferred): print 渲染槽（{:^N}/自定义 fill）对内嵌 NUL 的 str 按 printf %s 截断（format 路径字节保真）——行为不一致未文档化。
- Final: minor (deferred): "unterminated '{'"/"single '}'" 归类 E2020，语义更宜 E2022。
- Final: minor (deferred): 规格收窄超出文档化范围（{:#x}/{:F}/{:E}/{:G}/{:a}/{:A} 旧曾接受现 E2022）。
- Final: minor (deferred): emitPaddedValue 大 str 居中 → alloca(max(len,width)+1) 栈压力。
- Final: minor (deferred): renderScalar 负返回值未 clamp（不可达，防御风格不一致）。
- Final: minor (deferred): getSplitFn 硬编码 16 字节 stride（{ptr,i64} 布局变更即静默腐坏）。
- Final: minor (deferred): 长 to_string 带宽度规格（{:>N} 等经 snprintf）截断于 343+width 上限——strlen 直通仅覆盖裸 %s。
