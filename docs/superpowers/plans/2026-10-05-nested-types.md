# AGG-11 嵌套类型 + 前向声明 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 类/struct/union 体内可声明嵌套类型（enum/struct/class/union，任意深度），`Outer::Inner` 类型引用、`Outer::Red` 常量引用、嵌套类 static 成员可用，private 嵌套类型访问控制生效，前向声明语义钉住。

**Architecture:** 方案 A——解析期类前缀复用（类体解析压 `m_typeNamespacePrefix`，与 namespace 同构）；嵌套声明收集进 `StructDeclAST::nestedTypes`；sema 对称 `classPathPrefix` 推广 AGG-10 static 去糖公式；codegen 递归遍历。

**Tech Stack:** C++20 / LLVM / gtest（既有管线，无新依赖）。

**Spec:** `docs/superpowers/specs/2026-10-05-nested-types-design.md`（DS1–DS6；本 plan 论证自 spec）

## Global Constraints

- 基线 **840/840**（`6577c90`）；每任务结束全量 `ctest` 必须绿。
- TDD：RED 先行，未亲见失败不写实现；测试名 sema 用 `NT` 前缀（TypeContext 全局单例跨测试泄漏），e2e 用 `ClassCodegenE2E.NT*`。
- R5 Native：in-place master 执行；ledger 目录 `.superpowers/sdd/2026-10-05-nested-types/`。
- `Progress.md`、`docs/superpowers/` 均被 gitignore，提交需 `git add -f`。
- 记录一律中文；代码/命令/路径/标识符原样。
- 范围外（spec §6）：类体内 unqualified 引用（`Inner`/`Red` 须全限定）、签名/返回类型级 private 检查、typedef 嵌套、模板化、protected 细分（INH-06）。

## Review Focus

1. **三路判定回归**（最可能咬人）：成员位置 `struct { ... } x;` 匿名内联（AGG-03）、`enum Color c;`/`struct P p;`/`struct P p[3];` 字段不得被误路由——既有匿名聚合测试全量回归 + Task 1 `NTFieldOfNestedType` 钉字段形态。
2. **前向声明+定义**（codegen 提前 return 分支）：类型已注册时 `nestedTypes` 仍须生成，否则嵌套成员无存储——Task 3 `NTFwdWithNestedE2E` 钉住。
3. **嵌套类 static 双侧符号一致**（ns × 类嵌套组合，`ntns::NTN::S::v` 拍平 `ntns_NTN_S_v`）——Task 2 `NTStaticInNestedNsClass` 钉住。
4. **嵌套枚举常量不泄漏全局**（类外裸名 `Red` 不可用）——Task 2 `NTNestedEnumConstantNotGlobal` 钉住。
5. **private 嵌套类型经 cast 逃逸**（`(Outer::Secret*)malloc(...)`）——Task 2 `NTCastPrivateNestedRejected` 钉住。

---

### Task 1: Parser——嵌套声明识别 + 类前缀压栈 + 解析期注册

**Files:**
- Modify: `src/ast/Decl.h`（StructDeclAST、UnionDeclAST 加 nestedTypes）
- Modify: `src/frontend/Parser.h`（新私有方法声明）
- Modify: `src/frontend/Parser.cpp`（parseStructDecl :2344 起、parseClassDecl :2413 起、parseUnionDecl :2613 起成员循环；新 helper）
- Test: `tests/sema/test_semantic_analyzer.cpp`（文件末尾追加；`analyzeOk` helper :733）

**Interfaces:**
- Consumes: `parseEnumDecl()/parseUnionDecl()/parseStructDecl()/parseClassDecl()`（既有 decl 解析器，均消费尾部分号）；`m_typeNamespacePrefix`/`qualifyTypeDeclName`/`mangleQualifiedTypeName`（既有）；`bareName`（AGG-10，parseStructDecl/parseClassDecl 已记录；parseUnionDecl 本任务补记）。
- Produces: `StructDeclAST::nestedTypes` / `UnionDeclAST::nestedTypes`（`std::vector<std::unique_ptr<DeclAST>>`，Task 2/3 消费）；外层类 `memberAccess["type:<裸名>"]`（Task 2 消费）；解析期 TypeContext 注册（`Outer_Inner` 扁平键，全局依赖）。

