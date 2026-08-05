#include <gtest/gtest.h>
#include "ast/Symbol.h"
#include "ast/Type.h"

class OverloadSetTest : public ::testing::Test {
protected:
    void SetUp() override {
        typeCtx = &TypeContext::instance();
    }

    TypeContext* typeCtx;
};

class ScopeTest : public ::testing::Test {
protected:
    void SetUp() override {
        typeCtx = &TypeContext::instance();
    }

    TypeContext* typeCtx;
};

TEST_F(OverloadSetTest, AddSingleSymbol) {
    OverloadSet overloadSet;
    Symbol sym("add", new FunctionType(typeCtx->getInt(), {typeCtx->getInt(), typeCtx->getInt()}));
    
    overloadSet.add(&sym);
    
    EXPECT_EQ(overloadSet.size(), 1u);
    EXPECT_FALSE(overloadSet.empty());
}

TEST_F(OverloadSetTest, AddMultipleSymbols) {
    OverloadSet overloadSet;
    Symbol sym1("add", new FunctionType(typeCtx->getInt(), {typeCtx->getInt(), typeCtx->getInt()}));
    Symbol sym2("add", new FunctionType(typeCtx->getFloat(), {typeCtx->getFloat(), typeCtx->getFloat()}));
    
    overloadSet.add(&sym1);
    overloadSet.add(&sym2);
    
    EXPECT_EQ(overloadSet.size(), 2u);
}

TEST_F(OverloadSetTest, ResolveExactMatch) {
    OverloadSet overloadSet;
    Symbol sym1("add", new FunctionType(typeCtx->getInt(), {typeCtx->getInt(), typeCtx->getInt()}));
    Symbol sym2("add", new FunctionType(typeCtx->getFloat(), {typeCtx->getFloat(), typeCtx->getFloat()}));
    
    overloadSet.add(&sym1);
    overloadSet.add(&sym2);
    
    Symbol* resolved = overloadSet.resolve({typeCtx->getInt(), typeCtx->getInt()});
    EXPECT_EQ(resolved, &sym1);
    
    resolved = overloadSet.resolve({typeCtx->getFloat(), typeCtx->getFloat()});
    EXPECT_EQ(resolved, &sym2);
}

TEST_F(OverloadSetTest, ResolveNoMatch) {
    OverloadSet overloadSet;
    Symbol sym1("add", new FunctionType(typeCtx->getInt(), {typeCtx->getInt(), typeCtx->getInt()}));
    
    overloadSet.add(&sym1);
    
    Symbol* resolved = overloadSet.resolve({typeCtx->getDouble(), typeCtx->getDouble()});
    EXPECT_EQ(resolved, nullptr);
}

TEST_F(OverloadSetTest, ResolveAmbiguous) {
    OverloadSet overloadSet;
    Symbol sym1("add", new FunctionType(typeCtx->getInt(), {typeCtx->getInt(), typeCtx->getInt()}));
    Symbol sym2("add", new FunctionType(typeCtx->getInt(), {typeCtx->getInt(), typeCtx->getInt()}));
    
    overloadSet.add(&sym1);
    overloadSet.add(&sym2);
    
    Symbol* resolved = overloadSet.resolve({typeCtx->getInt(), typeCtx->getInt()});
    EXPECT_EQ(resolved, nullptr);
}

TEST_F(OverloadSetTest, GetCandidates) {
    OverloadSet overloadSet;
    Symbol sym1("add", new FunctionType(typeCtx->getInt(), {typeCtx->getInt(), typeCtx->getInt()}));
    Symbol sym2("add", new FunctionType(typeCtx->getFloat(), {typeCtx->getFloat(), typeCtx->getFloat()}));
    
    overloadSet.add(&sym1);
    overloadSet.add(&sym2);
    
    const auto& candidates = overloadSet.getCandidates();
    EXPECT_EQ(candidates.size(), 2u);
    EXPECT_EQ(candidates[0], &sym1);
    EXPECT_EQ(candidates[1], &sym2);
}

TEST_F(OverloadSetTest, EmptyOverloadSet) {
    OverloadSet overloadSet;
    
    EXPECT_TRUE(overloadSet.empty());
    EXPECT_EQ(overloadSet.size(), 0u);
    EXPECT_EQ(overloadSet.getCandidates().size(), 0u);
    
    Symbol* resolved = overloadSet.resolve({typeCtx->getInt()});
    EXPECT_EQ(resolved, nullptr);
}

TEST_F(ScopeTest, LookupOverload) {
    Scope scope(nullptr);
    Symbol sym1("add", new FunctionType(typeCtx->getInt(), {typeCtx->getInt(), typeCtx->getInt()}));
    Symbol sym2("add", new FunctionType(typeCtx->getFloat(), {typeCtx->getFloat(), typeCtx->getFloat()}));
    
    scope.declare("add", &sym1);
    scope.declare("add", &sym2);
    
    OverloadSet* overloadSet = scope.lookupOverload("add");
    ASSERT_NE(overloadSet, nullptr);
    EXPECT_EQ(overloadSet->size(), 2u);
}

TEST_F(ScopeTest, DeclareRejectsDuplicateSignature) {
    Scope scope(nullptr);
    Symbol sym1("add", new FunctionType(typeCtx->getInt(), {typeCtx->getInt(), typeCtx->getInt()}));
    Symbol sym2("add", new FunctionType(typeCtx->getInt(), {typeCtx->getInt(), typeCtx->getInt()}));
    
    EXPECT_TRUE(scope.declare("add", &sym1));
    EXPECT_FALSE(scope.declare("add", &sym2));
    EXPECT_EQ(scope.lookupOverload("add")->size(), 1u);
}

TEST_F(ScopeTest, LookupReturnsFirstCandidate) {
    Scope scope(nullptr);
    Symbol sym1("add", new FunctionType(typeCtx->getInt(), {typeCtx->getInt(), typeCtx->getInt()}));
    Symbol sym2("add", new FunctionType(typeCtx->getFloat(), {typeCtx->getFloat(), typeCtx->getFloat()}));
    
    scope.declare("add", &sym1);
    scope.declare("add", &sym2);
    
    Symbol* found = scope.lookup("add");
    EXPECT_EQ(found, &sym1);
}

TEST_F(ScopeTest, LookupOverloadNotFound) {
    Scope scope(nullptr);
    
    OverloadSet* overloadSet = scope.lookupOverload("nonexistent");
    EXPECT_EQ(overloadSet, nullptr);
}
