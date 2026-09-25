# SafeModern C 1.0 — ABI / 类型布局规范

> 对应 TODO：**TYP-27**、**MEM-14/15/16**、**MOD-15**、**CG-01/18**
> 状态：`[impl]` 以 `src/codegen/CodegenContext.cpp`、`src/ast/Mangle.cpp` 为准；`[plan]` 为目标，可能随 DEC 调整。
> 目标平台：默认跟随宿主 LLVM target（`module->getDataLayout()`）；交叉编译配置见 CG-16。

## 1. 标量类型映射与布局

| 源类型 | LLVM 类型 | 位宽 | 对齐 | 状态 | 备注 |
|---|---|---|---|---|---|
| `void` | `void` | — | — | `[impl]` | |
| `bool` | `i1` | 1 | 1B | `[impl]` | 存储按 1 字节 |
| `char` | `i8` | 8 | 1 | `[impl]` | |
| `int` | `i32` | 32 | 4 | `[impl]` | 平台默认 |
| `int8/16/32/64/128` | `i8/i16/i32/i64/i128` | 8…128 | 1…16 | `[impl]` | |
| `uint8…uint128` | `i8…i128` | 同上 | 同上 | `[impl]` | 无符号仅为语义 |
| `isize` | `i64` | 64 | 8 | `[impl]` | **目标**：绑定目标指针宽度（TYP-03） |
| `usize` | `i64` | 64 | 8 | `[impl]` | 同上；Slice 长度也用 `i64` |
| `float` / `float32` | `float` | 32 | 4 | `[impl]` | |
| `double` / `float64` | `double` | 64 | 8 | `[impl]` | |
| `f16` | — | 16 | 2 | `[plan]` | TYP-04，含软件兜底 |
| `f128` | — | 128 | 16 | `[plan]` | TYP-04 |
| `enum` | 底层类型（默认 `i32`） | 见底层 | 见底层 | `[impl]` | 显式底层类型 `:uint8` 等（TYP-09/TYP-25） |
| `T*` | `ptr` (opaque) | 指针 | 指针 | `[impl]` | 多级 = 多 ptr |
| `T[N]` | `[N x T]` | N×size | align(T) | `[impl]` | |
| `T[]` (Slice) | `{ ptr, i64 }` | 16(64 位) | 8 | `[impl]` | **目标** `{ptr, usize}`，TYP-12 |
| `T?` (Optional) | `{ T, i1 }` | — | — | `[impl]` | **目标** `{ bool valid; T value; }`，TYP-13 |
| `Result<T,E>` | `{ T, E }` | — | — | `[impl]` | 精确布局/访问见 DEC-03 |

## 2. 聚合布局

### 2.1 struct `[impl]`
- 使用具名 `%struct.Name`；字段按声明顺序。
- 自然对齐：每个字段对齐到自身 ABI 对齐，结构体对齐到最大成员对齐，尾部按需填充（LLVM 默认规则）。
- 无隐藏字段、无 vtable、无填充字段语义。

### 2.2 class `[impl]`
- 布局等价 struct；成员函数不占空间（AGG-08/13）。
- **继承**：基类子对象作为派生类第一个字段（偏移 0）——已按 `classDecl->baseClass` 生成（INH-03）；多继承明确不支持（INH-02/NG-09）。
- 成员函数降级为自由函数 `Class_method(Class* self, ...)`（AGG-09，见 §4）。

### 2.3 union `[impl]`
- 大小为最大成员分配大小，对齐为最对齐成员对齐。
- 所有成员偏移 0；成员访问是对字段 0 的 GEP + bitcast。
- 不跟踪活跃成员（C 风格，AGG-17）。

### 2.4 enum `[impl]`
- 默认底层类型为 `int`（`i32`）；可用 `enum E : uint8 { ... }` 显式指定任意整型底层类型，LLVM 层即该底层类型（TYP-09/TYP-25）。
- 枚举常量为编译期整数常量，参与常量折叠；固定大小由底层类型决定。
- 传参/返回按底层类型；窄于 `int` 的枚举作可变参数时经 C 默认实参提升（见 `conversions.md` §9）。
- `enum ↔ int` 不隐式转换的强制检查（TYP-09/20）仍为 `[plan]`。

### 2.5 位域 `[plan]`
- 尚未实现；布局对齐哪个 C ABI（System V / MSVC）与 `[[packed]]`/`[[align]]` 交互见 **DEC-08**（AGG-06 / CG-09）。

## 3. 传参 / 返回 ABI

| 场景 | 当前 `[impl]` | 目标 `[plan]` |
|---|---|---|
| 标量参数/返回 | 直接按 LLVM 标量 | C 约定（MEM-16） |
| 指针 | `ptr` | 同 C |
| struct/class/union 按值 | 作为 LLVM 聚合值传递 | 需明确 `byval`/`sret` 与寄存器分类（MEM-14） |
| Slice/Optional/Result | 作为 LLVM `{...}` 聚合值 | 冻结布局后再定 |
| 可变参数 | `isVarArg`（FUN-06） | 与 C varargs 兼容 |
| 调用约定 | LLVM 默认（宿主） | C 调用约定；可选其他（MEM-11/MEM-16） |

> 说明：当前实现依赖 LLVM 默认 lowering，**未显式标注** `byval`/`sret`/`signext`/`zeroext`。目标 ABI 必须在 MEM-14 冻结，并用 CG-18 ABI 一致性测试锁定。

## 4. 名称修饰（MOD-15）

当前规则（`src/ast/Mangle.cpp`）：

1. 经 `extern` 声明的符号标记为 C 名，**不修饰**（`isCName`）→ 直接 C ABI 链接。
2. 无参数函数名保持原名（`paramTypes.empty()` 时返回 `name`）。
3. 其余函数：`name + "_" + typeToMangled(param)...`，例如 `max_int_int`。

`typeToMangled` 规则：

| 类型 | 编码 | 类型 | 编码 |
|---|---|---|---|
| `void/int/float/double/char/bool` | `void/int/float/double/char/bool` | `T*` | `<T>ptr` |
| `int8..int128` | 同名 | `uint8..uint128` | 同名 |
| `isize/usize` | 同名 | `float32/float64` | 同名 |
| `A[N]` | `<A>arr` | `T[]` | `<T>slice` |
| `T?` | `<T>opt` | `Result<T,E>` | `<T>res<E>` |
| struct/class/union | 类型名 | enum | `int` |
| typedef | 展开到底层 | | |

**目标（MOD-15）**：需补充 `static` 成员、namespace、模板单态化符号的规范编码，并保证与 C ABI / FFI 的边界清晰（MEM-12/ACC-05）。命名必须稳定、可复现（INF-07）。

## 5. 端序与对齐

- 端序：跟随目标 `DataLayout`；规范不假定小端（端序规则见 MEM-15）。
- 默认对齐：LLVM 目标 ABI 对齐；`[[packed]]` / `[[align(N)]]` 尚未实现（ANN-03），交互见 DEC-08。
- 可复现构建要求输出路径无关、确定性（INF-07）。

## 6. 参考实现位置

- 类型映射：`CodegenContext::getLLVMType`（`src/codegen/CodegenContext.cpp:338`）
- 值转换：`CodegenContext::castValue`（:302）
- 名称修饰：`src/ast/Mangle.cpp`
- 类型定义：`src/ast/Type.h`
