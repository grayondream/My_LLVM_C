# AGG-11 嵌套类型 + 前向声明 — 设计 spec

日期：2026-10-05
状态：已获用户逐节批准（方案 A；范围 1A 全种类任意深度 + static 随行；访问控制 2A 跟随访问段）
前置：AGG-10 static 成员已完成（840/840，`6577c90`）

## 0. 背景与目标

AGG-11（TODO.md:207）：嵌套类型、前向声明。P1-01 最后一项。

现状（实证 2026-10-05）：

| 项 | 现状 |
|---|---|
| class/struct 前向声明 + 自引用指针 | ✅ 可用（AGG-10 e2e 已验） |
| enum/union 前向声明 | 解析无错（语义待钉住） |
| 类体内嵌套 enum/struct/class/union 声明 | ❌ 静默吞掉，且**吞掉后续字段** |
| `Outer::Red` / `Outer::Inner` 类型与常量引用 | ❌ 未注册/解析失败 |

吞成员根因：成员循环调用 `parseType()`，其 enum/struct/union **内联定义分支**消费了
`enum Color { Red, Green };` 并返回 EnumType（无 DeclAST、常量未注册），成员循环随后
因无标识符而 `break`——后续全部成员丢失。

成功标准：类体内可声明嵌套类型（全种类、任意深度），`Outer::Inner` 类型引用与
`Outer::Color::Red` 常量引用可用，访问段控制生效，static 成员在嵌套类中可用，
全量 ctest 绿。

## 1. 方案（已选定 A）

### 方案 A（选定）：解析期类前缀复用

类/struct/union **体解析期间**压 `m_typeNamespacePrefix += mangleQualifiedTypeName(已限定类名) + "_"`（与 namespace 前缀机制 `Parser.cpp:2756-2779` 完全同构），成员循环识别嵌套类型声明路由到 decl 解析器，收集进 `StructDeclAST::nestedTypes`。

关键依据：类型引用在**解析期**查 TypeContext（`lookupNamedType`），类型必须解析期注册
——这使 sema 侧统一限定（方案 B）不可行；方案 C（提升为顶层兄弟声明）丢失访问上下文
且 AST 重构无收益。

### 否决项

- **方案 B（sema 侧统一限定）**：类型引用解析期查找，sema 来不及注册。
- **方案 C（parser 提升顶层）**：错误定位漂移、private 嵌套类型访问上下文丢失。

## 2. 冻结决策

### DS1：嵌套类型种类与深度（1A）

`enum/struct/class/union` 均可嵌套于 class/struct/union 体内；任意深度递归
（类中类中类 `Outer_Inner_Inner2` 由前缀累进自动正确）。嵌套类的 static 成员
（变量与方法）与实例方法随类型自然可用（§3）。

### DS2：解析期三路判定

成员位置遇 `enum/struct/class/union`：

| 形态 | 判定 | 路由 |
|---|---|---|
| 关键字 + IDENT + `{` | 嵌套类型声明 | 对应 decl 解析器 → `nestedTypes` |
| 关键字 + `{` | 匿名内联字段 | 原路径（AGG-03 不回归） |
| 关键字 + IDENT 无 `{` | 字段 | 原 parseType 路径 |

`class` 关键字同型判定（`class Inner {` → 嵌套类；`class C x;` 非法由既有诊断处理）。
`typedef` 在类体内：暂不嵌套（范围外，见 §6）。

### DS3：名称注册与引用（扁平键）

- 前缀压栈用**已限定类名**（`node.name`，如 ns 内 `smns_Outer`）：ns 内嵌套 →
  `smns_Outer_Inner`；类中类 → `Outer_Inner`；引用侧 `Outer::Inner` /
  `ns::Outer::Inner` → `mangleQualifiedTypeName` → 既有 `lookupNamedType` 零改动命中。
- `bareName` 沿 AGG-10 语义（qualifyTypeDeclName 前的原始名）。

### DS4：sema 遍历与 static 去糖推广

- `visit(StructDeclAST)` 注册完自身类型后 visit `nestedTypes`（**先于方法体分析**——
  AGG-10 时序规则）；`StructDeclAST::codegen` 同序。
