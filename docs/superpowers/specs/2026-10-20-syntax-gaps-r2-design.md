# 语法缺口收口（第二轮）— 设计文档

> 对应 TODO：TYP-04 / PAR-06 / TYP-05 / PAR-08 / PAR-03 / SEM-17 / GEN-02。
> 状态：`[plan]`。本文为作者（+ builder-check 评审）决策后的权威来源。
> 关联：`docs/superpowers/specs/2026-10-18-syntax-gaps-design.md`（第一轮，已完成）。

## 1. 背景与目标

第一轮语法审计（2026-10-18）收口后，用户继续问"语法上还有哪些缺失"。基于
`docs/spec/grammar.ebnf` / `docs/spec/keywords.md` 的 `[plan]` 标记与实测，发现
第二轮缺口。用户裁定：

- **虚函数/vtable 是 Non-goal**（INH-07 维持；语言"无隐藏控制流"原则）。
- **可变参数模板不做**（GEN-10 维持）。
- **其余全做**：运算符重载（一元/集合收敛/下标/调用）、类外方法定义、`f16` 系列类型
  关键字、指针后置 `restrict`、UTF 前缀字面量、复合字面量、slice 字面量、
  非类型模板参数作为值。
- 一元 operator 重载**对齐 C++ 签名**（`operator++/--` 前缀无参、后缀带 `int32` 哑参）。
- 复合字面量/slice 字面量 = **栈对象，禁转义**（对齐 C99 自动存储；文件级 = 静态常量）。

**目标**：实现上述 10 项语法特性，全部 TDD（RED→GREEN→独立 commit，引用 TODO ID），
同步滞后文档，全量回归无失败。

**非目标**：虚函数/vtable（INH-07 Non-goal）；可变参数模板（GEN-10 Non-goal）；
restrict 的完整语义/优化（MEM-04 仅语法接受，语义挂后）；`u8/u16/u32` 前缀字面量
的完整 Unicode 转义族（复用既有 decodeEscapes 即可）；operator 复合赋值重载
（`operator+=` 等，SEM-17 集合不含，不引入）；`operator,` 重载；常量/static 成员
operator。

## 2. 各特性设计

### 2.1 `f16/f32/f64/f128` 类型关键字（TYP-04 剩余）

- **现状**：`float32/float64/float16/float128` 已是关键字（Lexer.cpp:122-125 注册
  `TOKEN_FLOAT32/64/16/128`）；`f16/f32/f64/f128` 仅被 `scanNumber` 作为**字面量后缀**
  消费（Lexer.cpp:372 最大匹配），不是类型关键字——实测 `f16 a = 1.0;` 报
  `E2031: unknown type 'f16'`。
- **语法**：`f16 ≡ float16`、`f32 ≡ float32`、`f64 ≡ float64`、`f128 ≡ float128`。
- **实现落点**：`Lexer.cpp` keywordMap 补 4 个条目（`f16→TOKEN_FLOAT16` 等）。
  `Parser.cpp` 的 `isTypeStart`/`parseBaseType`/`parseTypeKeyword` 已认
  `TOKEN_FLOAT16/32/64/128`（Parser.cpp:1817-1820、1997-2009），零改动。
- **词法安全**：`1.0f16` 的后缀在 `scanNumber` 内按最大匹配消费，不经 keywordMap，
  注册别名不破坏字面量后缀（回归 pin `1.0f16` 类型仍为 Float16）。
- **测试**：parser 类型关键字映射（4 项）+ e2e 声明/运算（2 项）+ 后缀回归 pin（1 项）。

### 2.2 指针后置 `restrict` 限定符（PAR-06）

- **现状**：`parseType` 的限定符循环只接受 `const/volatile/atomic`（Parser.cpp:478/:507），
  `int32* restrict p` 报 `expected ')' after parameter list`。Type 无 `isRestrict` 字段。
- **语法**：`T* restrict`、`T* const restrict`、`T* restrict const` 等指针后缀限定符
  组合（C 语义：restrict 只修饰指针，声明接受即可，无运行时语义）。