- [ ] **Step 1: 写失败测试（sema，8 项）**

```cpp
// AGG-11: nested types — parser collects & registers under flat keys.

TEST(SliceSemTest, NTNestedStructTypeRef) {
    EXPECT_TRUE(analyzeOk(
        "class NTO { public: struct Inner { int32 x; }; }; "
        "int32 main() { NTO::Inner obj; obj.x = 3; return obj.x == 3 ? 0 : 1; }"));
}

TEST(SliceSemTest, NTEnumDoesNotEatFields) {
    // 现状缺陷：成员循环 parseType 消费内联 enum 后因无标识符 break，
    // 后续字段全部丢失。钉住修复。
    EXPECT_TRUE(analyzeOk(
        "class NTF { public: enum Color { Red, Green }; int32 x; }; "
        "int32 main() { NTF o; o.x = 1; return o.x == 1 ? 0 : 1; }"));
}

TEST(SliceSemTest, NTDeepNested) {
    EXPECT_TRUE(analyzeOk(
        "class NTD { public: struct A { struct B { int32 v; }; }; }; "
        "int32 main() { NTD::A::B obj; obj.v = 2; return obj.v == 2 ? 0 : 1; }"));
}

TEST(SliceSemTest, NTUnionNested) {
    EXPECT_TRUE(analyzeOk(
        "class NTU { public: union U { int32 a; float32 b; }; }; "
        "int32 main() { NTU::U u; u.a = 1; return u.a == 1 ? 0 : 1; }"));
}

TEST(SliceSemTest, NTFieldOfNestedType) {
    // 类体内字段用裸嵌套类型名（解析期前缀查找的自然结果）。
    EXPECT_TRUE(analyzeOk(
        "class NTFLD { public: struct Inner { int32 x; }; Inner field; }; "
        "int32 main() { NTFLD o; o.field.x = 5; return o.field.x == 5 ? 0 : 1; }"));
}

TEST(SliceSemTest, NTStructInStruct) {
    EXPECT_TRUE(analyzeOk(
        "struct NTOS { struct Inner { int32 x; }; }; "
        "int32 main() { NTOS::Inner obj; obj.x = 3; return obj.x == 3 ? 0 : 1; }"));
}

TEST(SliceSemTest, NTNestedClassMethod) {
    // 方法表在 sema 填充——Task 1 预期 RED，Task 2 转绿。
    EXPECT_TRUE(analyzeOk(
        "class NTM { public: struct S { int32 f(int32 x) { return x; } }; }; "
        "int32 main() { NTM::S obj; return obj.f(7) == 7 ? 0 : 1; }"));
}

TEST(SliceSemTest, NTForwardDeclPin) {
    // DS6：前向声明（含嵌套类前向）+ 定义 + 使用；枚举/union 前向解析可用。
    // 既有能力 pin——Task 1 期预期 PASS。
    EXPECT_TRUE(analyzeOk(
        "struct NF1 { int32 x; }; "
        "class NF2; class NF2 { public: int32 y; }; "
        "enum NF3 : uint8; union NF4; "
        "int32 main() { NF1 a; a.x = 1; NF2 b; b.y = 2; "
        "return (a.x == 1 && b.y == 2) ? 0 : 1; }"));
}
```

- [ ] **Step 2: 运行确认失败形态**

Run: `cd build && cmake --build . -j$(nproc) && ./bin/compiler_tests --gtest_filter='SliceSemTest.NT*'`
预期：全部 FAIL（吞成员/类型未注册）；记录 RED 分布偏差（若有）。已知预期偏差：`NTEnumDoesNotEatFields` 在现状下字段丢失 → FAIL ✓；无预期 PASS 项。

- [ ] **Step 3: 实现 parser 改动**

1. `Decl.h`：`StructDeclAST` 与 `UnionDeclAST` 各加
   `std::vector<std::unique_ptr<DeclAST>> nestedTypes;`（注释：AGG-11 嵌套类型声明，保序）。
