# INH 继承链 设计（INH-01/02/03/04/06）

日期：2026-10-05
状态：已获用户批准的设计（对话修订版）；本文档为实施权威。
前置：AGG-10/AGG-11 已完成（868/868）。class 继承骨架已通（探查矩阵见 §2）。

## 1. 背景与目标

TODO §9（INH）要求单继承支持。探查证实 class 路径的继承骨架（布局、方法查找、
转换、遮蔽）已端到端可用，缺的是：struct 形态、错误诊断（INH-02）、把已工作行为
钉进测试、以及方法表复制机制带来的访问级别归属瑕疵。

用户裁决的范围收缩：**只做简单的 public 继承；缺省一律 public（class 亦然）；
成员访问级别机制本轮零改动**——派生类访问基类 protected/private 成员维持现状
（拒绝，比 C++ 严格），AGG-10/AGG-11 的「随 INH-06 复查」挂账改记为
「INH-06 已落地，protected 放宽未做，如需另立项」。

**范围外**：INH-05（CRTP：`this` 关键字、模板基类类型化、延迟实例化、
`static_cast<Derived*>(this)` 中的 this 语义）随 GEN 模板系统另立一轮；
protected 语义放宽；虚函数/vtable/RTTI 永久 Non-goals（INH-07）。

## 2. 现状矩阵（探查实证，2026-10-05）

| 能力 | 现状 | 实证 |
|---|---|---|
| class 单继承 + 基类字段布局（偏移 0） | ✅ | p1 |
| 方法沿继承链查找 | ✅（经方法表复制） | p2 |
| 派生→基 指针隐式转换 | ✅ | p3 |
| 字段遮蔽（派生同名优先） | ✅ | p5 |
| 派生→基 值赋值（切片） | ✅ | p6 |
| `static_cast<Derived*>` 向下转换 | ✅ | p7 |
| 非虚静默遮蔽（基指针调基版本） | ✅ | p10 |
| **struct 单继承** | ❌ `parseStructDecl` 不解析 `: Base`，吞成员 | p11 |
| **多继承诊断（INH-02）** | ❌ `class C : A, B` 静默误解析为前向声明 | p4 |
| 方法体内裸成员名 | ❌ 需显式 `this->`（AGG-08 隐式 self，范围外） | p13 |

## 3. 核心裁决（对话冻结）

- **DS1 范围**：INH-01/02/03/04/06 一轮；INH-05 随 GEN；protected 放宽出局。
- **DS2 继承说明符**：缺省一律 public（struct/class 一致，bare `class D : B`
  不诊断——偏离 C++ 的 class 缺省 private 继承，记文档）；`: private B` /
  `: protected B` → 明确诊断不支持。
- **DS3 机制（方案乙，用户选定）**：删除 sema 的基类方法表复制循环，
  `resolveMethod` 沿 `classType->base` 链查找并附带 definingClass。
- **DS4 访问级别**：本轮零改动。Private/Protected 判定式保持
  `currentClass == definingClass` 原样；方法沿链后私有基方法在派生类外可达的
  洞顺带闭合（行为收紧，pin 住）。
- **DS5 多继承**：基类后遇 `,` → 明确诊断（INH-02）。

## 4. 设计

### 4.1 Parser（INH-01/02）

- `parseStructDecl` 补继承子句，与 `parseClassDecl` 同构：`:` 后可选
  `public` 说明符（仅 `public` 合法，`private`/`protected` 诊断不支持），
  然后 `parseQualifiedTypeName()` 取基类名经 `qualifyTypeDeclName`。
  `StructDeclAST::baseClass` 已存在，struct 直接复用。
- `parseClassDecl` 现状丢弃说明符 lexeme——改为：遇到 `private`/`protected`
  说明符即诊断；`public` 或缺省均放行（缺省 = public，DS2）。
- **INH-02**：两处解析器在基类名后遇 `,` → 诊断
  「multiple inheritance is not supported」（E2xxx 码，见 §6）。
- 所有早退路径恢复 `m_typeNamespacePrefix`（AGG-11 惯例，逐一核对）。

### 4.2 Sema（方案乙核心）

- **删除基类方法复制循环**：`visit(StructDeclAST)` class 分支中「把
  `baseType->methods` 复制进派生类方法表」的整段删除；派生类**自身**方法的
  注册循环（「Only add if not already present」）原样保留。
- **`resolveMethod` 沿链**：先查本类表；未命中沿 `base` 链上溯（循环继承由
  既有 `hasCircularInheritance` 拦截，链长上限 = 深度自然终止）。返回值附带
  definingClass（内部小结构 `{Symbol*, ClassType* defining}`，不改 Symbol 公共布局）。