- **实现落点**：
  - `src/ast/Type.h`：`Type` 补 `bool isRestrict = false`（仅 Pointer 用）。
  - `Parser.cpp` 指针后缀循环（parseType 内处理 `*` 后的限定符）：循环条件加入
    `TOKEN_RESTRICT`，设置 `isRestrict`；恢复 token 后继续循环。
  - 限定符不得重复（`restrict restrict`）——沿用 const/volatile 重复即报错的既有路径
    （若 const/volatile 重复本就不报，则 restrict 重复也不报，记文档口径即可）。
- **测试**：parseType 映射（指针 isRestrict 置位）+ e2e 参数/变量声明接受（2 项）。
  不做 restrict 别名分析（Non-goal，记文档）。

### 2.3 `u8/u16/u32` 前缀字面量（TYP-05 剩余）

- **现状**：`char8/char16/char32` 类型关键字已存在（uint8/16/32 别名）；字符串字面量
  无前缀识别（`scanString` 只认裸 `"` 与 `r"`）。实测 `char8 s[] = u8"hi"` 报错。
- **语法**：`u8"..."` → `char8[]`（数组类型，可退化为 `char8*`）；`u16"..."` →
  `char16[]`；`u32"..."` → `char32[]`。无前缀 `"..."` 保持现状（`str`/string_view）。
- **实现落点**：
  - `Lexer.cpp` `scanString`/`scanToken`：在 `r"` 分支旁加 `u8"`/`u16"`/`u32"`
    前缀识别——`peek()=='u'` 且 `peekNext()=='8'`（或 16/32）且 `peekNext2()=='"'`
    时进入带前缀字符串扫描，token 携带 `StringLiteralPrefix`（NONE/U8/U16/U32）。
  - `Token.h`：token 加 `StringPrefix` 字段（或复用 literalKind 思路新增枚举）。
  - `Parser.cpp`：`StringExprAST` 构造时记录前缀。
  - `src/ast/Expr.h`：`StringExprAST` 加 `StringLiteralPrefix prefix`。
  - sema/codegen：按前缀映射类型——u8→`char8[]`（Array(char8)），u16→`char16[]`，
    u32→`char32[]`；codegen 生成对应定宽数组全局常量 + 退化指针。
  - 字符串内容按字节存储（不转码）：u16/u32 前缀若内容含多字节 UTF-8 如何编码？
    **裁定**：前缀字面量内容按字节序列直接存入定宽数组，逐字节拷贝，不做 Unicode
    编码转换（YAGNI——无 Unicode 语义需求，记文档）；转义仍走 decodeEscapes。
- **测试**：lexer 前缀识别（3 项）+ parser AST 前缀（3 项）+ e2e `char8[]`/`char16[]`/
  `char32[]` 初始化与 `.len`（3 项）。

### 2.4 C99 复合字面量 `(T[]){...}`（PAR-08 切片表达式补充）

- **现状**：`parsePrimary` 的 `(` 分支（Parser.cpp:469-498）先投机解析 cast——
  `(int32[]){1,2}` 被 parseType 成功后当 cast 解析 operand 遇 `{` 失败回退成
  括号表达式，随后报 `E2006 incompatible cast` 或解析混乱。
- **语法**（对齐 C99）：`( type-name ) { initializer-list }`。type-name 可为
  数组/struct/union/标量。块作用域 = 自动存储（栈临时对象）；文件作用域 = 静态常量。
- **实现落点**：
  - `Parser.cpp` parsePrimary 的 `(` 分支：cast 探测成功后，若下一 token 是 `{`，
    则解析初始化列表 → 构造新节点 `CompoundLiteralExprAST(type, initList)`；
    否则维持既有 cast/括号表达式逻辑。
  - `src/ast/Expr.h`：新增 `CompoundLiteralExprAST`（持 type + 初始化列表，
    类似聚合初始化但独立成节点，sema 按目标类型走聚合初始化校验）。
  - sema：类型校验走既有聚合初始化路径（struct/array/union）；标量类型拒绝或
    退化为普通 cast（**裁定**：标量 `(int32){5}` 接受，等价 `(int32)5`）。
  - codegen：块内 → `alloca` 匿名临时对象 + 初始化存储（复用聚合初始化 codegen）；
    文件级 → 全局匿名常量。
