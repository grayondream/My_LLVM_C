# INH 继承链 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 补齐单继承：struct 形态、INH-02 多继承诊断、非公有继承诊断、方案乙（沿链查找）机制，并把已工作行为钉进测试。

**Architecture:** Parser 两处继承子句 + 诊断；sema 删基类方法表复制、`resolveMethod` 沿 `base` 链查找并附带 definingClass；`StructType` 补 base 成员，字段访问沿链扩展到 struct；codegen struct 基类首字段 + GEP walk 泛化。访问级别判定式本轮零改动（DS4）。

**Tech Stack:** C++20、LLVM、gtest/ctest。

**Spec:** `docs/superpowers/specs/2026-10-05-inheritance-design.md`（DS1–DS5 冻结裁决在 §3）

## Global Constraints

- 诊断码从 E2xxx 注册表取下一可用值（INF-13 惯例；实施时查 `docs/spec/` 或既有码表确定具体值）。
- 测试名一律 `INH` 前缀（TypeContext 全局单例跨测试隔离，同 AGG-10/11 轮 `NT` 惯例）；e2e 用 `ClassCodegenE2E.INH*`。
- 测试基线 868；每任务结束全量 `ctest` 必须绿（计划内预期红除外，见各任务 Expected）。
- sema 侧与 codegen 侧改动须同一 commit（时序铁律，AGG-10 教训）；commit 前必须 `git status` 核对。
- `docs/superpowers/`、`Progress.md` 均 gitignore，提交须 `git add -f`。
- 方法体内成员访问须显式 `this->`（AGG-08 隐式 self 范围外，测试勿用裸成员名）。

## Review Focus

1. **struct 继承 × 匿名内联字段（AGG-03 promote）**：`struct D : B { union { int32 a; float32 b; }; int32 c; };` —— 期望 promote 与沿链字段访问共存不回归。pin：Task 2 `INHStructAnonPromote`（sema）。
2. **深链中间类不实例化**：`A→B→C` 只用 C，B 仅有声明参与继承 —— 期望 C 可用全部祖先成员。pin：Task 3 `INHDeepChainE2E`。
3. **同名方法不同签名（重载沿链）**：基 `f(int32)`、派生 `f(float64)`，派生实例按实参类型各自命中 —— 期望沿链查找不破坏既有重载第一阶段精确匹配。pin：Task 2 `INHOverloadChainSema`。
4. **循环继承与自继承诊断**：`class A : B; class B : A;` 与 `class S : S` —— 期望既有 `hasCircularInheritance` 诊断不回归且不挂死。pin：Task 2 `INHCircularSema`。
5. **前向声明的基类未定义**：`class B; class D : public B { ... };` B 无定义 —— 期望「基类不存在」诊断而非崩溃。pin：Task 2 `INHBaseUndefSema`。

---

### Task 1: Parser——struct 继承子句 + INH-02/说明符诊断

**Files:**
- Modify: `src/frontend/Parser.cpp`（`parseStructDecl` :2361 起——继承子句；`parseClassDecl` :2519 起——说明符诊断 + `,` 诊断）
- Test: `tests/sema/test_semantic_analyzer.cpp`（文件末尾追加；parse 级断言用显式管线：`Lexer/Parser/parser.getErrors()`）

**Interfaces:**
- Consumes: `parseClassDecl` 既有继承子句代码（`: [specifier] BaseName`，`qualifyTypeDeclName` 处理基类名）；`error(...)` 诊断发射。
- Produces: `parseStructDecl` 产出的 `StructDeclAST::baseClass`（string，已 `qualifyTypeDeclName`，Task 2 消费）；两处解析器的诊断进入 `parser.getErrors()`（Task 2/3 的诊断测试消费）。

- [ ] **Step 1: 写失败测试（sema 文件末尾，6 项）**

