#pragma once

// P1-05 / ANN-02/03: 聚合布局单源化（spec §4）。
// Struct/Class/Union 的 LLVM 布局构造唯一权威——codegen（getLLVMType /
// StructDeclAST::codegen）与编译期求值器（toLLVMType / offset_of）共同消费，
// size_of/offset_of 与实际 IR 布局的一致性由此结构性保证。
//
// 无注解路径复刻迁移前的构造语义（自然对齐、基类槽 0、union 最大成员块）；
// packed/align 注解由 sema 折叠进 Type 对象（StructType/UnionType 的
// isPacked/forcedAlign/fieldAligns），本类据此插入显式 padding（T5）。

#include <cstdint>
#include <functional>
#include <vector>

#include "Type.h"

namespace llvm {
class LLVMContext;
class Type;
class DataLayout;
}

struct LayoutField {
    llvm::Type* type;
    uint64_t offset;
};

struct LayoutResult {
    std::vector<LayoutField> fields; // 含基类槽 0 与显式 padding 字段
    bool isPacked{false};
    uint64_t size{0};  // getTypeAllocSize 语义（字节）
    uint64_t align{0}; // ABI 对齐

    std::vector<llvm::Type*> fieldTypes() const {
        std::vector<llvm::Type*> out;
        out.reserve(fields.size());
        for (auto& f : fields) out.push_back(f.type);
        return out;
    }
};

class LayoutBuilder {
public:
    using TypeConverter = std::function<llvm::Type*(Type*)>;
    using NameResolver = std::function<Type*(const std::string&)>;

    // Struct/Class：基类子对象占槽 0（INH-01），字段序 = 声明序。
    static LayoutResult buildAggregate(Type* agg, llvm::LLVMContext& ctx,
                                       const llvm::DataLayout& dl,
                                       const TypeConverter& conv,
                                       const NameResolver& resolve);

    // Union：最大成员块 + 对齐成员优先 + 尾部填充（现状语义）。
    static LayoutResult buildUnion(UnionType* ut, llvm::LLVMContext& ctx,
                                   const llvm::DataLayout& dl,
                                   const TypeConverter& conv);
};
