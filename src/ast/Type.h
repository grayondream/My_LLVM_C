#pragma once
#include <unordered_map>
#include <string>
#include <vector>

// PAR-04/SEM-04/DEC-01: member access levels. `class` members default to
// Private, `struct` members to Public (unrecorded members fall back to
// Public, which keeps the struct path unchanged).
enum class AccessLevel {
    Public,
    Private,
    Protected, // equivalent to Private until INH (inheritance) lands
};

enum class TypeKind {
    Void,
    Char,
    Bool,
    Pointer,
    Array,
    Struct,
    Class,
    Union,
    Enum,
    Function,
    Typedef,
    // 新增类型
    Int8,
    Int16,
    Int32,
    Int64,
    Int128,
    UInt8,
    UInt16,
    UInt32,
    UInt64,
    UInt128,
    ISize,
    USize,
    Float32,
    Float64,
    Float16,
    Float128,
    Slice,
    Optional,
    Result,
    // P1-03 / GEN: 模板类型参数占位与模板使用占位。TypeVar 仅合法存在于
    // 模板体 AST 内；TypeInstance 是 parse 期 `Name<args>` 的占位，sema 在
    // 使用点解析为具体实例类型后不再残留。
    TypeVar,
    TypeInstance,
};

class Type {
public:
    ~Type() = default;
    Type(const TypeKind kind, Type* base = nullptr)
        : kind(kind), base(base) {}
public:
    TypeKind kind{};
    Type* base{};
    bool isVolatile{};
    bool isConst{};
    // P1-04 / CT-03: 该类型仅在 compile_time.if 未选中分支被声明（parse 期
    // 占位毒化）。类型引用在 parse 期绑定指针，仅靠 TypeContext 注销无法
    // 阻断使用；sema 在变量声明处拒绝毒化类型（活分支的 sema 注册创建新
    // 对象，天然解毒）。
    bool ctDeadBranch{false};
};

class ArrayType : public Type {
public:
    Type* elementType;
    int size;
    // P1-03 / GEN-02: 非空时 size 是占位值（0），实际长度在实例化时由该
    // 命名的非类型模板参数给出（`T data[N]`）。具体化后副本须清空本字段。
    std::string sizeParam;

    ArrayType(Type* elem, int sz)
        : Type(TypeKind::Array), elementType(elem), size(sz) {}
};

class StructType : public Type {
public:
    std::string name;
    // INH-01: single public inheritance — base sub-object occupies field
    // slot 0 in the LLVM layout (mirrors ClassType::baseClass/base).
    std::string baseClass;
    Type* base{};
    // INH-01: completion tracking (mirrors ClassType::isComplete).
    bool isComplete{};
    // P1-03 / GEN: 模板体 parse 经普通声明路径注册的占位类型——裸模板名
    // （无实参）使用时由 sema 诊断。
    bool isTemplatePattern = false;
    std::vector<std::pair<std::string, Type*>> fields;

    StructType(const std::string& n)
        : Type(TypeKind::Struct), name(n) {}

    Type* getFieldType(const std::string& fieldName) const {
        for (auto& [name, type] : fields) {
            if (name == fieldName) return type;
        }
        return nullptr;
    }

    void addField(const std::string& fieldName, Type* fieldType) {
        fields.push_back({fieldName, fieldType});
    }
};

class UnionType : public Type {
public:
    std::string name;
    std::vector<std::pair<std::string, Type*>> members;
    // Redef 轮: completion tracking (mirrors ClassType::isComplete) — a
    // forward declaration leaves the placeholder incomplete.
    bool isComplete{};

    UnionType(const std::string& n)
        : Type(TypeKind::Union), name(n) {}

    void addMember(const std::string& memberName, Type* memberType) {
        members.push_back({memberName, memberType});
    }
};

class EnumType : public Type {
public:
    std::string name;
    std::vector<std::pair<std::string, int>> values;
    // Explicit underlying type (`enum E : u8`); null means the default (int).
    Type* underlyingType = nullptr;
    // Redef 轮: completion tracking (mirrors ClassType::isComplete).
    bool isComplete{};

    EnumType(const std::string& n)
        : Type(TypeKind::Enum), name(n) {}

    void addValue(const std::string& valueName, int val) {
        values.push_back({valueName, val});
    }
};

class FunctionType : public Type {
public:
    Type* returnType;
    std::vector<Type*> paramTypes;
    bool isVarArg;

    FunctionType(Type* ret, std::vector<Type*> params, bool varArg = false)
        : Type(TypeKind::Function), returnType(ret), paramTypes(std::move(params)), isVarArg(varArg) {}
};

class TypedefType : public Type {
public:
    std::string name;
    Type* aliasedType;

    TypedefType(const std::string& n, Type* aliased)
        : Type(TypeKind::Typedef), name(n), aliasedType(aliased) {}
};

class SliceType : public Type {
public:
    Type* elementType;

    SliceType(Type* elem)
        : Type(TypeKind::Slice), elementType(elem) {}
};

class OptionalType : public Type {
public:
    Type* elementType;

    OptionalType(Type* elem)
        : Type(TypeKind::Optional), elementType(elem) {}
};