- AGG-10 去糖公式推广：`scopedName(mangleNamespaceName(classPathPrefix + bareName + "::" + 成员))`，
  sema 新增 `classPathPrefix` 上下文（visit 嵌套类型时压栈外层类路径，如 `"Outer_"`）。
  验证：`ns::Outer::Inner::v` → 拍平 `ns_Outer_Inner_v` = 声明侧。
- 嵌套枚举常量注册键 = `mangleNamespaceName(枚举限定名 + "::" + 值)`
  （如 `Outer_Color_Red`）；与顶层枚举既有 `Mode::Red` 机制对齐（plan 阶段实证对齐点，
  若机制不同则按既有机制扩展而非另起炉灶）。

### DS5：访问控制（2A）

- parser 将嵌套类型级别记入外层类 `memberAccess["type:<名字>"]`（I1 独立键经验）。
- sema 建 `nestedTypeAccess`（类型名 → {外层类, 级别}），`memberAccessLevel` 缺键
  Public 的既有语义保持兼容。
- 检查点：**局部/全局变量声明与 new 表达式**——private 嵌套类型类外使用报 E2009；
  类体内（`currentClass` 匹配）可用。
- 限制（记录）：方法签名/返回类型/参数中的 private 嵌套类型不检查。

### DS6：前向声明

class/struct/enum/union 前向声明与自引用指针已实证解析可用——本轮以测试钉住
（前向声明+定义、指针自引用），预期零实现改动；若探针发现语义缺口按 Ruling 记录修复。

## 3. 架构与数据流

```
parser 成员循环（两处同步）
  └─ 三路判定（DS2）→ nestedTypes 收集
  └─ 体解析压/弹 m_typeNamespacePrefix（DS3）
sema visit(StructDeclAST)
  └─ 注册自身 → visit(nestedTypes)（压 classPathPrefix）→ staticMembers → 方法体
sema nestedTypeAccess + E2009 检查（DS5）
codegen StructDeclAST::codegen
  └─ nestedTypes 递归 → staticMembers → 方法（与 sema 同序；
     外层方法可引用嵌套 static，时序规则沿 AGG-10；枚举/typedef 空操作）
```

LLVM 类型由 `getLLVMType` 按注册名（`Outer_Inner`）惰性创建——无新布局逻辑。

## 4. 错误处理

- private 嵌套类型类外使用：E2009（复用 DEC-01 消息模式）。
- 嵌套声明名字与外层类成员/既有类型重名：沿用既有重定义诊断。
- 解析歧义误判（嵌套声明被当字段或反之）：测试钉住（§5）。

## 5. 测试计划（TDD，RED 先行；sema `NT` 前缀；预计 ~14 项）

| 组 | 项 |
|---|---|
| 解析注册 | 嵌套 enum/struct/class/union 各 1（sema）；类中类中类 1（sema） |
| 吞成员缺陷 | 嵌套 enum 后续字段不丢（sema，n4 复现钉住） |
| 类型引用 | `Outer::Inner obj` 局部/全局/字段/参数（sema+e2e） |
| 枚举常量 | `Outer::Color::Red`（sema+e2e）；ns 内 `ns::Outer::Color::Red`（sema） |
| 访问控制 | private 嵌套类型类外 var decl/new 拒绝 E2009；类内可用（sema） |
| static 随行 | 嵌套类 static 变量读写（sema+e2e）；嵌套类实例方法调用（e2e） |
| 前向声明 | 前向声明+定义+自引用指针（e2e 钉住既有行为） |

基线 840；预期完成 854 附近（以实际新增为准）。

## 6. 范围外（记录 TODO）

- 嵌套类型的 unqualified 引用（类体内直呼 `Inner`/`Red`——须全限定，与 DS1 单一规则一致）
- 方法签名/返回类型/参数中的 private 嵌套类型检查（DS5 限制）
- 嵌套类型的模板化、`Outer::Inner` 的 using 别名重导出、类体内 typedef 嵌套
- 嵌套类型内 protected 语义细分（随 INH-06）

## 7. 已知风险

1. 成员循环三路判定与匿名聚合（AGG-03）交互——匿名内联字段必须不回归（测试钉）。
2. 嵌套类体内再次压栈时 `bareName` 与 static 去糖一致性——plan 阶段探针验证
   （`ns::Outer::Inner` 内 static：`ns_Outer_Inner_v` 双侧一致）。
3. 前向声明部分可能有隐藏语义缺口——按 Ruling 记录。