```cpp
// ========== INH: inheritance — parser ==========
// 帮助函数沿用文件内既有显式管线模式；断言 parser 诊断用 parser.getErrors()。

TEST(SliceSemTest, INHStructInheritParses) {
    // 解析级：struct 继承子句被接受且 baseClass 记录，后续成员不丢。
    Lexer lexer("test.c",
        "struct B1 { int32 x; }; struct D1 : public B1 { int32 y; };");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_TRUE(parser.getErrors().empty());
    // 第二个声明是 D1，baseClass == "B1"，fields 含 y。
    ASSERT_GE(ast->declarations.size(), 2u);
    auto* d1 = dynamic_cast<StructDeclAST*>(ast->declarations[1].get());
    ASSERT_NE(d1, nullptr);
    EXPECT_EQ(d1->baseClass, "B1");
    ASSERT_EQ(d1->fields.size(), 1u);
    EXPECT_EQ(d1->fields[0].first, "y");
}

TEST(SliceSemTest, INHMultiInheritRejected) {
    // INH-02：多继承给明确诊断（今天静默误解析为前向声明）。
    Lexer lexer("test.c",
        "class A2 { public: int32 x; }; class B2 { public: int32 y; }; "
        "class C2 : public A2, public B2 { public: int32 z; };");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    EXPECT_FALSE(parser.getErrors().empty());
}

TEST(SliceSemTest, INHPrivateInheritRejected) {
    // DS2：private/protected 继承诊断不支持（struct 与 class 两侧）。
    Lexer lexer("test.c",
        "class B3 { public: int32 x; }; class D3 : private B3 { public: int32 y; };");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    EXPECT_FALSE(parser.getErrors().empty());
}

TEST(SliceSemTest, INHProtectedInheritRejected) {
    Lexer lexer("test.c",
        "struct B4 { int32 x; }; struct D4 : protected B4 { int32 y; };");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    EXPECT_FALSE(parser.getErrors().empty());
}

TEST(SliceSemTest, INHClassBareInheritPins) {
    // DS2：class 缺省说明符 = public，不诊断（用户裁决，偏离 C++ 记文档）。
    // 端到端 pin（class 继承骨架今天已通，预期 Task 1 期即 PASS）。
    EXPECT_TRUE(analyzeOk(
        "class Base5 { public: int32 x; }; class D5 : Base5 { public: int32 y; }; "
        "int32 main() { D5 d; d.x = 1; d.y = 2; "
        "return (d.x == 1 && d.y == 2) ? 0 : 1; }"));
}

TEST(SliceSemTest, INHStructBareInheritParses) {
    // struct 缺省说明符同样 = public。
    Lexer lexer("test.c",
        "struct B6 { int32 x; }; struct D6 : B6 { int32 y; };");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_TRUE(parser.getErrors().empty());
    auto* d6 = dynamic_cast<StructDeclAST*>(ast->declarations[1].get());
    ASSERT_NE(d6, nullptr);
    EXPECT_EQ(d6->baseClass, "B6");
}
```

- [ ] **Step 2: 运行确认失败形态**

Run: `./bin/compiler_tests --gtest_filter='SliceSemTest.INH*'`
Expected RED：`INHStructInheritParses`、`INHMultiInheritRejected`、`INHPrivateInheritRejected`、`INHProtectedInheritRejected`、`INHStructBareInheritParses` FAIL；`INHClassBareInheritPins` 预期 PASS（pin）。记录偏差。

- [ ] **Step 3: 实现**