2. `Parser.h` 加私有声明：`void registerNestedDeclType(DeclAST* decl);`
   实现（Parser.cpp，放 parseDeclaration 附近）：`dynamic_cast` 分派——
   `StructDeclAST`：`isClassDecl` → `getClass(name)` 空则 `new ClassType(name)`+`addField`+`addClass`；否则 `new StructType(name)`+`addField`+`addStruct`；
   `EnumDeclAST`：`values` 非空且 `getEnum(name)` 空时 `new EnumType(name)`+`addValue`+（保留 `underlyingType`）+`addEnum`；
   `UnionDeclAST`：`new UnionType(name)`+`addMember`+`addUnion`。
   （与 parseDeclaration 对应分支注册语义等价；executor 对照 parseDeclaration :1835-1960 校核。）
3. **三路判定**：parseClassDecl 与 parseStructDecl 与 parseUnionDecl 成员循环顶部、访问段/static 处理之前插入：

```cpp
TokenType kw = peek()->type;
if (kw == TokenType::TOKEN_ENUM || kw == TokenType::TOKEN_STRUCT ||
    kw == TokenType::TOKEN_UNION || kw == TokenType::TOKEN_CLASS) {
    size_t save = m_currentTokenPos;
    advance();
    std::string nestedName = parseQualifiedTypeName();
    bool nested = !nestedName.empty() &&
        (check(TokenType::TOKEN_LBRACE) ||
         (kw == TokenType::TOKEN_CLASS &&
          (check(TokenType::TOKEN_COLON) || check(TokenType::TOKEN_SEMICOLON))));
    m_currentTokenPos = save;
    if (nested) {
        std::unique_ptr<DeclAST> d;
        if (kw == TokenType::TOKEN_ENUM) d = parseEnumDecl();
        else if (kw == TokenType::TOKEN_UNION) d = parseUnionDecl();
        else if (kw == TokenType::TOKEN_CLASS) d = parseClassDecl();
        else d = parseStructDecl();
        if (!d) return nullptr;
        registerNestedDeclType(d.get());
        nestedTypes.push_back(std::move(d));
        continue;
    }
}
```

   要点：enum 仅 `IDENT+{` 路由（`enum Color c;` 是字段、`enum E : uint8;` 前向维持现状）；class 额外接受 `:`（继承）与 `;`（嵌套前向声明）；`struct {`/`union {` 匿名内联走原路径（AGG-03）。`class D c;`（IDENT 后非 `{/:/;`）走字段路径。成员循环局部需有 `nestedTypes` 容器，循环结束后搬入 decl（照 staticMembers 模式）。**嵌套类型访问级别**（仅 parseClassDecl 有 memberAccess）：路由分支内用检测阶段已解析出的 `nestedName`（源码中的名字，restore 前取得）记 `memberAccess["type:" + nestedName] = currentAccess;`。
4. **前缀压栈**：parseStructDecl/parseClassDecl/parseUnionDecl 确认进入体解析（`{` 之后）时：

```cpp
std::string savedPrefix = m_typeNamespacePrefix;
m_typeNamespacePrefix += mangleQualifiedTypeName(bareName) + "_";
```

   （bareName 为 `qualifyTypeDeclName` 前的原始名；parseUnionDecl 补记 `bareName`，照 parseStructDecl AGG-10 模式。）**所有** push 之后的 return 路径（成员解析失败的 `return nullptr`）先恢复 `m_typeNamespacePrefix = savedPrefix`——逐一枚举，不可遗漏（namespace 版无早退，class/struct 版有）。
5. `lookupNamedType`（:1447 起）核验裸名回退分支存在（前缀 miss 后 `tryKey(name)`）；若无则补（类体内引用全局类型不得因前缀破坏）。

- [ ] **Step 4: 运行验证**

Run: `./bin/compiler_tests --gtest_filter='SliceSemTest.NT*'`
预期：`NTNestedStructTypeRef/NTEnumDoesNotEatFields/NTDeepNested/NTUnionNested/NTFieldOfNestedType/NTStructInStruct/NTForwardDeclPin` PASS（末项为既有能力 pin）；`NTNestedClassMethod` FAIL（sema 未遍历，Task 2）。匿名聚合既有测试不回归（全量 ctest）。

- [ ] **Step 5: 全量 ctest + 提交**

```bash
cd build && ctest   # 840 + 8 = 848 附近，NTNestedClassMethod 外全绿
cd .. && git add src/ast/Decl.h src/frontend/Parser.h src/frontend/Parser.cpp tests/sema/test_semantic_analyzer.cpp
git commit -m "feat(AGG-11): parser 嵌套类型声明识别 + 类前缀压栈 + 解析期注册"
```

