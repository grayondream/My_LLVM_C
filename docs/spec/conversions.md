# SafeModern C 1.0 — 类型转换规范

> 对应 TODO：**TYP-19 ~ TYP-24**、**TYP-20**、**DEC-06/07**
> 状态：`[impl]` 以 `src/sema/SemanticAnalyzer.cpp`、`src/ast/Symbol.cpp`、`src/codegen/CodegenContext.cpp` 为准；`[plan]` 为目标。
> 设计原则：与 C 的内存模型/转换语义对齐，但去掉隐式危险转换（如 enum↔int、指针↔int 的静默）。

## 1. 类型等价 `[impl]`

`typesEqual`（`src/ast/Symbol.cpp`）：

- 同 `TypeKind` 基本相等（标量只看 kind）。
- 指针/数组：递归比较元素类型（数组不比较长度）。
- struct/class/union：按**名字**相等。
- typedef：展开后比较。
- 函数：返回类型 + 参数逐个比较（不比较 `isVarArg`）。

**目标 `[plan]`**：数组长度应参与等价（TYP-11）；`const`/限定符应参与兼容性判断（TYP-16）。

## 2. 隐式转换（当前实现）`[impl]`

`typesCompatible` 允许：

| 来源 → 目标 | 允许 |
|---|---|
| 同 kind | ✅ |
| 任意算术 → 任意算术 | ✅ |
| 指针 ↔ 指针 | ✅ |
| 指针 ↔ `int` | ✅（含 0 空指针） |
| 数组 ↔ 指针 | ✅（退化） |

`getCommonType`（当前）：同 kind 取其一 → `double` → `float` → `int` → 否则取左。

> 这是**弱规则**：尚未实现常规算术转换，signed/unsigned 混合、窄化均被当作兼容。属重点补强项。

## 3. 整数提升与常规算术转换 `[plan]`（TYP-22）

目标规则（对齐 C 语义，冻结于 TYP-22）：

1. **整数提升**：`bool`、`char`、`int8/16`、`uint8/16`、`enum` → `int`；若 `int` 无法表示则 → `uint`。
2. **常规算术转换**（二元算术/比较，对操作数取公共类型）：
   - 若任一为浮点：按 `f16 < float32 < float64 < f128` 取较宽者。
   - 否则：
     - 提升后若符号性相同 → 取较宽。
     - 不同符号性：无符号 rank ≥ 有符号 → 无符号；有符号能覆盖无符号全部值 → 有符号；否则 → 无符号版本。
3. 转换等级（rank，由低到高）：`bool < char < int16 < int32 < int64 < int128`；无符号同宽度与有符号同 rank。

| kind | 位宽（当前 `integerWidth`） |
|---|---|
| bool | 1 |
| char/int8/uint8 | 8 |
| int16/uint16 | 16 |
| int/int32/uint32/enum | 32 |
| int64/uint64/isize/usize | 64 |
| int128/uint128 | 128 |
| float/float32 | 32 |
| double/float64 | 64 |

## 4. 转换矩阵 `[plan]`（TYP-23）

图例：`隐`=隐式允许；`警`=隐式允许但告警；`显`=仅显式转换；`✗`=禁止。目标语义，待 TYP-23 定稿。

| 从 \ 到 | 宽整数 | 窄整数 | 有/无符号互换 | 浮点 | bool | 指针 | enum |
|---|---|---|---|---|---|---|---|
| 宽整数 | 隐 | 显 | 隐/显 | 隐 | 隐 | 显 | 显 |
| 窄整数 | 隐 | 隐 | 隐/显 | 隐 | 隐 | 显 | 显 |
| 浮点 | 显 | 显 | — | 隐(宽)/显(窄) | 显 | ✗ | 显 |
| bool | 隐 | 隐 | 隐 | 隐 | 隐 | ✗ | 显 |
| 指针 | 显 | 显 | — | ✗ | 隐(≠0) | 隐(同类型)/显 | 显 |
| enum | 显 | 显 | 显 | 显 | 显 | ✗ | 隐 |

> `enum` 与整数**必须显式转换**（TYP-20），与上表一致。

## 5. 显式转换 `[impl]/[plan]`

- `[impl]` C 风格 `(T)x`：目前**仅识别基础类型关键字**（`int/float/double/char/void`），见 `parseUnary`。窄化只告警不报错（`CastExprAST` 检查）。
- `[plan]` `static_cast<T>(x)` / `reinterpret_cast<T>(x)`（LEX-11 / PAR-18）。
- `[plan]` `enum ↔ int`、指针 ↔ 整数的显式规则（TYP-20/24）。
- 代码生成 `castValue`（`CodegenContext.cpp:302`）：

| 转换 | 指令 |
|---|---|
| 整数→更宽 | `zext`（源为 `i1`）/ `sext` |
| 整数→更窄 | `trunc` |
| 整数→浮点 | `sitofp` |
| 浮点→整数 | `fptosi` |
| 浮点→更宽/更窄 | `fpext` / `fptrunc` |
| 指针→指针 | `bitcast` |

## 6. 空指针常量 `[plan]`（TYP-24）

- 当前：`null` 字面量在 `parsePrimary` 中直接变成整数常量 `0` `[impl]`。
- 目标：定义 `nullptr` / `NULL` / 字面量 `0` 到指针与 `bool` 的转换规则；指针↔整数不再静默（需显式）。

## 7. 常量求值与溢出

- `constexpr` / 常量折叠 `[impl]`（BASE-05）。
- 整型字面量溢出诊断：见 LEX-17 `[plan]`。
- 整数溢出行为：Debug 检查 / Release 回绕，见 **DEC-07**（SEM-10）。
- 数组/Slice 边界检查策略见 **DEC-06**（SEM-09）。

## 8. 重载解析中的转换等级 `[impl]`

`conversionRank`（`src/ast/Symbol.cpp`）用于 `OverloadSet::resolve`：

| 值 | 含义 |
|---|---|
| `0` | 精确匹配 |
| `1` | 安全算术转换：整数加宽、整数→浮点、浮点加宽、数组→指针 |
| `-1` | 不可隐式转换 |

> 目标：随 §3/§4 定稿后细化等级（含限定符、指针限定、模板实参推导），见 SEM-16。