class ResultType : public Type {
public:
    Type* successType;
    Type* errorType;

    ResultType(Type* success, Type* error)
        : Type(TypeKind::Result), successType(success), errorType(error) {}
};

// P1-03 / GEN-01: 模板类型参数占位（`template<typename T>` 中的 T）。
// 仅合法存在于模板体 AST；实例化（克隆替换）后不得残留。
class TypeVarType : public Type {
public:
    std::string name;

    explicit TypeVarType(const std::string& n)
        : Type(TypeKind::TypeVar), name(n) {}
};

// P1-03 / GEN-03: parse 期模板使用占位（`Box<i32>`、`Array<T, 8>`）。
// sema 在使用点触发实例化并把它解析为具体 ClassType；sema 结束后不存在。
class TypeInstanceType : public Type {
public:
    std::string templateName;
    std::vector<Type*> typeArgs;
    std::vector<long long> valueArgs;

    explicit TypeInstanceType(const std::string& n)
        : Type(TypeKind::TypeInstance), templateName(n) {}
};

class ClassType : public Type {
public:
    std::string name;
    std::vector<std::pair<std::string, Type*>> fields;
    std::vector<std::pair<std::string, FunctionType*>> methods;
    std::string baseClass;

    // DEC-01: per-member access levels; absent -> Public (struct path).
    std::unordered_map<std::string, AccessLevel> memberAccess;

    // INH-01: false while only a forward declaration has been seen; sema sets
    // it when the definition is visited. Deriving from an incomplete class is
    // diagnosed.
    bool isComplete{};

    // P1-03 / GEN: 模板体 parse 的占位注册（同 StructType::isTemplatePattern）。
    bool isTemplatePattern = false;

    ClassType(const std::string& n)
        : Type(TypeKind::Class), name(n) {}

    void setMemberAccess(const std::string& memberName, AccessLevel level) {
        memberAccess[memberName] = level;
    }

    AccessLevel memberAccessLevel(const std::string& memberName) const {
        auto it = memberAccess.find(memberName);
        return it == memberAccess.end() ? AccessLevel::Public : it->second;
    }

    void addField(const std::string& fieldName, Type* fieldType) {
        fields.push_back({fieldName, fieldType});
    }

    void addMethod(const std::string& methodName, FunctionType* methodType) {
        methods.push_back({methodName, methodType});
    }

    Type* getFieldType(const std::string& fieldName) const {
        for (auto& [name, type] : fields) {
            if (name == fieldName) return type;
        }
        return nullptr;
    }

    FunctionType* getMethod(const std::string& methodName) const {
        for (auto& [name, type] : methods) {
            if (name == methodName) return type;
        }
        return nullptr;
    }
};

class TypeContext{
public:
    static TypeContext& instance();
    ~TypeContext();

    Type* getChar();
    Type* getVoid();

    void addTypedef(const std::string& name, Type* type);
    Type* getTypedef(const std::string& name) const;

    void addStruct(const std::string& name, StructType* type);
    StructType* getStruct(const std::string& name) const;

    void addUnion(const std::string& name, UnionType* type);
    UnionType* getUnion(const std::string& name) const;

    void addEnum(const std::string& name, EnumType* type);
    EnumType* getEnum(const std::string& name) const;

    void addClass(const std::string& name, ClassType* type);
    ClassType* getClass(const std::string& name) const;
    ClassType* getOrCreateClass(const std::string& name);

    // P1-03 / GEN: 模板体 parse 会经普通声明路径注册类型名（占位）——模板
    // 注册时须撤销，保证裸模板名（无实参）在类型位置不可解析。
    void removeStruct(const std::string& name);
    void removeClass(const std::string& name);
    // P1-04 / CT-03: compile_time.if 死分支占位撤销（同上先例）。
    void removeUnion(const std::string& name);
    void removeEnum(const std::string& name);

    // 仅测试用：清除所有模板实例类型（名字含 '$'）。
    void removeInstanceTypes();

    // 新增类型获取方法
    Type* getBool();
    Type* getInt8();
    Type* getInt16();
    Type* getInt32();
    Type* getInt64();
    Type* getInt128();
    Type* getUInt8();
    Type* getUInt16();
    Type* getUInt32();
    Type* getUInt64();
    Type* getUInt128();
    Type* getISize();
    Type* getUSize();
    Type* getFloat32();
    Type* getFloat64();
    Type* getFloat16();
    Type* getFloat128();
    SliceType* getSliceType(Type* elementType);
    OptionalType* getOptionalType(Type* elementType);
    ResultType* getResultType(Type* successType, Type* errorType);

    // P1-03 / GEN-01: 模板类型参数占位，同名同指针（单射）。
    TypeVarType* getTypeVar(const std::string& name);

private:
    std::unordered_map<TypeKind, Type*> m_types;
    std::unordered_map<std::string, Type*> m_typedefs;
    std::unordered_map<std::string, StructType*> m_structs;
    std::unordered_map<std::string, UnionType*> m_unions;
    std::unordered_map<std::string, EnumType*> m_enums;
    std::unordered_map<std::string, ClassType*> m_classes;
    std::unordered_map<std::string, TypeVarType*> m_typeVars;
};