- **方法 E2009 检查点**（`visit(MemberCallExprAST)` 内）：级别查
  `definingClass->memberAccessLevel(...)`，归属消息用 definingClass 名；
  判定式保持 `currentClass == definingClass`（DS4，不放宽）。
- **字段 E2009 检查点**（字段沿链 walk 处）：归属/级别已按定义类，判定式不动。
- **static E2009 检查点**（`checkStaticMemberAccess`）：definingClass 已是真实
  定义类，判定式不动。`Base::ps` 从派生类访问维持拒绝（pin 测试钉住现状）。
- **struct 分支**：基类存在性检查 + `hasCircularInheritance`（复用 class 分支
  逻辑）；`StructType` 设 `baseClass`/`base`。

### 4.3 Type.h 与 Codegen（struct 继承）

- `StructType` 补 `baseClass`（string）与 `base`（Type*），镜像 `ClassType`。
- sema `visit(MemberAccess)` 的字段沿链 walk 目前仅走 ClassType——扩展：
  StructType 同样沿 `base` 上溯。
- `emitClassFieldGEP`（Expr.cpp）：walk 循环扩展支持 StructType 链
  （基类子对象同样占字段槽 0）。
- `StructDeclAST::codegen`：struct 主路径（非 isClass）补基类首字段
  （fieldTypes 头部插入基类 llvm 类型）；既有类型提前 return 分支无需改动
  （类型已存在）。前向声明 + 定义组合由既有机制覆盖。
- 成员调用 codegen：`resolvedParamTypes` 已按定义点编码；既有沿链回退
  （resolvedParamTypes 为空时）成为死路径，保留作防御。

## 5. 测试计划（TDD，基线 868 → 预期 ~883）

### e2e（`tests/e2e/test_class_codegen.cpp`，ClassCodegenE2E.INH*，~9 项）

1. struct 单继承：字段读写、布局（经基指针验证偏移 0）
2. class 继承回归 pin：基字段/方法沿链/派生→基指针/切片/向下转换/静默遮蔽
3. 深链 A→B→C（两层派生）
4. 多继承 `,` → 编译失败诊断
5. `: private B` → 诊断
6. `class D : B`（bare，缺省 public）→ 可用
7. [[repr(C)]] 布局探针：基子对象偏移 0（指针差为 0）——repr 注解未落地，
   仅以普通 struct 钉布局，注解子句留待 ANN

### sema（`tests/sema/test_semantic_analyzer.cpp`，SliceSemTest.INH*，~6 项）

1. 方法沿链：派生实例调用基方法（含参数），不依赖复制
2. 私有洞闭合：private 基方法在派生类外调用 → E2009（方案乙行为收紧 pin）
3. `Base::ps` 从派生访问维持拒绝（现状 pin，防将来误放宽）
4. 多继承 / private 继承 / class-bare 继承的诊断断言（class-bare 断言无诊断）
5. struct 继承字段沿链（sema 层）
6. 循环继承诊断回归（`class A : B; class B : A;` 形态）

## 6. 诊断（E2xxx 注册表）

- 多继承：`multiple inheritance is not supported; use single inheritance`
- 非公有继承：`private/protected inheritance is not supported; use public inheritance`
- 基类不存在 / 循环继承：复用既有诊断

码值分配遵循 INF-13 注册表现状（实施时取下一可用 E2xxx）。

## 7. 已知限制（记录，不阻塞）

- protected 成员：派生类不可访问（比 C++ 严格；放宽另立项）
- 方法体内裸成员名：须显式 `this->`（AGG-08 隐式 self）
- 方法 E2009 归属随方案乙已精确到定义类
- `[[repr(C)]]` 注解未落地（ANN/P1-05），布局稳定性以普通 struct 钉住
- codegen 成员调用中的沿链回退成为死路径（保留，未来清理项）

## 8. 已知风险

- 方案乙触碰调用解析核心路径：868 个既有测试全量背书；AGG-10 的
  static 方法后向引用（I2）依赖同类查找，沿链化后需确认不受影响
  （同类查找 = 链第一跳，行为不变）
- struct 继承与匿名内联字段（AGG-03 promote）交互：promote 只在无继承的
  平凡场景使用，路径不相交，以既有测试背书
- Int8EnumNegativeValue 偶发 SEGFAULT（LLJIT flaky 家族）可能在本轮全量
  复现：按 AGG-11 轮既定口径处理（隔离重跑验证 + 台账记录）
