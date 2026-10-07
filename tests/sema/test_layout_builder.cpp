// P1-05 / ANN-02/03: LayoutBuilder 单源化——聚合布局构造的唯一权威。
// 无注解路径 = 迁移前行为逐字节一致（回归 pin）。

#include <gtest/gtest.h>

#include "ast/LayoutBuilder.h"
#include "ast/Type.h"
#include "codegen/CodegenContext.h"

#include <spdlog/spdlog.h>

class LayoutBuilderTest : public ::testing::Test {
protected:
    void SetUp() override {
        spdlog::set_level(spdlog::level::off);
    }

    CodegenContext cc;
    LayoutBuilder::TypeConverter conv = [this](Type* t) { return cc.getLLVMType(t); };
    std::unordered_map<std::string, Type*> named;
    LayoutBuilder::NameResolver resolve = [this](const std::string& n) -> Type* {
        auto it = named.find(n);
        return it == named.end() ? nullptr : it->second;
    };
};

TEST_F(LayoutBuilderTest, NoAnnotationLayoutUnchanged) {
    auto* S = new StructType("LT_S");
    S->addField("a", TypeContext::instance().getInt32());
    S->addField("b", TypeContext::instance().getFloat64());
    S->addField("c", TypeContext::instance().getInt16());
    auto LR = LayoutBuilder::buildAggregate(S, cc.getContext(),
                                            cc.getModule().getDataLayout(), conv, resolve);
    ASSERT_EQ(LR.fields.size(), 3u);
    EXPECT_EQ(LR.fields[0].offset, 0u);
    EXPECT_EQ(LR.fields[1].offset, 8u);
    EXPECT_EQ(LR.fields[2].offset, 16u);
    EXPECT_EQ(LR.size, 24u);
    EXPECT_EQ(LR.align, 8u);
    EXPECT_FALSE(LR.isPacked);
}

TEST_F(LayoutBuilderTest, UnionLayoutUnchanged) {
    auto* U = new UnionType("LT_U");
    U->addMember("i", TypeContext::instance().getInt32());
    U->addMember("f", TypeContext::instance().getFloat64());
    auto LR = LayoutBuilder::buildUnion(U, cc.getContext(),
                                        cc.getModule().getDataLayout(), conv);
    EXPECT_EQ(LR.size, 8u);
    EXPECT_EQ(LR.align, 8u);
    ASSERT_FALSE(LR.fields.empty());
    EXPECT_EQ(LR.fields[0].offset, 0u);
}

TEST_F(LayoutBuilderTest, ClassBaseSlotZero) {
    auto* B = new ClassType("LT_B");
    B->addField("b", TypeContext::instance().getInt32());
    named["LT_B"] = B;
    auto* D = new ClassType("LT_D");
    D->baseClass = "LT_B";
    D->addField("v", TypeContext::instance().getFloat64());
    auto LR = LayoutBuilder::buildAggregate(D, cc.getContext(),
                                            cc.getModule().getDataLayout(), conv, resolve);
    ASSERT_EQ(LR.fields.size(), 2u); // 基类槽 0 + v
    EXPECT_EQ(LR.fields[0].offset, 0u);
    EXPECT_EQ(LR.fields[1].offset, 8u);
    EXPECT_EQ(LR.size, 16u);
}
