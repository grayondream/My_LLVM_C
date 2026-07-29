#include <gtest/gtest.h>
#include "codegen/CodegenContext.h"
#include "ast/Expr.h"
#include "ast/Stmt.h"
#include "ast/Decl.h"
#include "ast/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"

class CodegenContextTest : public ::testing::Test {
protected:
    void SetUp() override {
        ctx = std::make_unique<CodegenContext>();
        typeCtx = &TypeContext::instance();
        setupEntryBlock();
    }

    void TearDown() override {
        finalizeEntryBlock();
        ctx.reset();
    }

    void setupEntryBlock() {
        llvm::FunctionType* fnType = llvm::FunctionType::get(
            llvm::Type::getVoidTy(ctx->getContext()), false);
        llvm::Function* fn = llvm::Function::Create(
            fnType, llvm::Function::ExternalLinkage, "__test_entry", ctx->getModule());
        entryBB = llvm::BasicBlock::Create(ctx->getContext(), "entry", fn);
        ctx->getBuilder().SetInsertPoint(entryBB);
    }

    void finalizeEntryBlock() {
        if (entryBB && !entryBB->getTerminator()) {
            auto savedIP = ctx->getBuilder().saveIP();
            ctx->getBuilder().SetInsertPoint(entryBB);
            ctx->getBuilder().CreateRetVoid();
            ctx->getBuilder().restoreIP(savedIP);
        }
    }

    bool verifyModule() {
        finalizeEntryBlock();
        std::string err;
        llvm::raw_string_ostream os(err);
        bool broken = llvm::verifyModule(ctx->getModule(), &os);
        if (broken) {
            os.flush();
            ADD_FAILURE() << "Module verification failed: " << err;
        }
        return !broken;
    }

    std::unique_ptr<CodegenContext> ctx;
    TypeContext* typeCtx;
    llvm::BasicBlock* entryBB{nullptr};
};

// ============================================================
// Expression Codegen Tests
// ============================================================

TEST_F(CodegenContextTest, NumberExpr) {
    auto expr = std::make_unique<NumberExprAST>(42);
    llvm::Value* val = expr->codegen(*ctx);
    ASSERT_NE(val, nullptr);
    auto* ci = llvm::dyn_cast<llvm::ConstantInt>(val);
    ASSERT_NE(ci, nullptr);
    EXPECT_EQ(ci->getSExtValue(), 42);
}

TEST_F(CodegenContextTest, FloatExpr) {
    auto expr = std::make_unique<FloatExprAST>(3.14);
    llvm::Value* val = expr->codegen(*ctx);
    ASSERT_NE(val, nullptr);
    auto* cf = llvm::dyn_cast<llvm::ConstantFP>(val);
    ASSERT_NE(cf, nullptr);
    EXPECT_DOUBLE_EQ(cf->getValueAPF().convertToDouble(), 3.14);
}

TEST_F(CodegenContextTest, CharExpr) {
    auto expr = std::make_unique<CharExprAST>('A');
    llvm::Value* val = expr->codegen(*ctx);
    ASSERT_NE(val, nullptr);
    auto* ci = llvm::dyn_cast<llvm::ConstantInt>(val);
    ASSERT_NE(ci, nullptr);
    EXPECT_EQ(ci->getZExtValue(), static_cast<uint64_t>('A'));
}

TEST_F(CodegenContextTest, StringExpr) {
    auto expr = std::make_unique<StringExprAST>("hello");
    llvm::Value* val = expr->codegen(*ctx);
    ASSERT_NE(val, nullptr);
    EXPECT_TRUE(val->getType()->isPointerTy());
}

TEST_F(CodegenContextTest, BinaryExprAdd) {
    auto left = std::make_unique<NumberExprAST>(3);
    auto right = std::make_unique<NumberExprAST>(4);
    auto bin = std::make_unique<BinaryExprAST>(BinaryOp::Add, std::move(left), std::move(right));
    llvm::Value* val = bin->codegen(*ctx);
    ASSERT_NE(val, nullptr);
    auto* ci = llvm::dyn_cast<llvm::ConstantInt>(val);
    ASSERT_NE(ci, nullptr);
    EXPECT_EQ(ci->getSExtValue(), 7);
}

