# P1-04 设计——`compile_time` 编译期设施（核心纵向切片）

> 对应 TODO：**CT-01/02/03/04/05/06/12/14**、**PAR-15**（部分）、**SEM-07**、**DEC-05**（裁决）。
> 上游规范：`docs/spec/compile_time.md`（草案）；本 spec 将其中本轮条目固化为实现决策。
> 状态：草案 → 已批准（2026-10-06，用户批）。

## 0. 范围

**本轮做**：
- CT-01 语法入口（`compile_time` 非关键字、成员前缀特判）
- CT-02 `compile_time.static_assert(cond, msg?)`
- CT-03 `compile_time.if` 条件编译 + SEM-07 死代码消除（**仅顶层**）
- CT-04 目标查询 `target.os/arch/cpu`
- CT-05 构建查询 `build.debug/optimize/version`
- CT-06 编译期求值器（独立类，共享内核）
- CT-12 LLVM 常量集成（`ctValue` memoized → `llvm::Constant`）
- CT-14 / **DEC-05 裁决：并存 + 共享求值器**——`constexpr` 保持现有语义（变量/函数、运行时可调用），`compile_time` 负责编译期设施；两者共享求值器内核（constexpr 函数调用委托既有 `evaluateConstexpr`，其函数解释种子已存在）

**本轮切掉**（ledger）：
- CT-07/08/13 反射 API 与类型作为值 → 另轮（FieldInfo 依赖 str/Slice，与 P1-06 排序协调）
- CT-09 缓存/增量 → 随 MOD-10
- CT-10 只做「诊断带源码位置」；求值栈/原因链不做
- CT-11 沙箱 → YAGNI（求值器无 IO 能力即天然沙箱）
- CT-03 函数体内语句级 → YAGNI（仅顶层，用户已批）

## 1. 解析层（CT-01）

- `compile_time` **不是关键字**（keywords.md §9 既定）。特判走「根标识符为 `compile_time` 的成员链」：
  - `compile_time.static_assert(...)` → MethodCall 形态（`compile_time` + `.` + 成员 + `(`）
  - `compile_time.target.os` / `compile_time.build.*` → MemberAccess 链形态
  - `compile_time.size_of(T)` / `align_of(T)` / `offset_of(T, "f")` → MethodCall 形态
- **消歧规则**：仅当成员名 ∈ 已知集合（`static_assert`/`if`/`target`/`build`/`size_of`/`align_of`/`offset_of`）才走编译期路径；其余按普通标识符解析（用户自定义 `compile_time` 变量/命名空间不受影响）。
- **新 AST 节点**（顶层两种形态）：
  ```cpp
  // compile_time.if (cond) { decls... } [else { decls... }] —— 仅顶层
  class CompileTimeIfDeclAST : public DeclAST {
  public:
      std::unique_ptr<ExprAST> cond;
      std::vector<std::unique_ptr<DeclAST>> thenDecls, elseDecls;
      llvm::Value* codegen(CodegenContext& ctx) override; // 恒 nullptr（sema 期已选择）
  };
  // 顶层 compile_time.static_assert(...) 的包装（函数体内走普通表达式路径，无需包装）
  class CompileTimeAssertDeclAST : public DeclAST {
  public:
      std::unique_ptr<ExprAST> call;
      llvm::Value* codegen(CodegenContext& ctx) override; // 恒 nullptr
  };
  ```
- `parseDeclarationImpl` 前瞻特判：标识符 `compile_time` + `.` + `if` → CompileTimeIfDeclAST；`compile_time` + `.` + `static_assert` → CompileTimeAssertDeclAST。**两个分支都 parse**（语法仍解析，spec §3 钉死），选择在 sema。
- 类型实参（`size_of(Point)` 的 `Point`）：**按名字解析**（sema 期 `resolveTypeByName`：class/struct/typedef/enum/union 表），不是一等类型值——CT-08 切除后的最小落地。

**已知脏点（接受并 ledger）**：parse 全文件先于 sema 选择——未选分支里的 struct/class 会在 parse 期注册 TypeContext 占位（isTypeStart 可见）；sema 不 visit 它们（无定义、无 codegen），类型名占位残留。与既有 forward-decl 行为同级别。

## 2. 求值器（CT-06 / CT-14）

新文件 `src/sema/CompileTimeEvaluator.{h,cpp}`，独立类（SemanticAnalyzer.cpp 已 ~3000 行，隔离边界）：

```cpp
class CompileTimeEvaluator {
public:
    explicit CompileTimeEvaluator(SemanticAnalyzer& sema); // 类型解析/constexpr 委托用
    // 求值表达式；失败返回 nullopt 并已发诊断（需 ASTNode 定位）。
    std::optional<ConstValue> eval(ExprAST* expr, ASTNode& at);
};
```