- **`T[]` 语义裁定（builder-check Q2）**：本语言 `T[]` 是 **slice** 类型，不是 C 的
  不完整数组。故：
  - `(int32[3]){1,2,3}` → **定长数组**复合字面量（Array 类型）。
  - `(int32[]){1,2,3}` → **slice 复合字面量**（栈匿名数组 + `{ptr,len}` 视图），与
    §2.5 slice 字面量共享实现（表达式形式 vs 声明形式）。
- **禁转义边界（用户裁定 + builder-check Q1 细化）**：
  - 块内复合字面量是自动存储，**直接转义**（`&` 取址、`return` 表达式、赋值给
    全局变量的初始化/赋值表达式**直接**是复合字面量）→ 悬垂拒绝。
  - **不追踪副本**：`int32[] t = s; return t;` 不查（副本转义是用户责任，记文档
    口径；全量 provenance 分析属另立项）。**裁定为 my call**。
  - **by-value 拷贝到全局允许**：`g = (Point){1,2}`（标量/struct 值拷贝，非悬垂）
    不拒绝；仅 slice/数组（含指针语义）复合字面量直赋全局拒绝。
  - "文件级 = 静态常量"仅指文件作用域直接写 `(int32[2]){1,2}` 作为全局初始化器
    （§6 全局初始化检查即此）；块内字面量不能出现在文件级常量初始化器。
- **测试**：parser 识别（2 项）+ e2e 数组/struct 复合字面量（3 项）+ 禁转义拒绝
  （2 项）。

### 2.5 slice 字面量初始化 `T[] s = {1,2,3}`（PAR-08 切片表达式补充）

- **现状**：`int32[] s = {1,2,3};` 报 `initializer list cannot initialize type 'slice'`
  （SemanticAnalyzer.cpp:3717——聚合初始化器分支无 Slice case）。
- **语法**：初始化列表直接初始化 `T[]`（Slice）变量——列表长度 = 元素个数，生成
  栈上匿名数组 + Slice 视图 `{ptr, len}`。
- **实现落点**：
  - sema：`visit(InitializerListExprAST)`（或聚合初始化分析处）对目标类型为 Slice
    增加分支——元素类型 = Slice 元素，逐元素校验/折叠，`len` = 列表长度；若元素
    初始化失败即报错。
  - 为承载存储，sema 在函数体声明区为该变量注册一个伴随匿名数组（或 codegen 侧
    alloca 匿名数组 + 填充 + 组装 `{ptr,len}`）。
  - codegen：alloca 定长匿名数组（元素类型 × len）→ 逐元素 store → `emitArrayToSliceDecay`。
  - 变量本身仍是 Slice 类型；后续 `.len`/下标走既有 Slice codegen。
- **禁转义**：返回该 slice / 存入全局 → 悬垂拒绝（用户裁定，镜像复合字面量；
  同样不追踪副本，by-value 元素拷贝不受限）。
- **测试**：e2e 初始化 + `.len` + 下标读写（3 项）+ 禁转义拒绝（1 项）+ 元素类型
  不匹配拒绝（1 项）。

### 2.6 类外方法定义 `S::get() {...}`（PAR-03）

- **现状**：`parseDeclarationAsType`（Parser.cpp:2809）解析完返回类型后，只认
  `IDENTIFIER (`（普通函数）与 `operator`，不认 `IDENTIFIER :: IDENTIFIER (`。
  实测 `int32 S::get() {...}` 报 `expected ';' after variable declaration`。
  TODO PAR-03 声称"方法定义语法 `Vector::length()`"已实现，但仅指**类体内**定义
  （类体循环 parseClassDecl 内）；类外定义缺。