TEST_F(CodegenContextTest, BinaryExprComparison) {
    auto left = std::make_unique<NumberExprAST>(5);
    auto right = std::make_unique<NumberExprAST>(3);
    auto lt = std::make_unique<BinaryExprAST>(BinaryOp::Lt, std::move(left), std::move(right));
    llvm::Value* val = lt->codegen(*ctx);
    ASSERT_NE(val, nullptr);
    EXPECT_TRUE(val->getType()->isIntegerTy(1));
}

TEST_F(CodegenContextTest, UnaryExprNegate) {
    auto expr = std::make_unique<NumberExprAST>(5);
    auto neg = std::make_unique<UnaryExprAST>(UnaryOp::Minus, std::move(expr));
    llvm::Value* val = neg->codegen(*ctx);
    ASSERT_NE(val, nullptr);
}

TEST_F(CodegenContextTest, CommaExpr) {
    auto left = std::make_unique<NumberExprAST>(1);
    auto right = std::make_unique<NumberExprAST>(2);
    auto comma = std::make_unique<CommaExprAST>(std::move(left), std::move(right));
    llvm::Value* val = comma->codegen(*ctx);
    ASSERT_NE(val, nullptr);
    auto* ci = llvm::dyn_cast<llvm::ConstantInt>(val);
    ASSERT_NE(ci, nullptr);
    EXPECT_EQ(ci->getSExtValue(), 2);
}

TEST_F(CodegenContextTest, SizeofExpr) {
    auto sz = std::make_unique<SizeofExprAST>(typeCtx->getInt());
    llvm::Value* val = sz->codegen(*ctx);
    ASSERT_NE(val, nullptr);
    auto* ci = llvm::dyn_cast<llvm::ConstantInt>(val);
    ASSERT_NE(ci, nullptr);
    EXPECT_EQ(ci->getZExtValue(), 4u);
}

TEST_F(CodegenContextTest, SizeofPointer) {
    auto ptrType = new Type(TypeKind::Pointer, typeCtx->getInt());
    auto sz = std::make_unique<SizeofExprAST>(ptrType);
    llvm::Value* val = sz->codegen(*ctx);
    ASSERT_NE(val, nullptr);
    auto* ci = llvm::dyn_cast<llvm::ConstantInt>(val);
    ASSERT_NE(ci, nullptr);
    EXPECT_EQ(ci->getZExtValue(), 8u);
}

TEST_F(CodegenContextTest, TernaryExpr) {
    llvm::FunctionType* fnType = llvm::FunctionType::get(
        llvm::Type::getVoidTy(ctx->getContext()), false);
    llvm::Function* fn = llvm::Function::Create(
        fnType, llvm::Function::ExternalLinkage, "test_ternary", ctx->getModule());
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(ctx->getContext(), "entry", fn);
    ctx->getBuilder().SetInsertPoint(bb);

    auto cond = std::make_unique<NumberExprAST>(1);
    auto then = std::make_unique<NumberExprAST>(10);
    auto else_ = std::make_unique<NumberExprAST>(20);
    auto ternary = std::make_unique<TernaryExprAST>(
        std::move(cond), std::move(then), std::move(else_));
    llvm::Value* val = ternary->codegen(*ctx);
    ASSERT_NE(val, nullptr);
    ctx->getBuilder().CreateRetVoid();
    EXPECT_TRUE(verifyModule());
}

TEST_F(CodegenContextTest, CastExprIntToFloat) {
    auto expr = std::make_unique<NumberExprAST>(42);
    auto cast = std::make_unique<CastExprAST>(typeCtx->getFloat(), std::move(expr));
    llvm::Value* val = cast->codegen(*ctx);
    ASSERT_NE(val, nullptr);
    EXPECT_TRUE(val->getType()->isFloatTy());
    EXPECT_TRUE(verifyModule());
}

TEST_F(CodegenContextTest, CastExprIntToChar) {
    auto expr = std::make_unique<NumberExprAST>(256);
    auto cast = std::make_unique<CastExprAST>(typeCtx->getChar(), std::move(expr));
    llvm::Value* val = cast->codegen(*ctx);
    ASSERT_NE(val, nullptr);
    EXPECT_TRUE(val->getType()->isIntegerTy(8));
    EXPECT_TRUE(verifyModule());
}