---

### Task 2: Sema——nestedTypes 遍历 + classPathPrefix + 枚举常量 + E2009

**Files:**
- Modify: `src/sema/SemanticAnalyzer.h`（classPathPrefix、nestedTypeAccess、visitNestedDecl 声明）
- Modify: `src/sema/SemanticAnalyzer.cpp`（visit(StructDeclAST) class/struct 分支、visit(UnionDeclAST)、visit(EnumDeclAST)、static 去糖两处、visit(VarDeclAST)、visit(CastExprAST)、新 helper）
- Test: `tests/sema/test_semantic_analyzer.cpp`

**Interfaces:**
- Consumes: Task 1 的 `nestedTypes`/`memberAccess["type:<名>"]`；AGG-10 的 `staticMemberIndex`/`checkStaticMemberAccess`/`mangleNamespaceName`/`scopedName`（:166 起语义：含 `::` 的名拍平、否则 namespacePrefix 走查）。
- Produces: `classPathPrefix`（裸类名累进，如 `"Outer_"`）；`nestedTypeAccess`（扁平类型名 → {外层 ClassType*, AccessLevel}）；E2009 消息 `"cannot access private type '<名>' of class '<类>' outside the class; make it public or add an accessor"`。

- [ ] **Step 1: 写失败测试（sema，8 项）**

```cpp
TEST(SliceSemTest, NTNestedEnumBare) {
    // 常量注册外层类作用域（DS4 修订）：Outer::Red；顶层 Mode::Red 不存在。
    EXPECT_TRUE(analyzeOk(
        "class NTE { public: enum Color { Red, Green }; int32 x; }; "
        "int32 main() { NTE::Color c = NTE::Red; NTE o; o.x = 1; "
        "return (o.x == 1 && c == 0) ? 0 : 1; }"));
}

TEST(SliceSemTest, NTNestedEnumConstantNotGlobal) {
    // Review Focus 4：类外裸名 Red 不可用（pin，预期 Task 1 期即 PASS）。
    EXPECT_FALSE(analyzeOk(
        "class NTG { public: enum Color { Red }; }; int32 main() { return Red; }"));
}

TEST(SliceSemTest, NTStaticInNestedClass) {
    EXPECT_TRUE(analyzeOk(
        "class NTS { public: struct S { static int32 v = 3; }; }; "
        "int32 main() { return NTS::S::v; }"));
}

TEST(SliceSemTest, NTStaticInNestedNsClass) {
    // Review Focus 3：ns × 类嵌套组合双侧符号一致。
    EXPECT_TRUE(analyzeOk(
        "namespace ntns { class NTN { public: struct S { static int32 v = 3; }; }; } "
        "int32 main() { return ntns::NTN::S::v; }"));
}

TEST(SliceSemTest, NTPrivateNestedTypeRejected) {
    Lexer lexer("test.c",
        "class NTP { private: struct Secret { int32 x; }; }; "
        "int32 main() { NTP::Secret s; return 0; }");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_TRUE(ast != nullptr);
    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    ASSERT_FALSE(analyzer.getErrors().empty());
    bool hasE2009 = false;
    for (auto& e : analyzer.getErrors())
        if (e.message.find("private") != std::string::npos) hasE2009 = true;
    EXPECT_TRUE(hasE2009);
}

TEST(SliceSemTest, NTPrivateNestedTypeInternalOk) {
    EXPECT_TRUE(analyzeOk(
        "class NTP2 { private: struct Secret { int32 x; }; "
        "public: int32 probe() { Secret s; s.x = 4; return s.x; } }; "
        "int32 main() { NTP2 o; return o.probe() == 4 ? 0 : 1; }"));
}

TEST(SliceSemTest, NTCastPrivateNestedRejected) {
    // Review Focus 5：cast 目标类型检查（malloc 堆用法主通道）。
    EXPECT_FALSE(analyzeOk(
        "class NTP3 { private: struct Secret { int32 x; }; }; "
        "int32 main() { return (NTP3::Secret*)0 ? 1 : 0; }"));
}

TEST(SliceSemTest, NTStaticNestedFactory) {
    // 嵌套类 static 方法 + 实例字段组合。注意：刻意避开 self-type
    // （类体内裸自引用类型解析期未注册是既有缺口，与 AGG-11 无关，
    // 顶层类同样存在——记录为限制）；限定名 `NTF2::S` 经前缀回退可解析。
    EXPECT_TRUE(analyzeOk(
        "class NTF2 { public: struct S { public: int32 x; "
        "static void fill(NTF2::S* s) { s->x = 9; } }; "
        "int32 probe() { NTF2::S s; NTF2::S::fill(&s); return s.x; } }; "
        "int32 main() { NTF2 o; return o.probe() == 9 ? 0 : 1; }"));
}
```

