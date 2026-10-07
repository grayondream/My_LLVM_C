#include "ast/LayoutBuilder.h"

#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/ErrorHandling.h"

// P1-05 / ANN-02/03: 布局构造唯一实现。
// 无注解路径复刻迁移前语义；T5 的 packed/align 经 Type 侧折叠字段
// （isPacked/forcedAlign/fieldAligns）进入构造。DataLayout 计算的偏移即
// 返回值——codegen 与编译期查询天然一致。

LayoutResult LayoutBuilder::buildAggregate(Type* agg, llvm::LLVMContext& ctx,
                                           const llvm::DataLayout& dl,
                                           const TypeConverter& conv,
                                           const NameResolver& resolve) {
    LayoutResult lr;
    const std::string* baseName = nullptr;
    const std::vector<FieldInfo>* flds = nullptr;
    if (agg->kind == TypeKind::Struct) {
        auto* st = static_cast<StructType*>(agg);
        baseName = &st->baseClass;
        flds = &st->fields;
        lr.isPacked = st->isPacked;
    } else if (agg->kind == TypeKind::Class) {
        auto* ct = static_cast<ClassType*>(agg);
        baseName = &ct->baseClass;
        flds = &ct->fields;
        lr.isPacked = ct->isPacked;
    } else {
        llvm::report_fatal_error("LayoutBuilder::buildAggregate: not an aggregate");
    }

    std::vector<llvm::Type*> fieldTypes;
    // 基类子对象占槽 0（INH-01）。基类按名解析（StructType::base 可能为空
    // 占位）；解析/转换失败则跳过——与迁移前的懒创建回退行为一致。
    if (!baseName->empty() && resolve) {
        if (Type* baseType = resolve(*baseName)) {
            if (llvm::Type* bt = conv(baseType)) {
                fieldTypes.push_back(bt);
            }
        }
    }
    for (auto& f : *flds) {
        if (llvm::Type* ft = conv(f.type)) {
            fieldTypes.push_back(ft);
        }
    }

    auto* temp = llvm::StructType::get(ctx, fieldTypes, lr.isPacked);
    const llvm::StructLayout* sl = dl.getStructLayout(temp);
    for (unsigned i = 0; i < fieldTypes.size(); ++i) {
        lr.fields.push_back({fieldTypes[i], sl->getElementOffset(i)});
    }
    lr.size = sl->getSizeInBytes();
    lr.align = sl->getAlignment().value();
    return lr;
}

LayoutResult LayoutBuilder::buildUnion(UnionType* ut, llvm::LLVMContext& ctx,
                                       const llvm::DataLayout& dl,
                                       const TypeConverter& conv) {
    LayoutResult lr;
    // 现状语义：最大成员块 + 对齐成员优先 + 尾部填充；成员偏移全 0。
    uint64_t maxSize = 0;
    uint64_t maxAlign = 0;
    llvm::Type* alignType = nullptr;
    for (auto& m : ut->members) {
        llvm::Type* mt = conv(m.type);
        if (!mt) continue;
        uint64_t sz = dl.getTypeAllocSize(mt);
        uint64_t al = dl.getABITypeAlign(mt).value();
        if (sz > maxSize) maxSize = sz;
        if (al > maxAlign) {
            maxAlign = al;
            alignType = mt;
        }
    }
    if (maxSize == 0) maxSize = 1;
    std::vector<llvm::Type*> fieldTypes;
    if (alignType) {
        fieldTypes.push_back(alignType);
        uint64_t alignSize = dl.getTypeAllocSize(alignType);
        if (maxSize > alignSize) {
            fieldTypes.push_back(llvm::ArrayType::get(
                llvm::Type::getInt8Ty(ctx), maxSize - alignSize));
        }
    } else {
        fieldTypes.push_back(llvm::ArrayType::get(
            llvm::Type::getInt8Ty(ctx), maxSize));
    }
    auto* temp = llvm::StructType::get(ctx, fieldTypes, false);
    const llvm::StructLayout* sl = dl.getStructLayout(temp);
    for (unsigned i = 0; i < fieldTypes.size(); ++i) {
        lr.fields.push_back({fieldTypes[i], sl->getElementOffset(i)});
    }
    lr.size = sl->getSizeInBytes();
    lr.align = sl->getAlignment().value();
    return lr;
}