TEST_F(CodegenContextTest, AssignmentExpr) {
    auto* alloca = ctx->getBuilder().CreateAlloca(
        llvm::Type::getInt32Ty(ctx->getContext()), nullptr, "x");
    ctx->declareVariable("x", alloca, typeCtx->getInt());

    auto lhs = std::make_unique<VariableExprAST>("x");
    auto rhs = std::make_unique<NumberExprAST>(42);
    auto assign = std::make_unique<AssignmentExprAST>(
        AssignOp::Assign, std::move(lhs), std::move(rhs));
    llvm::Value* val = assign->codegen(*ctx);
    ASSERT_NE(val, nullptr);
    EXPECT_TRUE(verifyModule());
}

TEST_F(CodegenContextTest, CompoundAssignmentExpr) {
    auto* alloca = ctx->getBuilder().CreateAlloca(
        llvm::Type::getInt32Ty(ctx->getContext()), nullptr, "x");
    ctx->declareVariable("x", alloca, typeCtx->getInt());
    ctx->getBuilder().CreateStore(
        llvm::ConstantInt::get(ctx->getContext(), llvm::APInt(32, 10)), alloca);

    auto lhs = std::make_unique<VariableExprAST>("x");
    auto rhs = std::make_unique<NumberExprAST>(5);
    auto addAssign = std::make_unique<AssignmentExprAST>(
        AssignOp::AddAssign, std::move(lhs), std::move(rhs));
    llvm::Value* val = addAssign->codegen(*ctx);
    ASSERT_NE(val, nullptr);
    EXPECT_TRUE(verifyModule());
}

TEST_F(CodegenContextTest, ArrayAccessExpr) {
    llvm::Type* i32 = llvm::Type::getInt32Ty(ctx->getContext());
    llvm::ArrayType* arrType = llvm::ArrayType::get(i32, 5);
    auto* alloca = ctx->getBuilder().CreateAlloca(arrType, nullptr, "arr");

    auto* arrVarType = new ArrayType(typeCtx->getInt(), 5);
    ctx->declareVariable("arr", alloca, arrVarType);

    auto arrVar = std::make_unique<VariableExprAST>("arr");
    arrVar->type = arrVarType;
    auto idx = std::make_unique<NumberExprAST>(2);
    auto access = std::make_unique<ArrayAccessExprAST>(std::move(arrVar), std::move(idx));

    llvm::Value* val = access->codegen(*ctx);
    ASSERT_NE(val, nullptr);
    EXPECT_TRUE(val->getType()->isPointerTy());
    EXPECT_TRUE(verifyModule());
}

TEST_F(CodegenContextTest, MemberAccessExpr) {
    llvm::FunctionType* fnType = llvm::FunctionType::get(
        llvm::Type::getVoidTy(ctx->getContext()), false);
    llvm::Function* fn = llvm::Function::Create(
        fnType, llvm::Function::ExternalLinkage, "test_member", ctx->getModule());
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(ctx->getContext(), "entry", fn);
    ctx->getBuilder().SetInsertPoint(bb);

    auto* structType = new StructType("Point");
    structType->addField("x", typeCtx->getInt());
    structType->addField("y", typeCtx->getInt());

    llvm::Type* i32 = llvm::Type::getInt32Ty(ctx->getContext());
    llvm::StructType* llvmStruct = llvm::StructType::create(
        ctx->getContext(), {i32, i32}, "Point");
    auto* alloca = ctx->getBuilder().CreateAlloca(llvmStruct, nullptr, "p");
    ctx->declareVariable("p", alloca, structType);

    auto obj = std::make_unique<VariableExprAST>("p");
    obj->type = structType;
    auto access = std::make_unique<MemberAccessExprAST>(
        MemberAccessKind::Dot, std::move(obj), "y");
    llvm::Value* val = access->codegen(*ctx);
    ASSERT_NE(val, nullptr);
    EXPECT_TRUE(val->getType()->isPointerTy());
    ctx->getBuilder().CreateRetVoid();
    EXPECT_TRUE(verifyModule());
}