- [ ] **Step 2: 运行确认失败形态**

Run: `./bin/compiler_tests --gtest_filter='SliceSemTest.NTNestedEnumBare:SliceSemTest.NTStatic*:SliceSemTest.NTPrivate*:SliceSemTest.NTCast*:SliceSemTest.NTStaticNestedFactory'`
预期 RED：除 `NTNestedEnumConstantNotGlobal`（pin，Task 1 期已 PASS）外全 FAIL。记录偏差。

- [ ] **Step 3: 实现 sema 改动**

1. `SemanticAnalyzer.h`（`staticMemberIndex` 声明旁）：
```cpp
// AGG-11: flattened enclosing-class path for nested-type visits, e.g. "Outer_".
std::string classPathPrefix;
// AGG-11: flattened nested-type name -> {defining class, access level}.
std::unordered_map<std::string, AccessLevel> nestedTypeAccess;
void visitNestedDecl(DeclAST& node);
void checkNestedTypeAccess(Type* type, VarDeclAST& site);
void checkNestedTypeAccess(Type* type, CastExprAST& site);
```
   （两个重载各自调用既有 `emitError` 对应重载；实现体两行分派共享一个私有 helper 即可。）
2. `visitNestedDecl`（新）：`dynamic_cast` 分派 StructDeclAST/EnumDeclAST/UnionDeclAST → 对应 visit；其他类型忽略。
3. `visit(StructDeclAST)` **class 与 struct 两分支**：类型注册完成后、staticMembers 循环之前：
```cpp
for (auto& nested : node.nestedTypes) {
    if (!nested) continue;
    // DS5: record access level keyed by flattened name (owner = this class).
    nestedTypeAccess[nestedName] = classType->memberAccessLevel("type:" + bareNestedName);
    std::string saved = classPathPrefix;
    classPathPrefix += mangleNamespaceName(bareNestedName) + "_";
    visitNestedDecl(*nested);
    classPathPrefix = saved;
}
```
   （class 分支级别取自 memberAccess["type:..."]；struct 分支恒 Public。`nestedName` = 嵌套 decl 的 `node.name`（扁平键，如 `NTN_S`）；`bareNestedName` = 其 `bareName`（缺省时用 node.name）。）
4. `visit(UnionDeclAST)`：同样遍历 `nestedTypes`（union 无访问级别，级别恒 Public；压 classPathPrefix）。
5. **static 去糖推广**（class 分支 I2 预注册循环与 staticMembers 循环两处，:1860/:1900 附近）：
   `mangleNamespaceName(node.bareName.empty() ? node.name : node.bareName + ...)` 改为
   `mangleNamespaceName(classPathPrefix + bareNameOf(node) + "::" + origName)`，
   其中 `bareNameOf(node) = node.bareName.empty() ? node.name : node.bareName`。
   验证组：顶层 `""+"SMN"` 不变 ✓；`"Outer_"+"Inner"` ✓；ns 内 `"Outer_"+"S"`+scopedName ✓。
6. `visit(EnumDeclAST)` 常量注册（:1963）：
```cpp
std::string key = classPathPrefix.empty()
    ? scopedName(val.first)
    : scopedName(mangleNamespaceName(classPathPrefix + val.first));
enumConstants[key] = {enumType, val.second};
```
   （类嵌套时不注册裸键——Review Focus 4。）