1. `parseStructDecl`（`qualifyTypeDeclName(bareName)` 之后、`check(LBRACE)` 之前）插入继承子句，逻辑镜像 `parseClassDecl`：`match(TOKEN_COLON)` 后——说明符 lexeme 为 `private`/`protected` → `error("private/protected inheritance is not supported; use public inheritance", *peek())` 并 `return nullptr`；`public` 或缺省均放行；`baseClass = qualifyTypeDeclName(parseQualifiedTypeName())`；随后 `check(TOKEN_COMMA)` → `error("multiple inheritance is not supported; use single inheritance", *peek())` 并 `return nullptr`。
2. `parseClassDecl` 既有子句：说明符 lexeme 为 `private`/`protected` → 同款诊断 + `return nullptr`（缺省 `public`/缺省放行不变）；基类名后 `check(TOKEN_COMMA)` → 同款多继承诊断 + `return nullptr`。
3. 两处 `return nullptr` 前若已在 `m_typeNamespacePrefix` 压栈区（本任务位置在压栈之前，无需恢复；逐一核对插入点在 `advance(); // consume '{'` 之前）。诊断经 `Parser::error(msg, token)` 发射——与既有 parser 诊断一致不带 E 码；E2xxx 注册统一归 INF-13 诊断格式轮（spec §6 的「取下一可用 E2xxx」在该轮兑现）。

- [ ] **Step 4: 运行测试确认通过**

Run: `./bin/compiler_tests --gtest_filter='SliceSemTest.INH*'`
Expected: 6/6 PASS。

- [ ] **Step 5: 全量回归 + 提交**

Run: `ctest`（注意 `INHStructInheritParses` 等 parse 级测试不触 sema/codegen，全量预期绿；`EnumUnderlyingE2E.Int8EnumNegativeValue` 为已知 flaky——SEGFAULT 时隔离重跑验证后记录）。

```bash
git add src/frontend/Parser.cpp tests/sema/test_semantic_analyzer.cpp
git commit -m "feat(INH): parser 继承子句——struct 形态 + INH-02 多继承诊断 + 非公有继承诊断"
```

---

### Task 2: Sema——方案乙（删复制、沿链查找）+ StructType base + struct 字段沿链

**Files:**
- Modify: `src/ast/Type.h`（`StructType` 补 `baseClass`/`base`，镜像 `ClassType` :160 附近）
- Modify: `src/sema/SemanticAnalyzer.h`（`resolveMethod` 签名加默认 out-param；私有 helper `derivesFrom` **不需要**——DS4 判定式零改动）
- Modify: `src/sema/SemanticAnalyzer.cpp`（class 分支删基类方法复制 :1914 区；`resolveMethod` :2323 沿链；方法 E2009 检查点 :1553 区改用 definingClass；struct 分支 :2005 区基类接线；struct 字段沿链 :1461 区）
- Test: `tests/sema/test_semantic_analyzer.cpp`

**Interfaces:**
- Consumes: Task 1 的 `StructDeclAST::baseClass`；既有 `hasCircularInheritance`、`ClassType::baseClass/base`（Type.h :160）。
- Produces: `StructType::baseClass`（string）/`StructType::base`（Type*）——Task 3 的 codegen 消费；`resolveMethod(ClassType*, const std::string&, const std::vector<Type*>&, ClassType** defining = nullptr)`——defining 为命中定义类（沿链后非空时指向基类）；struct 字段访问沿链（Task 3 GEP 行为对齐的 sema 语义）。

- [ ] **Step 1: 写失败测试（sema 文件末尾，6 项）**

