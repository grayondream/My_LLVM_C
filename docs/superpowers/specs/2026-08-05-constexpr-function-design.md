# Design: Constexpr Function Support

> **Status**: Design

## Overview

Add `constexpr` function support with C++14/17-style extended constant expressions. Functions marked `constexpr` can be evaluated at compile time when called with constant arguments, using LLVM's constant folding during codegen.

## Semantics

- **`constexpr` function**: Function that *can* be evaluated at compile time when all arguments are constant expressions
- **Compile-time evaluation**: Performed by LLVM constant folder at codegen time (requires optimization level ≥ O1)
- **Runtime fallback**: When called with non-constant arguments, executes normally at runtime
- **Extended constant expressions (C++14/17)**: Allow loops, local variables, conditionals, multiple returns in function body

## Scope

- **Location**: Global scope only (no local constexpr functions)
- **Return types**: Arithmetic types only (int, float, double, char) — no struct/class/pointer returns
- **Parameter types**: Arithmetic types only
- **Function body**: Extended constexpr rules (loops, locals, conditionals allowed)

## Components

### 1. Lexer

- No changes needed — `TOKEN_CONSTEXPR` already exists

### 2. AST

- **`FunctionDeclAST`**: Add `bool isConstexpr` field (default false)
- Constructor updated to accept constexpr flag

### 3. Parser

- In `parseDeclaration()`: Handle `constexpr` keyword before function declarations
- When `constexpr` encountered before function, set `isConstexpr = true` on `FunctionDeclAST`
- Syntax: `constexpr return_type name(params) { body }`

### 4. Semantic Analyzer

#### 4.1 Constexpr Function Validation

In `visit(FunctionDeclAST&)`:
- If `isConstexpr` is true, validate:
  - Return type is literal type (arithmetic for now)
  - All parameter types are literal types
  - Function body follows constexpr rules (no static/thread_local locals, no asm, no goto - initially lenient)
- Register constexpr function in symbol table for lookup during call evaluation

#### 4.2 Constexpr Function Call Handling

In `checkFunctionCall()` / `visit(CallExprAST&)`:
- If callee is constexpr function AND all arguments are constant expressions:
  - Mark call as potentially foldable (codegen will handle actual folding)
- No sema-time evaluation — rely on LLVM constant folding

### 5. Codegen

- **`FunctionDeclAST::codegen()`**: Emit as regular LLVM function
- When optimization enabled (≥ O1), LLVM's constant folder automatically evaluates constexpr function calls with constant arguments
- No special IR emission needed — constexpr is a semantic hint to optimizer

### 6. Diagnostics

New error messages:
- `"constexpr function '%s' must have literal return type"` — non-arithmetic return
- `"constexpr function '%s' parameter '%s' must have literal type"` — non-arithmetic parameter
- `"constexpr function '%s' cannot have void return type"` — void return (optional, could allow for side-effect-free)

## Example

```c
// Basic constexpr function
constexpr int square(int x) {
    return x * x;
}

// Extended constexpr (C++14/17) - loops, locals, conditionals
constexpr int factorial(int n) {
    int result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

// Recursive constexpr
constexpr int fib(int n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

// Float constexpr
constexpr double circle_area(double r) {
    constexpr double pi = 3.14159265359;
    return pi * r * r;
}

void test() {
    // Compile-time evaluation (LLVM folds at -O1)
    constexpr int a = square(5);        // = 25
    constexpr int b = factorial(5);     // = 120
    constexpr int c = fib(10);          // = 55
    constexpr double d = circle_area(2.0); // = 12.566...

    // Runtime evaluation (non-constant arguments)
    int x = 5;
    int y = square(x);  // Not folded - runtime call
}
```

## Testing

1. **Lexer test**: Verify constexpr keyword already recognized
2. **Parser test**: constexpr function declarations parse correctly
3. **Sema test**:
   - constexpr function with valid signature accepted
   - constexpr function with non-literal return type rejected
   - constexpr function with non-literal parameter rejected
4. **Codegen test**:
   - constexpr function emitted as regular function
   - Verify LLVM constant folding works at -O1 (compile-time evaluation)
5. **End-to-end test**: Compile and run programs using constexpr functions with constant and runtime arguments

## Out of Scope (Future)

- Constexpr member functions
- Constexpr constructors
- Constexpr with struct/class return types
- Constexpr with pointer/reference parameters
- `consteval` / `constinit` keywords
- Compile-time evaluation without optimization (requires sema-time interpreter)

## Implementation Notes

### LLVM Constant Folding Requirements
- Must compile with optimization level ≥ O1 (`-O1`, `-O2`, `-O3`)
- At `-O0`, constexpr functions execute at runtime (acceptable default)
- CodegenContext should set optimization level appropriately

### Symbol Table
- constexpr functions stored in global scope like regular functions
- Type marked with constexpr flag for semantic lookup

### Recursion
- LLVM handles recursive constexpr functions (e.g., fib) up to its internal limits
- Deep recursion may hit LLVM's constant folding depth limits