7. `checkNestedTypeAccess` 实现：`stripTypedef` → 沿 Pointer/elementType 解引用 → `ClassType/StructType/EnumType/UnionType` 取 `name`（对照 Type.h 各类名字段）→ `nestedTypeAccess` 查找 → 非 Public 且 `currentClass != owner` 时 `emitError(DiagnosticCode::SemPrivateMemberAccess, "cannot access private type '" + name + "' of class '" + ownerName + "' outside the class; make it public or add an accessor", site)`。
8. 检查点挂接：`visit(VarDeclAST)`（解析 node.type 后；全局与局部同函数覆盖）与 `visit(CastExprAST)`（入口处查 `node.castType`）。

- [ ] **Step 4: 运行验证**

Run: `./bin/compiler_tests --gtest_filter='SliceSemTest.NT*'`
预期：16 项全 PASS；全量 ctest 856 附近（848 + 8）。

- [ ] **Step 5: 全量 ctest + 提交**

```bash
cd build && ctest
cd .. && git add src/sema/SemanticAnalyzer.h src/sema/SemanticAnalyzer.cpp tests/sema/test_semantic_analyzer.cpp
git commit -m "feat(AGG-11): sema 嵌套类型遍历 + classPathPrefix 去糖推广 + 枚举常量 + E2009"
```

---

### Task 3: Codegen——nestedTypes 递归生成 + e2e

**Files:**
- Modify: `src/ast/Decl.cpp`（StructDeclAST::codegen 两分支 :449/:470 起；UnionDeclAST::codegen :487 起）
- Test: `tests/e2e/test_class_codegen.cpp`

**Interfaces:**
- Consumes: Task 1/2 的 `nestedTypes`（DeclAST::codegen 为虚，直接 `nested->codegen(ctx)` 分派）；AGG-10 `emitStaticMembers`（时序规则：staticMembers 先于方法）。
- Produces: 嵌套类型全局符号 `Outer_Inner_method` / `Outer_Inner_v`（Task 4 文档化）。

- [ ] **Step 1: 写失败测试（e2e，7 项）**

```cpp
// AGG-11: nested types e2e.

TEST_F(ClassCodegenE2E, NTStructFieldE2E) {
    EXPECT_EQ(runSource(R"(
        class NTE1 { public: struct Inner { int32 x; }; };
        int32 main() {
            NTE1::Inner obj; obj.x = 3;
            return obj.x == 3 ? 0 : 1;
        }
    )", "test_nt_struct.c"), 0);
}

TEST_F(ClassCodegenE2E, NTEnumConstantE2E) {
    EXPECT_EQ(runSource(R"(
        class NTE2 { public: enum Color { Red, Green }; };
        int32 main() { NTE2::Color c = NTE2::Green; return c == 1 ? 0 : 1; }
    )", "test_nt_enum.c"), 0);
}

TEST_F(ClassCodegenE2E, NTStaticE2E) {
    EXPECT_EQ(runSource(R"(
        class NTE3 { public: struct S {
            private: static int32 count = 0;
            public: static int32 next() { NTE3::S::count = NTE3::S::count + 1; return NTE3::S::count; }
        }; };
        int32 main() { NTE3::S::next(); return NTE3::S::next() == 2 ? 0 : 1; }
    )", "test_nt_static.c"), 0);
}

TEST_F(ClassCodegenE2E, NTMethodE2E) {
    EXPECT_EQ(runSource(R"(
        class NTE4 { public: struct S { int32 f(int32 x) { return x; } }; };
        int32 main() { NTE4::S obj; return obj.f(7) == 7 ? 0 : 1; }
    )", "test_nt_method.c"), 0);
}

TEST_F(ClassCodegenE2E, NTDeepE2E) {
    EXPECT_EQ(runSource(R"(
        class NTE5 { public: struct A { struct B { int32 v; }; }; };
        int32 main() { NTE5::A::B obj; obj.v = 2; return obj.v == 2 ? 0 : 1; }
    )", "test_nt_deep.c"), 0);
}

TEST_F(ClassCodegenE2E, NTFwdWithNestedE2E) {
    // Review Focus 2：前向声明+定义时 nestedTypes 仍生成。
    EXPECT_EQ(runSource(R"(
        class NTE6;
        class NTE6 { public: struct Inner { int32 x; }; };
        int32 main() { NTE6::Inner obj; obj.x = 6; return obj.x == 6 ? 0 : 1; }
    )", "test_nt_fwd.c"), 0);
}

TEST_F(ClassCodegenE2E, NTSelfRefPtrE2E) {
    // 前向声明语义钉住（DS6）：自引用指针。
    EXPECT_EQ(runSource(R"(
        struct NFNode { int32 v; struct NFNode* next; };
        int32 main() {
            struct NFNode n; n.v = 1; n.next = 0;
            return n.v == 1 ? 0 : 1;
        }
    )", "test_nt_selfref.c"), 0);
}
```