- **语法**：`RetType ClassName::methodName(params) [= init] { body }`——声明在类内、
  定义在类外（对齐 C++ 类外定义；本语言无头文件，仍允许类外定义以便聚合声明）。
- **实现落点**：
  - `Parser.cpp` parseDeclarationAsType：`IDENTIFIER :: IDENTIFIER (` 形态 → 解析
    为 `MethodDefinitionAST`（或复用 FunctionDeclAST 加 `qualifierClass` 字段）。
    **裁定**：复用 `FunctionDeclAST`，新增字段 `className`（限定类名，空 = 普通函数）。
  - `Token.h`/`Utils.cpp`：token 名表无需新增（COLON_COLON 已有）。
  - sema：`visit(FunctionDeclAST)` 若 `className` 非空——查找该类，若类内已有同名
    方法声明则**校验签名一致后绑定**（不新增方法，更新既有方法的定义体）；
    类内无声明则诊断（`method 'X' not declared in class 'S'`）；类不存在 E2031。
    类外定义的方法体按普通方法分析（隐式 this、访问级别复用 DEC-01）。
  - codegen：mangle 用类名前缀（与类内定义一致，复用 `mangleMethod`）。
- **测试**：类外定义 + 调用（e2e 1 项）+ 签名不一致拒绝（sema 1 项）+ 类内未声明
  拒绝（sema 1 项）。

### 2.7 一元 operator 重载调用点（SEM-17）

- **现状**：声明侧 parser 支持 `operator-`/`operator~`/`operator++`/`operator--`
  （Parser.cpp:2843-2868 符号表含 `- ~ ++ --`，自由函数/类内方法均可声明）；
  **调用点 sema `visit(UnaryExprAST)` 完全不查 operator**（SemanticAnalyzer.cpp:1826
  只做内建检查），实测 `-v` 报 `invalid operand to unary '-'`。codegen 侧
  `UnaryExprAST::codegen`（Expr.cpp:644）无 mangledCallee 通道。
- **语法/签名**（用户裁定对齐 C++）：
  - `operator-` / `operator+` / `operator~` / `operator!`：单参（无 this 时 1 参，
    方法形式 0 参）。
  - `operator++` / `operator--`：**前缀** = 无参（方法形式 0 参 / 自由函数 1 参）；
    **后缀** = 带 `int32` 哑参（方法形式 1 参 / 自由函数 2 参）。
- **实现落点**：
  - sema：`visit(UnaryExprAST)` 在 `switch(node.op)` 内建检查**之前**，若 operand
    类型为聚合（struct/union/class，复用 `isOverloadOperand`），按 op 查
    `operator-`/`operator+`/`operator~`/`operator!`（一元）/ 前缀 `operator++`/`--`
    （PreInc/PreDec）/ 后缀 `operator++`/`--`（PostfixIncDecExprAST 单独处理），
    查到即 `node.mangledCallee = mangleFunction(opName, {operandType})` 且
    `node.type = 返回类型`。
  - `UnaryExprAST` 补 `mangledCallee` 字段（镜像 BinaryExprAST）。
  - codegen：`UnaryExprAST::codegen` 若 mangledCallee 非空 → CreateCall。
  - 后缀 `x++`/`x--` 走 `PostfixIncDecExprAST`：sema `visit` 内查 `operator++(int32)`
    （哑参 0 折叠），codegen CreateCall。返回旧值语义由重载函数自身实现（
    **裁定**：本语言重载 `operator++(int32)` 返回新值，调用点不自动生成旧值快照——
    对齐 C++ 惯例由实现者处理，记文档；简单类型用内建不受影响）。
  - 一元内建检查保留：找不到 operator 时维持既有 `invalid operand` 诊断。
  - **触发类型（builder-check Q6）**：与二元一致复用 `isOverloadOperand`
    （struct/union/class/Optional/Result），一致性优先。
  - **mangle 口径（builder-check Q5）**：沿用既有二元路径——`OverloadSet::resolve`
    找候选，但 mangledName 用 raw 操作数类型构造（`mangleFunction(opName,
    {operandType})`）。既有模式潜在 mismatch（resolved 参数类型 ≠ raw 操作数类型）
    如实镜像，不在本轮修正（属既有泛化缺陷，记文档）。
