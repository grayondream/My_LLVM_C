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

static uint64_t alignUp(uint64_t v, uint64_t a) {
    return a == 0 ? v : (v + a - 1) / a * a;
}

LayoutResult LayoutBuilder::buildAggregate(Type* agg, llvm::LLVMContext& ctx,
                                           const llvm::DataLayout& dl,
                                           const TypeConverter& conv,
                                           const NameResolver& resolve) {
    LayoutResult lr;
    const std::string* baseName = nullptr;
    const std::vector<FieldInfo>* flds = nullptr;
    bool materialized = false;
    uint64_t forcedAlign = 0;
    const std::unordered_map<std::string, uint64_t>* fieldAligns = nullptr;
    if (agg->kind == TypeKind::Struct) {
        auto* st = static_cast<StructType*>(agg);
        baseName = &st->baseClass;
        flds = &st->fields;
        lr.isPacked = st->isPacked;
        materialized = st->layoutMaterialized;
        forcedAlign = st->forcedAlign;
        fieldAligns = &st->fieldAligns;
    } else if (agg->kind == TypeKind::Class) {
        auto* ct = static_cast<ClassType*>(agg);
        baseName = &ct->baseClass;
        flds = &ct->fields;
        lr.isPacked = ct->isPacked;
        materialized = ct->layoutMaterialized;
        forcedAlign = ct->forcedAlign;
        fieldAligns = &ct->fieldAligns;
    } else {
        llvm::report_fatal_error("LayoutBuilder::buildAggregate: not an aggregate");
    }

    bool annotated = lr.isPacked || forcedAlign || (fieldAligns && !fieldAligns->empty());

    // P1-05 / ANN-03: 注解布局——首次构造时把目标偏移差以显式 i8 padding
    // 伪字段（name=""）物化进 Type 侧字段序列（幂等）；GEP 按名查索引自然
    // 覆盖伪字段，size/offset 查询与 IR 布局保持一致。
    std::vector<uint64_t> targetOffsets;
    uint64_t structAlign = 1;
    if (!materialized && annotated) {
        std::vector<FieldInfo> padded;
        if (!baseName->empty() && resolve) {
            if (Type* baseType = resolve(*baseName)) {
                if (llvm::Type* bt = conv(baseType)) {
                    padded.push_back(FieldInfo{"", baseType, {}});
                    structAlign = std::max<uint64_t>(
                        structAlign, dl.getABITypeAlign(bt).value());
                }
            }
        }
        uint64_t cursor = 0;
        for (auto& f : *flds) {
            llvm::Type* ft = conv(f.type);
            if (!ft) continue;
            uint64_t sz = dl.getTypeAllocSize(ft);
            uint64_t nat = dl.getABITypeAlign(ft).value();
            uint64_t ta = nat;
            if (lr.isPacked) ta = 1;
            auto it = fieldAligns->find(f.name);
            if (it != fieldAligns->end()) ta = std::max(ta, it->second);
            structAlign = std::max(structAlign, ta);
            uint64_t off = alignUp(cursor, ta);
            if (off > cursor) {
                padded.push_back(FieldInfo{
                    "", new ArrayType(TypeContext::instance().getInt8(),
                                      static_cast<int>(off - cursor)),
                    {}});
            }
            padded.push_back(f);
            targetOffsets.push_back(off);
            cursor = off + sz;
        }
        if (forcedAlign) {
            structAlign = std::max(structAlign, forcedAlign);
        }
        uint64_t total = alignUp(cursor, structAlign);
        if (total > cursor) {
            padded.push_back(FieldInfo{
                "", new ArrayType(TypeContext::instance().getInt8(),
                                  static_cast<int>(total - cursor)),
                {}});
        }
        *const_cast<std::vector<FieldInfo>*>(flds) = std::move(padded);
        if (agg->kind == TypeKind::Struct) {
            static_cast<StructType*>(agg)->layoutMaterialized = true;
        } else {
            static_cast<ClassType*>(agg)->layoutMaterialized = true;
        }
    }

    std::vector<llvm::Type*> fieldTypes;
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
    lr.align = std::max<uint64_t>(sl->getAlignment().value(), forcedAlign);

    // 自检：注解模式下命名（非伪）字段的目标偏移必须等于 DataLayout 实算。
    if (!materialized && annotated && !targetOffsets.empty()) {
        size_t k = 0;
        const bool hasBase = !baseName->empty() && resolve;
        for (size_t i = 0; i < flds->size() && k < targetOffsets.size(); ++i) {
            const auto& f = (*flds)[i];
            if (f.name.empty()) continue;
            if (lr.fields[i + (hasBase ? 1 : 0)].offset != targetOffsets[k]) {
                llvm::report_fatal_error(
                    "LayoutBuilder: annotated layout self-check failed");
            }
            ++k;
        }
    }
    return lr;
}

LayoutResult LayoutBuilder::buildUnion(UnionType* ut, llvm::LLVMContext& ctx,
                                       const llvm::DataLayout& dl,
                                       const TypeConverter& conv) {
    LayoutResult lr;
    // 现状语义：最大成员块 + 对齐成员优先 + 尾部填充；成员偏移全 0。
    uint64_t maxSize = 0;
    uint64_t maxAlign = 0;
    const uint64_t forcedAlign = ut->forcedAlign;
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
    if (forcedAlign) {
        maxSize = alignUp(maxSize, forcedAlign);
        maxAlign = std::max(maxAlign, forcedAlign);
    }
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
    lr.align = std::max<uint64_t>(sl->getAlignment().value(), forcedAlign);
    return lr;
}