- [ ] **Step 2: 运行确认失败形态**

Run: `./bin/compiler_tests --gtest_filter='ClassCodegenE2E.NT*'`
预期：除 `NTSelfRefPtrE2E`（既有能力 pin，可能 PASS）外全 FAIL；记录偏差。注意段错误 = 可接受 RED 形态（评审经验：编译器带病产出坏二进制）。

- [ ] **Step 3: 实现 codegen 改动**

1. `StructDeclAST::codegen` **两分支**（提前 return 分支与正常路径）在 `emitStaticMembers` **之前**插入：
```cpp
// AGG-11: nested types first — outer method bodies may reference their
// members (same ordering rule as sema; enums/typedefs are no-ops).
for (auto& nested : nestedTypes) {
    if (nested) nested->codegen(ctx);
}
```
2. `UnionDeclAST::codegen`（现为 no-op）加同一遍历。
- [ ] **Step 4: 运行验证**

Run: `./bin/compiler_tests --gtest_filter='ClassCodegenE2E.NT*'` → 7 项 PASS；全量 ctest 863 附近（856 + 7）。

- [ ] **Step 5: 提交**

```bash
cd .. && git add src/ast/Decl.cpp tests/e2e/test_class_codegen.cpp
git commit -m "feat(AGG-11): codegen 嵌套类型递归生成（两分支，先于 staticMembers/方法）"
```

---

### Task 4: 文档 + 收尾 + 整分支评审

**Files:**
- Modify: `docs/spec/abi.md`（MOD-15 §4 第 4 条后补第 5 条）
- Modify: `TODO.md`（AGG-11 `[x]`、PAR-04 完成收口、P1-01 `[x]`、AGG-07 提及行更新）
- Modify: `Progress.md`（gitignore，`git add -f`）

- [ ] **Step 1: abi.md MOD-15 补编码**

§4 第 4 条（static 成员）后加：

```markdown
5. **嵌套类型（AGG-11，2026-10-05）**：类/struct/union 体内声明的类型按
   `Outer::Inner` → `Outer_Inner` 扁平键注册（`mangleQualifiedTypeName` 变换，
   与 namespace 限定类型共用编码；namespace 组合 `ns::Outer::Inner` →
   `ns_Outer_Inner`）。其成员符号沿第 4 条规则以扁平类名为类路径。
```

- [ ] **Step 2: TODO.md 勾记**

- AGG-11 条目改 `[x]` 并附完成摘要（方案 A、DS1-DS6、嵌套枚举常量 `Outer::Red`、E2009 检查点 var decl+cast、Deferred minors）；同时记录既有缺口：**类体内裸自引用类型（self-type）解析期未注册**（顶层类同样存在，与嵌套无关，AGG-10 已知规避项）——是否单独立项由用户定。
- PAR-04「**待补**：嵌套类型（AGG-11）」→「四要素齐备（2026-10-05）」；条目改 `[x]`。
- P1-01 `[~]` → `[x]`，剩余清空；AGG-07 行「待 AGG-10/11」改为完成表述。

- [ ] **Step 3: Progress.md 追加 + 提交**

（时间、改动摘要、验证结果 862/840 基线对照、遗留。）

```bash
git add docs/spec/abi.md TODO.md && git add -f Progress.md
git commit -m "docs: AGG-11 嵌套类型完成记录"
```

- [ ] **Step 4: 整分支评审 + TDD 修复**

`review-package docs/superpowers/plans/2026-10-05-nested-types.md <spec-commit> HEAD` → subagent 评审（对照 spec DS1-DS6 与 Review Focus 五条；重点：三路判定回归面、前缀泄漏/恢复完整性、嵌套类 static 双侧一致）→ 按 TDD 修复 Critical/Important → 全量 ctest → 记 ledger → 最终汇报（Rulings + Deferred minors）。