```cpp
// ========== INH: inheritance — sema（方案乙） ==========

TEST(SliceSemTest, INHMethodChainSema) {
    // 沿链方法调用（不依赖方法表复制；Task 1 后 struct/class 解析已通）。
    EXPECT_TRUE(analyzeOk(
        "class B7 { public: int32 f(int32 v) { return v; } }; "
        "class D7 : public B7 { public: int32 y; }; "
        "int32 main() { D7 d; return d.f(5) == 5 ? 0 : 1; }"));
}

TEST(SliceSemTest, INHPrivateBaseMethodRejected) {
    // 方案乙行为收紧 pin：private 基方法经派生实例从类外调用 → E2009。
    // 今天（复制机制）级别丢失放行 → RED；沿链后定义类级别生效 → GREEN。
    EXPECT_FALSE(analyzeOk(
        "class B8 { private: int32 m() { return 1; } }; "
        "class D8 : public B8 { public: int32 z; }; "
        "int32 main() { D8 d; return d.m(); }"));
}

TEST(SliceSemTest, INHBaseStaticProtectedPin) {
    // AGG-10 挂账现状 pin：Base::ps 从派生访问维持拒绝（DS4 不放宽）。
    // 今天即拒绝 → 预期 Task 2 期 PASS（防将来误放宽）。
    EXPECT_FALSE(analyzeOk(
        "class Base9 { protected: static int32 ps = 7; }; "
        "class D9 : public Base9 { public: int32 get() { return Base9::ps; } }; "
        "int32 main() { D9 d; return d.get(); }"));
}

TEST(SliceSemTest, INHStructFieldChainSema) {
    // struct 字段沿链（Type.h StructType::base + sema walk）。
    EXPECT_TRUE(analyzeOk(
        "struct B10 { int32 x; }; struct D10 : public B10 { int32 y; }; "
        "int32 main() { D10 d; d.x = 1; d.y = 2; "
        "return (d.x == 1 && d.y == 2) ? 0 : 1; }"));
}

TEST(SliceSemTest, INHBaseUndefSema) {
    // Review Focus 5：基类未定义 → 诊断而非崩溃。
    EXPECT_FALSE(analyzeOk(
        "class Undef11; class D11 : public Undef11 { public: int32 y; }; "
        "int32 main() { D11 d; d.y = 1; return d.y; }"));
}

TEST(SliceSemTest, INHOverloadChainSema) {
    // Review Focus 3：同名方法不同签名沿链——基 f(int32)、派生 f(float64)，
    // 按实参各自命中（沿链查找不破坏重载第一阶段精确匹配）。
    EXPECT_TRUE(analyzeOk(
        "class B13 { public: int32 f(int32 v) { return v; } }; "
        "class D13 : public B13 { public: float64 f(float64 v) { return v; } }; "
        "int32 main() { D13 d; return (d.f(5) == 5 && d.f(0.5) == 0.5) ? 0 : 1; }"));
}

TEST(SliceSemTest, INHStructAnonPromote) {
    // Review Focus 1：struct 继承 × 匿名内联字段（AGG-03 promote）共存。
    EXPECT_TRUE(analyzeOk(
        "struct B14 { int32 x; }; "
        "struct D14 : public B14 { union { int32 a; float32 b; }; int32 c; }; "
        "int32 main() { D14 d; d.x = 1; d.a = 2; d.c = 3; "
        "return (d.x == 1 && d.a == 2 && d.c == 3) ? 0 : 1; }"));
}

TEST(SliceSemTest, INHCircularSema) {
    // Review Focus 4：循环继承诊断不回归、不挂死。
    EXPECT_FALSE(analyzeOk(
        "class CA12 : public CB12 { public: int32 x; }; "
        "class CB12 : public CA12 { public: int32 y; }; "
        "int32 main() { return 0; }"));
}
```

- [ ] **Step 2: 运行确认失败形态**

Run: `./bin/compiler_tests --gtest_filter='SliceSemTest.INHMethodChainSema:SliceSemTest.INHPrivateBaseMethodRejected:SliceSemTest.INHStructFieldChainSema:SliceSemTest.INHBaseUndefSema:SliceSemTest.INHCircularSema:SliceSemTest.INHBaseStaticProtectedPin:SliceSemTest.INHOverloadChainSema:SliceSemTest.INHStructAnonPromote'`
Expected RED：`INHPrivateBaseMethodRejected`（今天放行）、`INHStructFieldChainSema`、`INHStructAnonPromote`（struct 无 base）、`INHBaseUndefSema`/`INHCircularSema`（分支行为差异所致，以实测为准）FAIL；`INHMethodChainSema`（复制机制今天可用）、`INHOverloadChainSema`、`INHBaseStaticProtectedPin`（今天已拒）预期 PASS（pin）。记录偏差。