- **测试**：`operator-` 一元（e2e 1 项）+ `operator~`（e2e 1 项）+ 前缀/后缀
  `operator++`（e2e 1 项）+ 非聚合类型仍走内建（回归 1 项）。

### 2.8 二元 operator 重载集合收敛（SEM-17）

- **现状**：声明侧 parser 支持 18 种符号（Parser.cpp:2843-2868：`+ - * / % == !=
  < > <= >= && || & | ^ << >> ~ ++ --`）；调用点 sema `visit(BinaryExprAST)`
  （SemanticAnalyzer.cpp:1771）只查 **10 种**（`+ - * / == != < > <= >=`，
  switch 缺 `% && || & | ^ << >>`）。`getOperatorMangledName`
  （SemanticAnalyzer.cpp:499）同样只 10 种。实测 `a % b` 对 struct 报
  `invalid operands to binary '%'`。
- **目标**：调用点收敛到与声明侧一致的集合——`+ - * / % == != < > <= >= && ||
  & | ^ << >>`（17 种二元；`~` 是一元已含 2.7；`++ --` 已在 2.7）。
- **实现落点**：
  - sema：`visit(BinaryExprAST)` 的 operatorName switch 补
    `Mod→"operator%"`、`And→"operator&&"`、`Or→"operator||"`、`BitAnd→"operator&"`、
    `BitOr→"operator|"`、`BitXor→"operator^"`、`Shl→"operator<<"`、`Shr→"operator>>"`。
  - `getOperatorMangledName` 同步补齐（8 个 case）。
  - codegen：`BinaryExprAST::codegen` mangledCallee 通道已通用（Expr.cpp:389），
    零改动。**注意** `&&`/`||`：重载后**不做短路**（对齐 C++——重载 `operator&&`/
    `operator||` 是普通函数调用，两个实参都求值），codegen 走 CreateCall 分支
    （短路分支只服务内建，mangledCallee 优先已保证）。
  - 内建检查保留：无 operator 时维持既有诊断。
- **测试**：`%`、`<<`、`&&` 三个代表重载 e2e（3 项）+ 非聚合回归（1 项）。

### 2.9 `operator[]` / `operator()` 重载（SEM-17 扩展）

- **现状**：parser operator 符号表（Parser.cpp:2843-2868）无 `[` 与 `(`——
  `operator[]`/`operator()` 声明报 `expected operator symbol after 'operator'`。
- **语法**（对齐 C++）：
  - `operator[]`：`T operator[](IndexType i)`（自由函数 2 参 / 方法形式 1 参），
    **不承诺多参下标**（YAGNI）。
  - `operator()`：`T operator()(Args...)`（任意参数个数，含 0）。
