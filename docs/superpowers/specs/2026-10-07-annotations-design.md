# 注解系统设计（P1-05 / ANN-01~06）

- 日期：2026-10-07
- 来源：TODO §13 注解系统（ANN-01~10）、PAR-07 挂载点、LEX-10 注解 token、SEM-11/W3004、TYP-21、MEM-09、DEC-08（packed/align 交互）
- 文法权威：`docs/spec/grammar.ebnf` §7（`annotation = '[[' , annotation-body , ']]'`，已钉死）
- 上轮背景：P1-04 落地 `compile_time`；ANN-07 反射（`compile_time.attributes_of`）依赖 `Slice<Annotation>`（P1-06 str），本轮切除。

## §1 范围

**本轮（全纵向）**：
- ANN-01：注解语法解析、AST 挂载（含 LEX-10 词法裁决）
- ANN-06：目标验证、冲突检测
- ANN-02：`[[repr(C)]]`
- ANN-03：`[[packed]]`、`[[align(N)]]`
- ANN-04：`[[inline]]`、`[[cold]]`
- ANN-05：`[[nonnull]]`、`[[deprecated]]`（含 W3004 落地）
- PAR-07：六处挂载点（类型/字段/函数/参数/变量/模块）
- ANN-09/10：各注解语义细则与无注解默认行为——并入本 spec §3/§4，TODO 收口时注记，不另立条目

**切除（挂账）**：
- ANN-07 注解反射：依赖 `compile_time.attributes_of` + `Slice<Annotation>`，待 P1-06 str 落地后另轮
- ANN-08 自定义注解：YAGNI；本轮保守策略 = 未知注解名报错（E2010），落地时降级为注册制
- 位域布局（DEC-08 的位域部分）：语言尚无位域语法，不适用
- 模块注解语义：仅解析记录（ANN-09 也未定义模块级语义）

## §2 语法与 AST（ANN-01 / LEX-10）

### 词法
不新增 token：`[[` 即两个 `TOKEN_LBRACKET`（现有 lexer 逐字符产出，无需改动）。LEX-10 的完成形态 = parser 在注解合法位置对连续两个 `TOKEN_LBRACKET` 特判。注解只出现在**声明位置**（见挂载点），表达式位置不特判——`a[[1]]` 不会误判（嵌套下标不存在，且声明位置无歧义）。

### AST（新 `src/ast/Annotation.h`）
```cpp
struct AnnotationArg {
    enum class Kind { Expr, Type, Ident, String };
    Kind kind;
    std::unique_ptr<ExprAST> expr; // Expr：sema 期常量折叠（align）
    Type* type{};                  // Type
    std::string text;              // Ident / String
    SourceLocation loc;
};

struct Annotation {
    std::string name;               // "repr"/"packed"/"align"/"inline"/"cold"/"nonnull"/"deprecated"
    std::vector<AnnotationArg> args;
    SourceLocation loc;
};
```
实参按注解名解释（不按语法位置）：`align(64)`→Expr 折叠、`deprecated("msg")`→String、`repr(C)`→Ident `"C"`。`repr` 的实参非 `C` → E2011（本轮仅支持 C）。

### 挂载
| 位置 | 载体 | 改动 |
|---|---|---|
| 函数/变量/数组/struct/union/enum/typedef/using/模块 | `DeclAST` 基类加 `std::vector<Annotation> annotations;` | 一处，全部子类免费获得 |
| 参数 | `ParamDeclAST`（非 DeclAST）单独加同名字段 | 一处 |
| 字段 | `StructDeclAST::fields`：`std::vector<std::pair<std::string, Type*>>` → `std::vector<FieldInfo>`（`struct FieldInfo { std::string name; Type* type; std::vector<Annotation> annotations; }`） | **本轮最大侵入点**：`getTypeField`、codegen GEP、sema 字段访问检查、`CompileTimeEvaluator::offset_of`、UnionType::members 等消费点跟进（UnionType::members 同步改为 FieldInfo） |

### 解析位置（前瞻特判，可连写叠加 `[[a]] [[b]]`）
1. 模块声明前（仅记录）
2. 各顶层声明头（`[[...]]` 在类型关键字/存储类之前）
3. struct/class/union 字段声明前
4. 参数声明前（逐参数）
5. 局部变量声明语句前（函数体内，语句位置特判）

与 `compile_time` 前瞻特判同模式；注解序列解析为独立函数 `parseAnnotations()`。

## §3 各注解语义与默认行为（ANN-09/10 吸收）

