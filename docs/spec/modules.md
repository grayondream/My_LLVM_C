# SafeModern C 1.0 — 模块 / 命名空间 / 名称解析规范

> 对应 TODO：**MOD-01 ~ MOD-15**（重点 MOD-12~15）、**DEC-02/14/17**
> 状态：`[impl]` 以 `src/frontend/Parser.cpp`、`src/sema/SemanticAnalyzer.cpp`、`src/ast/Symbol.cpp` 为准；`[plan]` 为目标。
> 前提：**无预处理器**（NG-02），模块是唯一的跨文件组织方式；C 互操作走 `extern` + `std.c` 绑定层（MOD-09/STD-23）。

## 1. 现状 `[impl]`

`Parser::parse` 当前仅做**记录**，未实现任何跨文件解析：

| 语法 | 解析结果 | 缺口 |
|---|---|---|
| `module NAME;` | `ModuleDeclAST(name, imports, exports)` | 无文件映射、无编译单元绑定 |
| `import NAME;` | 收集到 imports（仅当紧随标识符） | 不加载目标、不建符号 |
| `export decl` | 解析 `decl` 后丢弃 `export` 标记 | `exports` 恒空，无可见性语义 |

其余：符号表为**单文件全局作用域**（`Scope` 链），无模块层级；无 `namespace`；无循环依赖检测。

## 2. 命名空间（MOD-12，DEC-17）`[plan]`

### 2.1 语法（对齐 PAR-22 / grammar.ebnf）

```smc
namespace geometry {        // 声明
    struct Point { int x; int y; }
}

namespace geometry.ops {    // 嵌套
    int dot(Point a, Point b) { ... }
}

namespace geometry {        // 开放命名空间：可再次进入
    struct Circle { Point c; int r; }
}
```

- 限定名 `A::B::name`（`::` token 待补，LEX-14/PAR-22）。
- 命名空间是**编译期名字分组**，与模块（文件/编译单元）**正交**：
  - 模块决定“哪些文件一起编译/如何导入”；
  - namespace 决定“符号的限定名与查找路径”。
- 关系与共存方式最终由 **DEC-17** 裁定；本文件建议：`module` 为物理边界，`namespace` 为逻辑边界，允许同模块多 namespace、同 namespace 跨模块。

### 2.2 查找规则

1. 非限定名：当前块 → 外层块 → 函数 → 当前 namespace（由内向外）→ 其外层 namespace → 模块根 →（若 import）被导入模块的**导出**符号。
2. 限定名 `A::x`：从全局根按名逐级进入 namespace/module。
3. ADL（实参依赖查找）**不引入**（保持 C 风格可预测，见 NG-03）。

## 3. 模块与文件（MOD-04/13）`[plan]`

- **一文件一模块**：每个 `.smc` 源文件最多一条 `module` 声明，且必须位于文件首部。
- 文件↔模块映射：`module a.b.c;` ↔ 路径 `a/b/c.smc`；缺省模块名为文件名（去扩展名）。
- 编译单元 = 一个模块文件（或显式引入的一组）。
- 导入搜索路径由驱动提供（`-M`/`--module-path`，见 TOOL-04/TOOL-08），默认搜索当前目录与标准库根。

> 待定：`module` 是否存在子模块/分区（partial）语义——归入 DEC-17 一并裁定。

## 4. 导入与可见性（MOD-05/06/11，DEC-02）

### 4.1 导入 `[plan]`

```smc
import math;            // 形式 1：引入候选符号
import math::{add, sub};// 形式 2（提案）：选择性导入
import math as m;       // 形式 3（提案）：别名
```

- 被导入模块内**未标记 `export`/`public`** 的符号不可见。
- 访问语法（`math.add` 还是直接 `add`）由 **DEC-02** 裁定；本文件建议默认**限定访问** `math.add`，选择性导入后可直接用名，避免全局污染。

### 4.2 可见性

| 标记 | 作用域 | 状态 |
|---|---|---|
| `export` | 模块外可见 | `[impl]` 仅记录，语义待实现 |
| `public` | 类型成员 / namespace 成员公开 | `[impl]` 成员段待补（PAR-04） |
| `private` | 仅本模块/本类型可见 | `[impl]` 同上 |
| 无标记 | 默认**模块内私有**（提案） | `[plan]` |

## 5. 名称解析与作用域（MOD-01/02/03）`[impl → 待扩展]`

- 现有 `Scope`：块/函数/全局；`declare` 拒绝同层重名，`lookup` 沿父链上升。
- 重载：同层同名非函数符号冲突；函数进入 `OverloadSet`（见 semantics.md §3）。
- 前向声明：函数/struct/class/enum/union 已有占位类型 `[impl]`。
- **目标**：在全局之上增加 模块层 → namespace 层，查找顺序见 §2.2。

## 6. 循环依赖（MOD-07）`[plan]`

- 构建模块依赖图，DFS 检出环并报错，诊断列出环路径。
- 类型级前向引用通过占位类型允许，但**导入环**禁止（与 Rust 类似）。
- 纯类型循环（`struct A { B* b; }` / `struct B { A* a; }`）允许，参照 C。

## 7. 名称修饰（MOD-15）

- 现状与目标编码见 [`abi.md` §4](./abi.md)。namespace 与模板实例需纳入编码，保证跨模块稳定、可复现（INF-07）。

## 8. 增量编译与缓存（MOD-10，DEC-14）`[plan]`

- 以**内容哈希**为键缓存 模块/AST/类型/IR；模块接口（导出符号表）单独序列化。
- 失效策略：实现变更、导入闭包任一变更 → 失效。
- 缓存格式与稳定性见 **DEC-14**；AST 序列化基础设施见 PAR-19。

## 9. C 互操作（MOD-09 / STD-23）

- 不解析真实 C 头文件；用 `extern` 声明 + 手写 `std.c` 绑定层。
- `extern` 符号保持未修饰 C 名（`isCName`，见 abi.md §4）。
- 关键缺口，列为 P0-02。

## 10. 未决项

| 决策 | 内容 |
|---|---|
| DEC-02 | 导入后符号访问语法（`math.add` vs `add`） |
| DEC-14 | 模块缓存格式与稳定性 |
| DEC-17 | `namespace` 与 `module` 的关系与共存方式 |