- **实现落点**：
  - `Parser.cpp` operator 符号表：`TOKEN_LBRACKET` → `"[]"`；`TOKEN_LPAREN` →
    `"()"`（注意 `operator(` 后要跳过 `)` 才算符号，再跟参数表——`operator()(...)`：
    **裁定**声明写 `operator()(...)`，首 token 为 `(` 时直接认 `()` 符号并消费一对
    空括号，然后进入 parseFunctionDecl 的参数表）。
  - AST：operator 声明复用 parseFunctionDecl → FunctionDeclAST，函数名 = `operator[]`
    / `operator()`，自由函数直接按名注册（OverloadSet 已按名）。
  - 调用点：
    - **下标**：`ArrayAccessExprAST`（Expr.h:344）。sema `visit` 若 array 表达式类型
      为聚合且无内建下标适用，查 `operator[]`，设置 mangledCallee（补字段）+
      返回类型；codegen CreateCall(array, index)。
    - **调用**：`CallExprAST`（Expr.h:205）callee 为表达式（非函数名）时——目前
      callee 是 `std::string`。**裁定**：仅支持**对象成员形式** `obj(args)` 通过
      `MethodCallExprAST`？非——`obj(...)` 是 CallExprAST。需新增：`CallExprAST`
      允许 callee 为表达式（新增 `std::unique_ptr<ExprAST> calleeExpr` 字段，
      与字符串 callee 二选一）；sema 若 calleeExpr 类型为聚合查 `operator()`；
      codegen 按 mangledCallee CreateCall（对象作为首参）。
  - 对象作首参：mangle 用 `mangleFunction("operator[]", {objType, idxType})` /
    `mangleFunction("operator()", {objType, argTypes...})`。
  - **触发类型（Q6）**：`operator[]` 查找沿用 `isOverloadOperand`（聚合含
    Optional/Result）；`operator()` 对聚合对象（struct/union/class）查找。
  - **调用机制（Q5）**：`operator()` 走窄 operator 路径（OverloadSet::resolve +
    mangledCallee + CreateCall），**不承诺**默认参数折叠/显式模板实参/重载打分
    的全套 CallExprAST 机制（记文档；需要时另立项）。
- **测试**：`operator[]` 读写（e2e 1 项）+ `operator()`（e2e 1 项）+ 内建数组下标
  回归（1 项）。

### 2.10 非类型模板参数作为值（GEN-02 剩余）

- **现状**：`template<int32 N> int32 c() { return N; }` 报 `E2001: undeclared
  identifier 'N'`。`TemplateInstantiator::rewrite`（TemplateInstantiator.cpp:33-51）
  只把 `ArrayType::sizeParam` 用 `m_valueArgs` 具体化；函数体内引用 N 的
  `VariableExprAST` 未被替换。`TemplateRegistry.cpp:100-118` 已建 `valueArgMap`
  （参数名→值），但仅用于数组维度。
- **语义**：非类型参数 N 在模板体内**任何值位置**可用，作为编译期整数常量
  （对齐 C++ 非类型模板参数）。
- **实现落点**：
  - `TemplateInstantiator`：新增 AST 节点克隆规则——`VariableExprAST` 若 name 在
    `m_valueArgs` 中，克隆为 `NumberExprAST(值)`（或带 `isTemplateValueConst`
    标记的常量节点，走既有常量折叠）。
  - `TemplateRegistry::instantiateFunction`/`instantiateClass`：`valueArgMap` 已建，
    传入 TemplateInstantiator 时保留；`instantiateByKey` 现传 `valueArgMap`（核对
    TemplateInstantiator.h:16 构造签名，已收 `m_valueArgs`）。
  - sema 模板体分析：N 作为标识符在**定义处**不应报 undeclared——现有定义处只
    parse（GEN-06），实例点才全量 sema；实例化后 N 已被替换为常量，诊断自然消失。
  - 常量上下文：N 值可用于数组维度（既有）、`return`、算术、`compile_time` 常量
    位置（实例化后即字面量，天然支持）。
  - 类型参数 vs 值参数区分：克隆规则只认 `m_valueArgs`（值参数名），类型参数名
    不在此表，走既有 TypeVar 替换。
  - **局部遮蔽（builder-check Q4）**：name-only 替换会把局部同名变量误替换。裁定：
    sema 在模板体分析（实例点）时，若函数/类体内声明了与值参数同名的局部变量，
    **报诊断拒绝**（`shadowing of template value parameter 'N' is not allowed`）。
    实现：sema 模板体作用域注册时检测与 valueArgMap 键冲突。简单、无误编译。
  - **模板实参位置（builder-check Q3）**：`foo<N>()`（非类型参数作为显式模板实参）
    **延后**——parser 显式值实参只收字面量数字（Parser.cpp:874-876），延迟折叠需
    模板体内把 N 折叠成常量后再喂回实参列表，属独立机制。记文档（GEN-02 范围外）。