| 注解 | 合法目标 | 语义 | 备注 |
|---|---|---|---|
| `repr(C)` | struct/union | 固化 C ABI 兼容承诺（本轮 struct 布局已自然 C 兼容，故为 marker + 校验）；class/含继承类型 → E2011 | 与 packed/align 可组合 |
| `packed` | struct/class/union/字段 | 类型级=字段间无填充（LLVM packed literal struct）；字段级=该字段对齐 1 | 带方法的聚合（路由为 class 目标）同样合法（终审 I5 裁决） |
| `align(N)` | struct/class/union/字段/变量（全局/局部） | 类型级=变量声明 `setAlignment(N)` + sizeof 补齐至 N 倍数（尾部 padding 字节）；字段级=字段对齐提升（前置 padding）；类型级 align 传导到该类型变量的声明对齐（终审 I2） | N 须常量折叠且 ≥1、2 的幂，否则 E2013/E2014 |
| `inline` | 函数（自由/成员/static 成员） | LLVM `alwaysinline` 属性 | 声明与定义处均可挂 |
| `cold` | 函数 | LLVM `cold` 属性 | 同上 |
| `nonnull` | 仅指针参数 | LLVM `nonnull` 参数属性 + 调用点静态检查（见 §5 W3005） | 非指针参数 → E2011 |
| `deprecated` | 全部目标 | 使用处 W3004（见 §5） | 实参可选 string，入警告文本 |

**默认行为（ANN-10）**：无注解时——布局=LLVM 自然对齐（现状逐字节不变）、调用约定=目标默认、无附加诊断。全部现状不变，有回归 pin 保证。

**交互钉死**：
- `packed` + `align(N)`：字段无填充 + 整体补齐至 N（GNU `packed,aligned(N)` 语义）
- `repr(C)` + `packed`：允许，字段填充由 packed 决定
- 同一实体重复同一注解：E2012
- 注解间无其他互斥；未知注解间组合不校验（先报 E2010）

## §4 布局单源化（ANN-02/03 实现）

**新 `src/ast/LayoutBuilder.{h,cpp}`**（单源化裁决）：
- 职责：输入聚合类型的字段序列 + 字段级/类型级注解 → 输出布局结果（各字段 llvm::Type 与偏移、插入的 padding 字段、`isPacked`、总 size、align）。
- 消费者（全部改走 LayoutBuilder）：
  1. `CodegenContext::getLLVMType`（Struct/Union case）
  2. `StructDeclAST::codegen`（类路径同步）
  3. `CompileTimeEvaluator::toLLVMType`
  4. `CompileTimeEvaluator::evalLayoutQuery`（offset_of/size_of/align_of）
- **硬约束**：`offset_of`/`size_of`/`align_of` 的返回值必须与实际 IR 布局（GEP 偏移 / alloc size / ABI align）一致——由组合矩阵一致性测试结构性保证，不靠人工同步。
- **回归 pin**：LayoutBuilder 落地时，无注解聚合的布局必须与落地前逐字节一致（既有全部测试 + 新增 pin）。
- Union：packed 语义对 union = size 不变（union 本无填充），仅 align 有效；spec 注记。

## §5 诊断（码位钉死）

| 码 | 文案 | 触发 |
|---|---|---|
| E2010 | `unknown annotation '<name>'` | 注解名 ∉ 七个已知名（ANN-08 预留：自定义注解落地时降级注册制） |
| E2011 | `annotation '<name>' is not valid on <target>` | 目标不符；含 `repr` 实参非 `C`、`nonnull` 修饰非指针参数、`repr(C)` 用于 class |
| E2012 | `duplicate annotation '<name>'` | 同一实体重复同注解 |
| E2013 | `align argument must be a power of two` | align 实参折叠后非 2 的幂或 < 1 |
| E2014 | `annotation argument must be a compile-time constant` | align 实参非常量 |
| W3004 | `'<name>' is deprecated[: <msg>]` | 使用处：函数调用（Call/MethodCall 解析后）、变量引用（VariableExpr 解析）、类型使用（VarDecl/参数/字段/cast 类型解析）、字段访问（MemberAccess） |
| W3005 | `null passed to nonnull parameter '<param>' of '<fn>'` | 调用点实参为字面 `nullptr` 或整型常量 0，且形参挂 nonnull |

- 解析层错误复用 E1001/E1002。
- W3004 已注册未发射（SEM-11 挂账），本轮落地；W3005 新增注册。
- 诊断走现有 `emitError`/`emitWarning` 快照机制。

## §6 测试与收口

- 每注解 sema + e2e 各 ≥1 正例；ANN-06 目标校验负例表（每个 E2010~E2014 至少一例）；W3004/W3005 快照测试。
- 一致性矩阵：{默认, repr(C)} × {无, packed} × {无, align(8/64)} × 字段级 align × union——size_of/offset_of/align_of 值 vs 实际 IR 布局断言相等。
- 无注解回归 pin：全部既有测试不变 + LayoutBuilder 前后布局逐字节一致。
- 任务切分预告（writing-plans 细化）：T1 解析层（Annotation/AST/parser 挂载）→ T2 ANN-06 验证 → T3 LayoutBuilder 单源化（回归 pin）→ T4 布局注解（packed/align/repr）→ T5 函数注解 + W3004/W3005 → T6 硬化收口（TODO：ANN-01~06/LEX-10/PAR-07 → [x]；SEM-11/TYP-21/MEM-09 注记；ANN-07/08/09/10 挂账注记）。
- 已知风险：FieldInfo 化 fields 是最大侵入点，T1 单任务内完成并以全量回归兜底；getLLVMType 热路径改动由 T3 回归 pin 兜底。
