# Class Support Design Spec

## Overview
Add class/struct support with member functions, `this` pointer, and public inheritance. No constructors, destructors, virtual functions, or templates.

## Requirements
- `class` and `struct` are identical (no access control difference)
- Member functions (methods) with implicit `this` pointer
- Public inheritance via `class Derived : public Base { ... }`
- No constructors or destructors
- No virtual functions
- No templates

## Approach: Extend StructDeclAST

Minimal changes to existing struct infrastructure. Reuses existing code paths.

## Changes

### 1. AST (`src/ast/Decl.h`)
Add to `StructDeclAST`:
```cpp
struct StructDeclAST : DeclAST {
    std::string name;
    std::vector<std::pair<std::string, Type*>> fields;
    std::vector<FunctionDeclAST*> methods;  // NEW
    std::string baseClass;                  // NEW (empty = no inheritance)
    // ...
};
```

### 2. Parser (`src/frontend/Parser.cpp`)
- Add `class` keyword mapped to `TOKEN_STRUCT` (same token)
- Parse inheritance: `class Derived : public Base { ... }`
- Parse member functions inside struct body
- Member functions are `FunctionDeclAST` with implicit `this` as first parameter

### 3. Semantic Analyzer (`src/sema/SemanticAnalyzer.cpp`)
- Validate inheritance chain (base class must exist, no circular inheritance)
- Inject `this` pointer type for method calls
- Method resolution: search derived class first, then base classes

### 4. Code Generation (`src/ast/Decl.cpp`, `src/codegen/CodegenContext.cpp`)
- Struct with methods: methods are separate LLVM functions
- `this` is passed as first parameter (pointer to struct)
- Inheritance: derived struct contains base struct as first field
- Method calls: `obj.method(args)` becomes `method(&obj, args)`

### 5. Name Mangling (`src/ast/Mangle.cpp`)
- Class methods: `ClassName::MethodName` mangles to `ClassName_MethodName_type1_type2`
- Inherited methods: same mangling, resolved at call site

## Example

Input:
```cpp
class Animal {
    int age;
    
    void setAge(int a) {
        this->age = a;
    }
    
    int getAge() {
        return this->age;
    }
};

class Dog : public Animal {
    int breed;
    
    void setBreed(int b) {
        this->breed = b;
    }
};

int main() {
    Dog d;
    d.setAge(5);
    d.setBreed(1);
    return d.getAge();
}
```

Generated LLVM IR (conceptual):
```llvm
%struct.Animal = type { i32 }
%struct.Dog = type { %struct.Animal, i32 }

define void @Animal_setAge(%struct.Animal* %this, i32 %a) {
    %age_ptr = getelementptr %struct.Animal, %struct.Animal* %this, i32 0, i32 0
    store i32 %a, i32* %age_ptr
    ret void
}

define i32 @Animal_getAge(%struct.Animal* %this) {
    %age_ptr = getelementptr %struct.Animal, %struct.Animal* %this, i32 0, i32 0
    %age = load i32, i32* %age_ptr
    ret i32 %age
}

define void @Dog_setBreed(%struct.Dog* %this, i32 %b) {
    %breed_ptr = getelementptr %struct.Dog, %struct.Dog* %this, i32 0, i32 1
    store i32 %b, i32* %breed_ptr
    ret void
}

define i32 @main() {
    %d = alloca %struct.Dog
    %d_base = bitcast %struct.Dog* %d to %struct.Animal*
    call void @Animal_setAge(%struct.Animal* %d_base, i32 5)
    call void @Dog_setBreed(%struct.Dog* %d, i32 1)
    %d_base2 = bitcast %struct.Dog* %d to %struct.Animal*
    %age = call i32 @Animal_getAge(%struct.Animal* %d_base2)
    %result = sub i32 %age, 4
    ret i32 %result
}
```

## Testing

1. **Unit tests**: Parser tests for class syntax, sema tests for method resolution
2. **E2E tests**: 
   - Basic class with methods
   - Class inheritance
   - Method calls on derived class
   - `this` pointer usage
   - Multiple levels of inheritance