// ============================================================
// Statement Codegen Tests
// ============================================================

TEST_F(CodegenContextTest, NullStmt) {
    auto nullStmt = std::make_unique<NullStmtAST>();
    llvm::Value* val = nullStmt->codegen(*ctx);
    EXPECT_EQ(val, nullptr);
}

TEST_F(CodegenContextTest, ExprStmt) {
    auto expr = std::make_unique<NumberExprAST>(42);
    auto stmt = std::make_unique<ExprStmtAST>(std::move(expr));
    llvm::Value* val = stmt->codegen(*ctx);
    EXPECT_EQ(val, nullptr);
    EXPECT_TRUE(verifyModule());
}

TEST_F(CodegenContextTest, IfStmt) {
    std::vector<std::unique_ptr<StmtAST>> thenStmts;
    thenStmts.push_back(std::make_unique<ExprStmtAST>(std::make_unique<NumberExprAST>(1)));
    auto cond = std::make_unique<NumberExprAST>(1);
    auto then = std::make_unique<CompoundStmtAST>(std::move(thenStmts));
    auto ifStmt = std::make_unique<IfStmtAST>(
        std::move(cond), std::move(then));
    ifStmt->codegen(*ctx);
    ctx->getBuilder().CreateRetVoid();
    EXPECT_TRUE(verifyModule());
}

TEST_F(CodegenContextTest, IfElseStmt) {
    std::vector<std::unique_ptr<StmtAST>> thenStmts;
    thenStmts.push_back(std::make_unique<ExprStmtAST>(std::make_unique<NumberExprAST>(1)));
    std::vector<std::unique_ptr<StmtAST>> elseStmts;
    elseStmts.push_back(std::make_unique<ExprStmtAST>(std::make_unique<NumberExprAST>(0)));

    auto cond = std::make_unique<NumberExprAST>(0);
    auto then = std::make_unique<CompoundStmtAST>(std::move(thenStmts));
    auto else_ = std::make_unique<CompoundStmtAST>(std::move(elseStmts));
    auto ifStmt = std::make_unique<IfStmtAST>(
        std::move(cond), std::move(then), std::move(else_));
    ifStmt->codegen(*ctx);
    ctx->getBuilder().CreateRetVoid();
    EXPECT_TRUE(verifyModule());
}

TEST_F(CodegenContextTest, WhileStmt) {
    auto cond = std::make_unique<NumberExprAST>(0);
    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    bodyStmts.push_back(std::make_unique<ExprStmtAST>(std::make_unique<NumberExprAST>(1)));
    auto body = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));
    auto whileStmt = std::make_unique<WhileStmtAST>(
        std::move(cond), std::move(body));
    whileStmt->codegen(*ctx);
    ctx->getBuilder().CreateRetVoid();
    EXPECT_TRUE(verifyModule());
}

TEST_F(CodegenContextTest, DoWhileStmt) {
    llvm::FunctionType* fnType = llvm::FunctionType::get(
        llvm::Type::getVoidTy(ctx->getContext()), false);
    llvm::Function* fn = llvm::Function::Create(
        fnType, llvm::Function::ExternalLinkage, "test_dowhile", ctx->getModule());
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(ctx->getContext(), "entry", fn);
    ctx->getBuilder().SetInsertPoint(bb);

    auto cond = std::make_unique<NumberExprAST>(0);
    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    bodyStmts.push_back(std::make_unique<ExprStmtAST>(std::make_unique<NumberExprAST>(1)));
    auto body = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));
    auto doWhile = std::make_unique<DoWhileStmtAST>(
        std::move(cond), std::move(body));
    doWhile->codegen(*ctx);
    ctx->getBuilder().CreateRetVoid();
    EXPECT_TRUE(verifyModule());
}

TEST_F(CodegenContextTest, ForStmt) {
    auto init = std::make_unique<NullStmtAST>();
    auto cond = std::make_unique<NumberExprAST>(0);
    auto inc = std::make_unique<NumberExprAST>(0);
    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    bodyStmts.push_back(std::make_unique<ExprStmtAST>(std::make_unique<NumberExprAST>(1)));
    auto body = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));
    auto forStmt = std::make_unique<ForStmtAST>(
        std::move(init), std::move(cond), std::move(inc), std::move(body));
    forStmt->codegen(*ctx);
    ctx->getBuilder().CreateRetVoid();
    EXPECT_TRUE(verifyModule());
}