- **测试**：函数模板 `c<5>()` 返回 N（e2e 1 项）+ struct 模板数组维度既有回归
  （1 项）+ N 参与算术常量折叠（e2e 1 项）+ 类型参数不受影响（回归 1 项）+
  局部遮蔽拒绝（sema 1 项）。

## 3. 文档同步

- `docs/spec/grammar.ebnf`：`f16/f32/f64/f128` 类型关键字去 `[plan]`；pointer-suffix
  补 `restrict` `[impl]`；复合字面量/前缀字面量产生式补 `[impl]`；类外方法定义说明；
  operator 重载集合注记同步 17 二元 + 一元 + `[]`/`()`。
- `docs/spec/keywords.md`：`f16/f32/f64/f128` 由 `[plan]` → `[impl]`；`restrict`
  状态更新；`u8/u16/u32` 前缀记 TYP-05。
- `docs/spec/semantics.md`：SEM-17 可重载集合收敛表（17 二元 + 4 一元 + `[]`/`()`）；
  复合字面量/前缀字面量/slice 字面量存储与禁转义；非类型模板参数常量语义。
- `TODO.md`：TYP-04/PAR-06/TYP-05/PAR-08/PAR-03/SEM-17/GEN-02 更新 + 进度快照；
  INH-07（虚函数）与 GEN-10（可变参数模板）加"用户确认维持 Non-goal"注记。
- `Progress.md`：追加本轮完成记录。

## 4. 测试与验证

- 每项 TDD：先写失败测试（RED），实现后转绿（GREEN），独立 commit 引用 TODO ID。
- 新测试文件：`tests/e2e/test_compound_literals.cpp`、`test_slice_literal.cpp`、
  `test_utf_prefix.cpp`、`test_operator_unary.cpp`、`test_operator_extended.cpp`、
  `test_out_of_class_method.cpp`、`test_non_type_param_value.cpp`；既有文件补
  f16 关键字/restrict。
- 全量 `cd build && cmake --build . -j$(nproc) && ctest -j8` 无失败；
  SpecConformance 12/12；工作区 clean。
- 文档同步在功能任务完成后收尾（Task 9 镜像第一轮）。

## 5. 任务划分（实施计划输入）

- **Task 1**：f16/f32/f64/f128 类型关键字（TYP-04）
- **Task 2**：指针后置 restrict（PAR-06）
- **Task 3**：u8/u16/u32 前缀字面量（TYP-05）
- **Task 4**：C99 复合字面量（PAR-08）
- **Task 5**：slice 字面量初始化（PAR-08）
- **Task 6**：类外方法定义（PAR-03）
- **Task 7**：一元 operator 重载调用点（SEM-17）
- **Task 8**：二元 operator 集合收敛（SEM-17）
- **Task 9**：operator[] / operator() 重载（SEM-17）
- **Task 10**：非类型模板参数作为值（GEN-02）
- **Task 11**：文档同步（grammar/keywords/semantics/TODO/Progress）

## 6. 风险与已知边界

- operator[]/() 的 CallExprAST calleeExpr 改造触及调用表达式主干，需谨慎保持
  字符串 callee 路径不动（既有 print/method 调用依赖）。
- 复合字面量禁转义检查需覆盖 `&`、return、全局初始化三处；slice 字面量同理；
  不追踪副本（记文档口径）。
- 前缀字面量逐字节存储不转码为已知简化（用户已认可）。
- 后缀 `operator++` 返回旧值语义由实现者负责（不自动快照），记文档。
- restrict 仅语法接受，无别名分析（MEM-04 挂后）。
- operator 调用点沿用 raw-type mangle（既有模式潜在 mismatch，如实镜像不修）。
- `foo<N>()` 模板实参位置延后；模板体内局部遮蔽值参数诊断拒绝。
- 一元/`[]`/`()` operator 查找触发类型复用 `isOverloadOperand`（含 Optional/Result）。


