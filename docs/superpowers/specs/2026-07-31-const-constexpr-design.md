# Design: const/constexpr Variable Support

> **Status**: Implemented

## Overview

Add `const` and `constexpr` qualifier support for variables with basic types (int, float, char, double, etc.) at both global and local scope. Includes Sema-stage constant folding for constexpr variables.

## Semantics

- **`const`**: Variable cannot be modified after initialization. Can be initialized with any expression (runtime or compile-time).
- **`constexpr`**: Variable must be initialized with a compile-time constant expression (literal, arithmetic on literals, or other constexpr variables). The initializer is evaluated at compile time.

## Scope

- **Location**: Both global and local scope
- **Types**: Basic types only (int, float, char, double, etc.) — no pointer const
- **Functions**: No constexpr function support

## Components

### 1. Lexer

- Add `TOKEN_CONSTEXPR` to `TokenType` enum in `Token.h`
- Add `"constexpr"` to `keywordMap` in `Lexer.cpp`

### 2. AST

- `VarDeclAST`: Add `bool isConstexpr` field (default false)
- `Type`: `isConst` already exists, reuse it for `const` qualifier

### 3. Parser

- `parseType()`: Already handles `const` and `volatile` as type qualifiers
- Add handling for `constexpr` keyword in declaration context
- When `constexpr` is encountered, set `isConstexpr = true` on `VarDeclAST`

### 4. Semantic Analyzer

#### 4.1 Const Assignment Check

In `visit(AssignmentExprAST&)`:
- After resolving LHS type, check if `lhs->isConst` is true
- If const, emit error: "cannot assign to const variable"

#### 4.2 Constexpr Validation

In `visit(VarDeclAST&)`:
- If `isConstexpr` is true, validate initializer is a constant expression
- Call `evaluateConstexpr()` to attempt compile-time evaluation

#### 4.3 Constant Folding

New method `evaluateConstexpr(ExprAST* expr)`:
- Recursively evaluates constant expressions at compile time
- Supported operations:
  - Integer literals → return value
  - Float literals → return value
  - Character literals → return integer value
  - Binary arithmetic (`+`, `-`, `*`, `/`, `%`) on constants → fold
  - Unary operations (`-`, `+`, `!`) on constants → fold
  - Parenthesized expressions → unwrap and evaluate
- Returns `std::optional<ConstValue>` where `ConstValue` is a variant-like type
- On failure (non-constant expression), returns `std::nullopt`

#### 4.4 Symbol Table

- `Symbol` class: no changes needed (const-ness is on `Type`)
- When declaring constexpr variable, store folded value in symbol if possible

### 5. Codegen

- For `constexpr` local variables: use folded constant value directly in store
- For `const` local variables: normal alloca + store, but sema prevents reassignment
- For global `const` variables: emit as `@name = constant` instead of `@name = global`
- For global `constexpr` variables: emit as `@name = constant` with folded value

### 6. Diagnostics

New error messages:
- `"cannot assign to const variable '%s'"` — when assigning to const
- `"constexpr variable '%s' must be initialized with a constant expression"` — when constexpr init is not constant
- `"constexpr variable '%s' must have an initializer"` — when constexpr has no init

## Example

```c
// Global const/constexpr
const int g_const = 10;
constexpr int g_constexpr = 20;
constexpr int g_folded = g_constexpr * 2 + 3;  // = 43

void foo() {
    // Local const/constexpr
    const int local_const = 10;
    constexpr int local_constexpr = 30;
    constexpr int local_folded = local_constexpr + 5;  // = 35

    local_const = 20;      // ERROR: cannot assign to const
    local_constexpr = 40;  // ERROR: cannot assign to constexpr
    constexpr int bad = x; // ERROR: x is not constexpr
}
```

## Testing

1. **Lexer test**: constexpr token recognition
2. **Parser test**: const/constexpr declarations parse correctly
3. **Sema test**:
   - const assignment rejected
   - constexpr with non-constant init rejected
   - constexpr constant folding produces correct values
4. **Codegen test**:
   - global const emitted as `constant`
   - constexpr variables have correct values
5. **End-to-end test**: compile and run programs using const/constexpr

## Out of Scope

- Pointer const (`const int*`, `int* const`)
- Const member functions
- Constexpr functions
- Const reference parameters
- Mutable keyword