TEST_F(CodegenContextTest, SwitchStmt) {
    auto cond = std::make_unique<NumberExprAST>(1);
    std::vector<std::unique_ptr<StmtAST>> cases;
    std::vector<std::unique_ptr<StmtAST>> case0Stmts;
    case0Stmts.push_back(std::make_unique<ExprStmtAST>(std::make_unique<NumberExprAST>(10)));
    cases.push_back(std::make_unique<CompoundStmtAST>(std::move(case0Stmts)));
    std::vector<std::unique_ptr<StmtAST>> case1Stmts;
    case1Stmts.push_back(std::make_unique<ExprStmtAST>(std::make_unique<NumberExprAST>(20)));
    cases.push_back(std::make_unique<CompoundStmtAST>(std::move(case1Stmts)));

    auto switchStmt = std::make_unique<SwitchStmtAST>(
        std::move(cond), std::move(cases));
    switchStmt->caseValues.push_back(
        llvm::ConstantInt::get(ctx->getContext(), llvm::APInt(32, 0)));
    switchStmt->caseValues.push_back(
        llvm::ConstantInt::get(ctx->getContext(), llvm::APInt(32, 1)));
    switchStmt->codegen(*ctx);
    ctx->getBuilder().CreateRetVoid();
    EXPECT_TRUE(verifyModule());
}

TEST_F(CodegenContextTest, BreakStmtNoContext) {
    auto breakStmt = std::make_unique<BreakStmtAST>();
    llvm::Value* val = breakStmt->codegen(*ctx);
    ASSERT_NE(val, nullptr);
}

TEST_F(CodegenContextTest, ContinueStmtNoContext) {
    auto contStmt = std::make_unique<ContinueStmtAST>();
    llvm::Value* val = contStmt->codegen(*ctx);
    ASSERT_NE(val, nullptr);
}

TEST_F(CodegenContextTest, LabelAndGoto) {
    llvm::FunctionType* fnType = llvm::FunctionType::get(
        llvm::Type::getVoidTy(ctx->getContext()), false);
    llvm::Function* fn = llvm::Function::Create(
        fnType, llvm::Function::ExternalLinkage, "test_goto", ctx->getModule());
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(ctx->getContext(), "entry", fn);
    ctx->getBuilder().SetInsertPoint(bb);

    auto gotoStmt = std::make_unique<GotoStmtAST>("target");
    gotoStmt->codegen(*ctx);

    auto labelStmt = std::make_unique<LabelStmtAST>(
        "target", std::make_unique<NullStmtAST>());
    labelStmt->codegen(*ctx);
    ctx->getBuilder().CreateRetVoid();
    EXPECT_TRUE(verifyModule());
}

TEST_F(CodegenContextTest, ReturnStmt) {
    llvm::FunctionType* fnType = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(ctx->getContext()), false);
    llvm::Function* fn = llvm::Function::Create(
        fnType, llvm::Function::ExternalLinkage, "test_return", ctx->getModule());
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(ctx->getContext(), "entry", fn);
    ctx->getBuilder().SetInsertPoint(bb);

    auto ret = std::make_unique<ReturnStmtAST>(std::make_unique<NumberExprAST>(42));
    ret->codegen(*ctx);
    EXPECT_TRUE(verifyModule());
}

TEST_F(CodegenContextTest, CompoundStmt) {
    std::vector<std::unique_ptr<StmtAST>> stmts;
    stmts.push_back(std::make_unique<ExprStmtAST>(std::make_unique<NumberExprAST>(1)));
    stmts.push_back(std::make_unique<ExprStmtAST>(std::make_unique<NumberExprAST>(2)));
    auto compound = std::make_unique<CompoundStmtAST>(std::move(stmts));
    llvm::Value* val = compound->codegen(*ctx);
    ctx->getBuilder().CreateRetVoid();
    EXPECT_TRUE(verifyModule());
}