- [ ] **Step 3: 实现**

1. `Type.h` `StructType`：加 `std::string baseClass;` 与 `Type* base{};`（镜像 `ClassType`）。
2. `SemanticAnalyzer.cpp` class 分支：**删除**「把 `baseType->methods` 逐个 `classType->addMethod`」的复制循环（基类方法表复制段）；自身方法注册循环保留。
3. `resolveMethod`（:2323）：签名加第 4 参 `ClassType** defining = nullptr`；本类表未命中时沿 `classType->base` 上溯逐类查找（每跳同样做 Phase 1 精确匹配 + Phase 2 衰减转换，沿链只取**第一个**命中定义类）；命中时 `if (defining) *defining = 该类`。调用点 :1091（to_string）不动（默认参数）；:1551 调用点取 defining。
4. 方法 E2009 检查点（`visit(MemberCallExprAST)` :1558 区）：`classType->memberAccessLevel(...)` 改为 `defining->memberAccessLevel(...)`；判定式保持 `currentClass != defining`（DS4）；消息 `of class '...'` 用 `defining->name`。
5. struct 分支（:2005 区）：`node.baseClass` 非空时——`typeCtx->getStruct(baseClass)` 查找（不存在 → 既有「base class not found」同款诊断）；`hasCircularInheritance` 检查；`structType->baseClass = node.baseClass; structType->base = baseType;`。
6. struct 字段沿链（:1461 区 struct 分支）：线性扫描未命中且 `structType->base` 非空时沿链上溯（镜像 class 分支 walk），命中即返回；未命中报既有 `no member named ... in struct '...'`（报首层名）。

- [ ] **Step 4: 运行测试确认通过**

Run: `./bin/compiler_tests --gtest_filter='SliceSemTest.INH*'`
Expected: 14/14 PASS（Task 1 的 6 项 + 本任务 8 项）。

- [ ] **Step 5: 全量回归 + 提交**

Run: `ctest`（AGG-10 静态方法后向引用、既有方法重载、E2009 全家为高风险回归区，逐一核对失败名单）。

```bash
git add src/ast/Type.h src/sema/SemanticAnalyzer.h src/sema/SemanticAnalyzer.cpp tests/sema/test_semantic_analyzer.cpp
git commit -m "feat(INH): 方案乙——sema 沿链方法查找（删复制）+ StructType base + struct 字段沿链"
```

---

### Task 3: Codegen——struct 基类首字段 + GEP walk 泛化 + e2e

**Files:**
- Modify: `src/codegen/CodegenContext.cpp`（`getLLVMType` Struct case :155 区——基类首字段，镜像 Class case :279 区）
- Modify: `src/ast/Expr.cpp`（`emitClassFieldGEP` :897——签名 `ClassType*` → `Type*` 泛化，walk 内按 link 分派 ClassType/StructType；`MemberAccessExprAST::codegen` Struct 分支 :949 区改为先走 GEP walk）
- Test: `tests/e2e/test_class_codegen.cpp`

**Interfaces:**
- Consumes: Task 2 的 `StructType::baseClass/base`；Task 1 的解析。
- Produces: struct 继承端到端可执行；`emitClassFieldGEP(CodegenContext&, Type*, llvm::Value*, const std::string&)`（泛化签名，Class 分支两个既有调用点 :991/:1018 同步改传参）。

- [ ] **Step 1: 写失败测试（e2e 文件末尾，8 项）**