- **ConstValue 扩展**：加 `STR`（`std::string`，编译期字符串）；BOOL 复用 INT（0/1，语义层判真值）。`ConstValue` 定义**迁至 `CompileTimeEvaluator.h`**（单一权威定义）；`SemanticAnalyzer` 内以 `using ConstValue = CompileTimeEvaluator::ConstValue;` 保持既有引用不改动。
- **支持集**（spec §6 钉死）：整/浮/布/字符/字符串字面量；算术/位/比较/逻辑/三元；字符串 `==`/`!=`/`+` 拼接；`size_of`/`align_of`/`offset_of`；constexpr 函数调用（**委托** `SemanticAnalyzer::evaluateConstexpr`——含既有函数解释种子，DEC-05 共享内核落地）。
- **布局查询**：evaluator 私有 host TargetMachine + 独立 LLVMContext/Module；自建最小 `Type* → llvm::Type` 映射（标量/指针/数组/struct/class/union，递归 + 缓存），`DataLayout` 求 alloc size/align/字段偏移（struct 含基类子对象槽，与 codegen 布局一致）。
- **上限**：递归深度 64、单表达式节点求值次数上限，超限诊断（防编译期爆栈）。
- **STR 逃逸**：类型系统无 `str`（P1-06 未做）——STR 值赋给运行时变量/传参自然产生类型不匹配错误，无需专门逃逸诊断。

## 3. sema 行为（CT-02/03/04/05）

钩子：`visit(MethodCallExprAST)`（对象为 VariableExpr `compile_time` 且成员名在集合内）与 `visit(MemberAccessExprAST)`（根为 `compile_time` 的链）。特判先于普通解析。

**诊断文案（钉死）**：
| 场景 | 文案 |
|---|---|
| 断言失败（有 msg） | `static_assert failed: <msg>` |
| 断言失败（无 msg） | `static_assert failed` |
| 条件非常量 | `compile_time argument must be a compile-time constant` |
| if 条件非常量 | `compile_time.if condition must be a compile-time constant` |
| if 条件非布尔 | `compile_time.if condition must be a boolean` |
| 未知成员 | `unknown compile_time member '<name>'` |
| 深度超限 | `compile_time evaluation depth limit exceeded (64)` |
| 类型实参未知 | `unknown type '<name>' in compile_time expression` |

- **CT-02**：cond 非常量 → 上表诊断；求值 INT==0 → 断言失败诊断（带源码位置）。函数体内 static_assert 走表达式钩子，顶层走 CompileTimeAssertDeclAST。
- **CT-03**：`compile_time.if` cond 求值 INT（非零真）/诊断；仅选中分支的 decls 追加进 TU visit 序列（`analyze` 主流程内、按声明位置原位展开），未选分支不 visit、不 codegen（SEM-07）。
- **CT-04/05**：`target.os`→`"linux"/"macos"/"windows"`（host triple 映射）、`target.arch`→`"x86_64"/"aarch64"`、`target.cpu`→host CPU 名；`build.debug`→setter 注入（默认 false）、`build.optimize`→driver optLevel（默认 `"O0"`）、`build.version`→`"1.0.0"`。CLI flag 本轮不加（YAGNI），测试经 setter 注入两态。

## 4. 表达式落地（CT-12）

- `CallExprAST`/`MethodCallExprAST`/`MemberAccessExprAST` 加 `std::optional<ConstValue> ctValue`：钩子求值成功 → 写入 + `node.type` 置为对应类型（INT→int32/int64 按值域、STR→标记、布局查询→usize）。
- codegen 遇 `ctValue` → 直接 `llvm::Constant`（INT/FP 常量；STR 出现即内部错误，因无 str 类型不可能到这），**零指令**。
- 覆盖位置随之成立：constexpr 变量初始化器（`constexpr usize N = compile_time.size_of(Point);`）、static_assert/if 条件、普通变量初始化器、数组维度暂不支持（parse 期字面量限制不变，YAGNI）。

## 5. 测试计划

**e2e**（`tests/e2e/test_compile_time.cpp`，fixture 仿 GenericE2E）：
- `TargetOsIsLinux`：`compile_time.static_assert(compile_time.target.os == "linux", "...")` 通过
- `CompileTimeIfSelectsBranch`：真分支使用、假分支引用未定义符号仍编译通过（死代码消除证据）；反向分支
- `BuildDebugSetter`：debug 两态注入结果不同
- `SizeOfAlignOfOffsetOf`：标量/struct（含嵌套/基类槽）/union
- `ConstVarFromCompileTime`：`constexpr usize N = compile_time.size_of(...)` + 数组使用
- `StringConcatInAssert`：字符串拼接/比较进断言
- `InstanceSharedAcrossFunctions` 式回归 pin：compile_time 调用跨函数/多处使用

**sema**（`tests/sema/test_compile_time.cpp`，前缀 `CT*`）：
- 断言失败带 msg/不带 msg、条件非常量、未知成员、if 非布尔、深度超限（>64 层表达式）、用户自定义 `compile_time` 变量不被劫持

**任务拆分预估**（7 任务，TDD 每任务 RED→GREEN + 全量绿 + 提交）：
T1 解析层（两节点 + 前瞻）→ T2 求值器骨架（ConstValue 迁出+STR+运算）→ T3 target/build + 钩子/ctValue 管线 → T4 布局查询（size_of/align_of/offset_of）→ T5 static_assert → T6 compile_time.if + SEM-07 → T7 硬化 + TODO/Progress 收口。

## 6. TODO 收口清单（T7 执行）

`[x]`：CT-01/02/03/04/05/06/12/14、PAR-15（部分，注明）、SEM-07、DEC-05（并存裁决）。
`[ ]` 保留：CT-07/08/09/13（另轮）、CT-10（部分，注明）、CT-11（YAGNI 注明）。