// ============================================================
// Declaration Codegen Tests
// ============================================================

TEST_F(CodegenContextTest, VarDecl) {
    auto varDecl = std::make_unique<VarDeclAST>("x", typeCtx->getInt());
    llvm::Value* val = varDecl->codegen(*ctx);
    ASSERT_NE(val, nullptr);
    ctx->getBuilder().CreateRetVoid();
    EXPECT_TRUE(verifyModule());
}

TEST_F(CodegenContextTest, VarDeclWithInit) {
    auto varDecl = std::make_unique<VarDeclAST>(
        "x", typeCtx->getInt(), std::make_unique<NumberExprAST>(42));
    llvm::Value* val = varDecl->codegen(*ctx);
    ASSERT_NE(val, nullptr);
    ctx->getBuilder().CreateRetVoid();
    EXPECT_TRUE(verifyModule());
}

TEST_F(CodegenContextTest, ArrayDecl) {
    auto arrDecl = std::make_unique<ArrayDeclAST>("arr", typeCtx->getInt(), 10);
    llvm::Value* val = arrDecl->codegen(*ctx);
    ASSERT_NE(val, nullptr);
    EXPECT_TRUE(val->getType()->isPointerTy());
    ctx->getBuilder().CreateRetVoid();
    EXPECT_TRUE(verifyModule());
}

TEST_F(CodegenContextTest, StructDecl) {
    std::vector<std::pair<std::string, Type*>> fields;
    fields.push_back({"x", typeCtx->getInt()});
    fields.push_back({"y", typeCtx->getInt()});
    auto structDecl = std::make_unique<StructDeclAST>("Point", std::move(fields));
    llvm::Value* val = structDecl->codegen(*ctx);
    EXPECT_TRUE(verifyModule());
}

TEST_F(CodegenContextTest, EnumDecl) {
    std::vector<std::pair<std::string, int>> values;
    values.push_back({"RED", 0});
    values.push_back({"GREEN", 1});
    values.push_back({"BLUE", 2});
    auto enumDecl = std::make_unique<EnumDeclAST>("Color", std::move(values));
    llvm::Value* val = enumDecl->codegen(*ctx);
    EXPECT_TRUE(verifyModule());
}

TEST_F(CodegenContextTest, TypedefDecl) {
    auto typedefDecl = std::make_unique<TypedefDeclAST>("MyInt", typeCtx->getInt());
    llvm::Value* val = typedefDecl->codegen(*ctx);
    EXPECT_TRUE(verifyModule());
}

TEST_F(CodegenContextTest, ForwardDecl) {
    auto fwdDecl = std::make_unique<ForwardDeclAST>("MyStruct");
    llvm::Value* val = fwdDecl->codegen(*ctx);
    EXPECT_TRUE(verifyModule());
}

TEST_F(CodegenContextTest, DeclStmt) {
    auto varDecl = std::make_unique<VarDeclAST>("x", typeCtx->getInt());
    auto declStmt = std::make_unique<DeclStmtAST>(std::move(varDecl));
    llvm::Value* val = declStmt->codegen(*ctx);
    ASSERT_NE(val, nullptr);
    ctx->getBuilder().CreateRetVoid();
    EXPECT_TRUE(verifyModule());
}

TEST_F(CodegenContextTest, FunctionDecl) {
    std::vector<std::unique_ptr<ParamDeclAST>> params;
    params.push_back(std::make_unique<ParamDeclAST>("a", typeCtx->getInt()));

    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    bodyStmts.push_back(std::make_unique<ReturnStmtAST>(
        std::make_unique<VariableExprAST>("a")));
    auto body = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));

    auto funcDecl = std::make_unique<FunctionDeclAST>(
        "identity", typeCtx->getInt(), params, body);
    llvm::Value* val = funcDecl->codegen(*ctx);
    ASSERT_NE(val, nullptr);
    auto* func = llvm::cast<llvm::Function>(val);
    EXPECT_EQ(func->arg_size(), 1u);
    EXPECT_TRUE(verifyModule());
}