```cpp
// ========== INH: inheritance e2e ==========

TEST_F(ClassCodegenE2E, INHStructE2E) {
    EXPECT_EQ(runSource(R"(
        struct B20 { int32 x; };
        struct D20 : public B20 { int32 y; };
        int32 main() { D20 d; d.x = 3; d.y = 4; return (d.x == 3 && d.y == 4) ? 0 : 1; }
    )", "test_inh_struct.c"), 0);
}

TEST_F(ClassCodegenE2E, INHStructLayoutOffset0E2E) {
    // INH-03：基类子对象偏移 0——派生指针隐式转基指针后字段同址。
    EXPECT_EQ(runSource(R"(
        struct B21 { int32 x; };
        struct D21 : public B21 { int32 y; };
        int32 main() { D21 d; d.x = 8; B21* b = &d; return b->x == 8 ? 0 : 1; }
    )", "test_inh_layout.c"), 0);
}

TEST_F(ClassCodegenE2E, INHDeepChainE2E) {
    // Review Focus 2：A→B→C 只用 C。
    EXPECT_EQ(runSource(R"(
        class A22 { public: int32 a; };
        class B22 : public A22 { public: int32 b; };
        class C22 : public B22 { public: int32 c; };
        int32 main() { C22 o; o.a = 1; o.b = 2; o.c = 3;
            return (o.a == 1 && o.b == 2 && o.c == 3) ? 0 : 1; }
    )", "test_inh_deep.c"), 0);
}

TEST_F(ClassCodegenE2E, INHShadowE2E) {
    // 字段遮蔽 pin（p5 行为）。遮蔽语义由 sema 名称解析保证（Task 2 已钉）。
    // `a->v`（经基指针读）读到 A23 子对象槽，与 `d.v` 是否同址取决于布局——
    // 以实测裁决：若 `a->v` 为未定义值则删该断言只留 `d.v == 4`，台账记录。
    EXPECT_EQ(runSource(R"(
        class A23 { public: int32 v; };
        class D23 : public A23 { public: int32 v; };
        int32 main() { D23 d; d.v = 4; A23* a = &d;
            return (d.v == 4 && a->v == 4) ? 0 : 1; }
    )", "test_inh_shadow.c"), 0);
}

```cpp
TEST_F(ClassCodegenE2E, INHSliceE2E) {
    // 派生→基 值赋值（切片语义）pin（p6 行为）。
    EXPECT_EQ(runSource(R"(
        class A24 { public: int32 x; int32 w; };
        class D24 : public A24 { public: int32 t; };
        int32 main() { D24 d; d.x = 3; d.w = 5; d.t = 9; A24 a = d;
            a.x = 6; return (a.x == 6 && a.w == 5 && d.x == 3) ? 0 : 1; }
    )", "test_inh_slice.c"), 0);
}

TEST_F(ClassCodegenE2E, INHDowncastE2E) {
    // static_cast 向下转换 pin（p7 行为）。
    EXPECT_EQ(runSource(R"(
        class A25 { public: int32 x; };
        class D25 : public A25 { public: int32 t; };
        int32 main() { D25 d; A25* a = &d; D25* d2 = static_cast<D25*>(a);
            d2->t = 2; return (d.t == 2 && a->x == 0) ? 0 : 1; }
    )", "test_inh_downcast.c"), 0);
}

TEST_F(ClassCodegenE2E, INHMultiInheritDiagE2E) {
    // INH-02 端到端：多继承 → runSource 返回非 0（诊断路径）。
    EXPECT_NE(runSource(R"(
        class A26 { public: int32 x; };
        class B26 { public: int32 y; };
        class C26 : public A26, public B26 { public: int32 z; };
        int32 main() { return 0; }
    )", "test_inh_multi.c"), 0);
}
```

- [ ] **Step 2: 运行确认失败形态**

Run: `./bin/compiler_tests --gtest_filter='ClassCodegenE2E.INH*'`
Expected RED：`INHStructE2E`、`INHStructLayoutOffset0E2E`、`INHDeepChainE2E`（struct 布局无 base / GEP 无 struct walk）、`INHMultiInheritDiagE2E`（诊断在 Task 1 已生效——若该测试 FAIL 说明 runSource 对诊断返回 0，以实测为准调整断言）；`INHShadowE2E`、`INHSliceE2E`、`INHDowncastE2E` 为 pin 预期 PASS（`INHShadowE2E` 的 `a->v` 断言以实测裁决，见注）。记录偏差。

- [ ] **Step 3: 实现**

1. `CodegenContext.cpp` `getLLVMType` Struct case：`fieldTypes` 组装前，镜像 Class case——`if (!st->baseClass.empty())` 且 `llvm::StructType::getTypeByName(st->baseClass)` 存在则 `push_back` 为首字段。
2. `Expr.cpp` `emitClassFieldGEP`：签名改 `Type* aggType`；walk 循环内按 `cur->kind` 分派——ClassType 走既有逻辑（fields/baseClass/base），StructType 同构（fields/baseClass/base，均 Task 2 已有）；GEP 的 `idx += 1`（基类占槽 0）两分支一致。
3. `MemberAccessExprAST::codegen` Struct 分支（:949 区）：线性扫描前先 `if (auto* gep = emitClassFieldGEP(ctx, valType, objVal, memberName)) return gep;`（valType 为 StructType 时 walk 命中含基类字段；未命中回落既有本类扫描）。
4. Class 分支两个既有调用点（:991/:1018）改传 `Type*`（类型已是 ClassType 的基类指针，直接传）。

- [ ] **Step 4: 运行测试确认通过**

Run: `./bin/compiler_tests --gtest_filter='ClassCodegenE2E.INH*'`
Expected: 8/8 PASS（`INHShadowE2E` 按实测裁决后的最终形态）。

- [ ] **Step 5: 全量回归 + 提交**

Run: `ctest`（union GEP、匿名 promote、Slice `.len`、既有 class 方法调用为高风险回归区）。

```bash
git add src/codegen/CodegenContext.cpp src/ast/Expr.cpp tests/e2e/test_class_codegen.cpp
git commit -m "feat(INH): codegen struct 基类首字段 + GEP walk 泛化 + e2e 钉测试"
```

---

### Task 4: 文档 + 收尾 + 整分支评审

**Files:**
- Modify: `TODO.md`（INH-01/02/03/04/06 勾记；INH-05 注明随 GEN；AGG-10/AGG-11 挂账改记）
- Modify: `Progress.md`（追加本任务记录，`git add -f`）
- 评审材料: `.superpowers/sdd/2026-10-05-inheritance/`

- [ ] **Step 1: TODO.md 勾记**

- INH-01/02/03/04/06 → `[x]`，完成记录含 spec 路径、方案乙一句、DS2 偏离 C++ 说明
- INH-05 → 保持 `[ ]`，注「随 GEN 模板系统另立一轮；本轮已覆盖 `static_cast` 向下转换（既有 PAR-18）」
- AGG-10 已知限制句改记：「随 INH-06 复查」→「INH-06 已落地（2026-10-05）；protected 放宽维持未做，如需另立项」
- AGG-11 同款挂账句同改
- INH-07 不动（Non-goals）

- [ ] **Step 2: Progress.md 追加（`git add -f`）**

时间戳条目：完成事项/文件路径/验证（全量 868→890）/提交列表/遗留。

- [ ] **Step 3: 提交文档**

```bash
git add TODO.md && git add -f Progress.md
git commit -m "docs: INH 继承链完成记录（TODO INH-01/02/03/04/06 收口；挂账改记）"
```

- [ ] **Step 4: 整分支评审（TDD 修复轮）**

`review-package docs/superpowers/plans/2026-10-05-inheritance.md <BASE> HEAD` → subagent 评审（对照 spec DS1–DS5 + Review Focus 五条）→ 按 TDD 修复 Critical/Important（RED 先行）→ 全量绿 → 最终汇报（Rulings + Deferred minors）→ 删除 sdd 工作区。