TEST_F(CodegenContextTest, TranslationUnit) {
    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<VarDeclAST>("x", typeCtx->getInt()));
    decls.push_back(std::make_unique<VarDeclAST>("y", typeCtx->getFloat()));
    auto tu = std::make_unique<TranslationUnitAST>(std::move(decls));
    llvm::Value* val = tu->codegen(*ctx);
    ctx->getBuilder().CreateRetVoid();
    EXPECT_TRUE(verifyModule());
}

// ============================================================
// Break/Continue in Loop Tests
// ============================================================

TEST_F(CodegenContextTest, BreakInWhile) {
    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    bodyStmts.push_back(std::make_unique<BreakStmtAST>());
    auto body = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));
    auto cond = std::make_unique<NumberExprAST>(1);
    auto whileStmt = std::make_unique<WhileStmtAST>(
        std::move(cond), std::move(body));
    whileStmt->codegen(*ctx);
    ctx->getBuilder().CreateRetVoid();
    EXPECT_TRUE(verifyModule());
}

TEST_F(CodegenContextTest, ContinueInFor) {
    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    bodyStmts.push_back(std::make_unique<ContinueStmtAST>());
    auto body = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));
    auto init = std::make_unique<NullStmtAST>();
    auto cond = std::make_unique<NumberExprAST>(1);
    auto inc = std::make_unique<NumberExprAST>(0);
    auto forStmt = std::make_unique<ForStmtAST>(
        std::move(init), std::move(cond), std::move(inc), std::move(body));
    forStmt->codegen(*ctx);
    ctx->getBuilder().CreateRetVoid();
    EXPECT_TRUE(verifyModule());
}

// ============================================================
// Scope Tests
// ============================================================

TEST_F(CodegenContextTest, ScopePushPop) {
    ctx->pushScope();
    EXPECT_NE(ctx->currentScope(), nullptr);
    ctx->popScope();
    EXPECT_NE(ctx->currentScope(), nullptr);
}

TEST_F(CodegenContextTest, VariableLookup) {
    auto* alloca = ctx->getBuilder().CreateAlloca(
        llvm::Type::getInt32Ty(ctx->getContext()), nullptr, "x");
    ctx->declareVariable("x", alloca, typeCtx->getInt());

    llvm::Value* val = ctx->lookupVariable("x");
    ASSERT_NE(val, nullptr);
}

TEST_F(CodegenContextTest, VariableLookupNestedScope) {
    auto* alloca = ctx->getBuilder().CreateAlloca(
        llvm::Type::getInt32Ty(ctx->getContext()), nullptr, "x");
    ctx->declareVariable("x", alloca, typeCtx->getInt());

    ctx->pushScope();
    llvm::Value* val = ctx->lookupVariable("x");
    ASSERT_NE(val, nullptr);
    ctx->popScope();
}

// ============================================================
// Type Conversion Tests
// ============================================================

TEST_F(CodegenContextTest, GetLLVMTypeInt) {
    llvm::Type* t = ctx->getLLVMType(typeCtx->getInt());
    EXPECT_TRUE(t->isIntegerTy(32));
}

TEST_F(CodegenContextTest, GetLLVMTypeFloat) {
    llvm::Type* t = ctx->getLLVMType(typeCtx->getFloat());
    EXPECT_TRUE(t->isFloatTy());
}

TEST_F(CodegenContextTest, GetLLVMTypeDouble) {
    llvm::Type* t = ctx->getLLVMType(typeCtx->getDouble());
    EXPECT_TRUE(t->isDoubleTy());
}

TEST_F(CodegenContextTest, GetLLVMTypeChar) {
    llvm::Type* t = ctx->getLLVMType(typeCtx->getChar());
    EXPECT_TRUE(t->isIntegerTy(8));
}

TEST_F(CodegenContextTest, GetLLVMTypeVoid) {
    llvm::Type* t = ctx->getLLVMType(typeCtx->getVoid());
    EXPECT_TRUE(t->isVoidTy());
}

TEST_F(CodegenContextTest, GetLLVMTypePointer) {
    auto* ptrType = new Type(TypeKind::Pointer, typeCtx->getInt());
    llvm::Type* t = ctx->getLLVMType(ptrType);
    EXPECT_TRUE(t->isPointerTy());
}
