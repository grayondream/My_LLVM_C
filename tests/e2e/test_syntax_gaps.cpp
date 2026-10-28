// 第三轮语法缺口回归：基础 C 声明/语句。
#include <gtest/gtest.h>

#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "sema/SemanticAnalyzer.h"
#include "codegen/CodegenContext.h"
#include "support/Log.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"

#include <spdlog/spdlog.h>

class SyntaxGapsE2E : public ::testing::Test {
protected:
    void SetUp() override {
        spdlog::set_level(spdlog::level::off);
        static bool init = false;
        if (!init) {
            llvm::InitializeAllTargetInfos();
            llvm::InitializeAllTargets();
            llvm::InitializeAllTargetMCs();
            llvm::InitializeAllAsmParsers();
            llvm::InitializeAllAsmPrinters();
            init = true;
        }
    }
};

static int runSyntaxSource(const std::string& source, const std::string& filename) {
    Lexer lexer(filename, source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    if (!ast || !parser.getErrors().empty()) {
        for (auto& err : parser.getErrors()) ADD_FAILURE() << err.format();
        return -1;
    }

    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    if (!analyzer.getErrors().empty()) {
        for (auto& err : analyzer.getErrors()) ADD_FAILURE() << err.format();
        return -1;
    }

    CodegenContext ctx;
    ctx.setSourceFile(filename);
    ast->codegen(ctx);
    ctx.finalizeDebugInfo();

    std::string ve;
    llvm::raw_string_ostream vs(ve);
    if (llvm::verifyModule(ctx.getModule(), &vs)) {
        ADD_FAILURE() << "invalid IR: " << vs.str();
        return -1;
    }

    auto jtmb = llvm::orc::JITTargetMachineBuilder::detectHost();
    if (!jtmb) return -1;
    auto jit = llvm::orc::LLJITBuilder()
        .setJITTargetMachineBuilder(std::move(*jtmb)).create();
    if (!jit) return -1;
    if (auto e = (*jit)->addIRModule(
            llvm::orc::ThreadSafeModule(ctx.takeModule(), ctx.takeContext()))) {
        return -1;
    }
    auto sym = (*jit)->lookup("main");
    if (!sym) return -1;
    return ((int (*)())(intptr_t)sym->getValue())();
}

TEST_F(SyntaxGapsE2E, EmptyStatement) {
    EXPECT_EQ(runSyntaxSource(R"(
        int32 main() {
            int32 x = 1;
            while (x < 1) ;
            if (x) ; else ;
            for (int32 i = 0; i < 1; i++) ;
            return x - 1;
        }
    )", "syntax_empty_stmt.c"), 0);
}

TEST_F(SyntaxGapsE2E, StructAndClassMultipleFields) {
    EXPECT_EQ(runSyntaxSource(R"(
        struct SMulti { int32 a, b; };
        class CMulti { public: int32 a, b; };
        int32 main() {
            struct SMulti s; s.a = 1; s.b = 2;
            CMulti c; c.a = 3; c.b = 4;
            return s.a + s.b + c.a + c.b - 10;
        }
    )", "syntax_multi_fields.c"), 0);
}

TEST_F(SyntaxGapsE2E, UnionMultipleFields) {
    EXPECT_EQ(runSyntaxSource(R"(
        union UMulti { int32 a, b; };
        int32 main() {
            union UMulti u; u.a = 5;
            if (sizeof(union UMulti) != 4) return 1;
            return u.a - 5;
        }
    )", "syntax_union_fields.c"), 0);
}

TEST_F(SyntaxGapsE2E, AggregateDirectFunctionPointerField) {
    EXPECT_EQ(runSyntaxSource(R"(
        int32 incFn(int32 x) { return x + 1; }
        int32 dblFn(int32 x) { return x * 2; }
        struct SFn { int32 (*f)(int32), (*g)(int32); };
        class CFn { public: int32 (*h)(int32); };
        int32 main() {
            struct SFn s; s.f = incFn; s.g = dblFn;
            CFn c; c.h = incFn;
            return s.f(1) + s.g(2) + c.h(3) - 10;
        }
    )", "syntax_fnptr_field.c"), 0);
}

TEST_F(SyntaxGapsE2E, AggregatePointerToArrayField) {
    EXPECT_EQ(runSyntaxSource(R"(
        struct SArr { int32 (*p)[3]; };
        int32 main() {
            int32 a[3] = {4, 5, 6};
            struct SArr s; s.p = &a;
            return (*s.p)[1] - 5;
        }
    )", "syntax_ptr_array_field.c"), 0);
}

TEST_F(SyntaxGapsE2E, SizeofAlignofFullTypeId) {
    EXPECT_EQ(runSyntaxSource(R"(
        int32 main() {
            if (sizeof(int32[3]) != 12) return 1;
            if (sizeof(int32[2][3]) != 24) return 2;
            if (sizeof(int32(*)(int32)) != 8) return 3;
            if (alignof(int32[3]) != 4) return 4;
            return 0;
        }
    )", "syntax_sizeof_typeid.c"), 0);
}

TEST_F(SyntaxGapsE2E, PostfixQualifiers) {
    EXPECT_EQ(runSyntaxSource(R"(
        int32 const g = 7;
        int32 main() {
            int32 const x = 1;
            int32 volatile v = 2;
            int32 const *p = &g;
            return x + v + *p - 10;
        }
    )", "syntax_postfix_qual.c"), 0);
}

TEST_F(SyntaxGapsE2E, BlockScopeExternRefersToGlobal) {
    EXPECT_EQ(runSyntaxSource(R"(
        int32 gExtern = 7;
        int32 main() {
            extern int32 gExtern;
            return gExtern - 7;
        }
    )", "syntax_block_extern.c"), 0);
}

TEST_F(SyntaxGapsE2E, RecordAttachedPointerAndFunction) {
    EXPECT_EQ(runSyntaxSource(R"(
        struct SAttach { int32 x; } *pAttach;
        struct SAttach { int32 x; } makeAttach() { struct SAttach s; s.x = 9; return s; }
        int32 main() {
            struct SAttach s; s.x = 4; pAttach = &s;
            return pAttach->x + makeAttach().x - 13;
        }
    )", "syntax_record_attached.c"), 0);
}

TEST_F(SyntaxGapsE2E, RecordAttachedFunctionPointerVariable) {
    EXPECT_EQ(runSyntaxSource(R"(
        struct SAttachFp { int32 x; } (*fpAttach)(int32);
        struct SAttachFp makeAttachFp(int32 v) {
            struct SAttachFp s; s.x = v; return s;
        }
        int32 main() {
            fpAttach = makeAttachFp;
            return fpAttach(3).x - 3;
        }
    )", "syntax_record_attached_fp.c"), 0);
}

TEST_F(SyntaxGapsE2E, RecordAttachedFunctionReturningPointer) {
    EXPECT_EQ(runSyntaxSource(R"(
        struct SAttachPtr { int32 x; } *makeAttachPtr() {
            static struct SAttachPtr s;
            s.x = 7;
            return &s;
        }
        int32 main() { return makeAttachPtr()->x - 7; }
    )", "syntax_record_attached_ptrfn.c"), 0);
}

TEST_F(SyntaxGapsE2E, ClassAttachedPointerKeepsMethods) {
    EXPECT_EQ(runSyntaxSource(R"(
        class CAttach { public: int32 x; int32 get() { return this->x; } } *pCAttach;
        int32 main() {
            CAttach c; c.x = 6; pCAttach = &c;
            return pCAttach->get() - 6;
        }
    )", "syntax_class_attached.c"), 0);
}

TEST_F(SyntaxGapsE2E, MultiDeclaratorComplexSuccessors) {
    EXPECT_EQ(runSyntaxSource(R"(
        int32 incMulti(int32 x) { return x + 1; }
        int32 dblMulti(int32 x) { return x * 2; }
        int32 (*fMulti)(int32), (*gMulti)(int32);
        int32 (*pMulti)[2], (*qMulti)[2];
        int32 main() {
            int32 a[2] = {1, 2};
            fMulti = incMulti; gMulti = dblMulti; pMulti = &a; qMulti = &a;
            return fMulti(1) + gMulti(2) + (*pMulti)[0] + (*qMulti)[1] - 9;
        }
    )", "syntax_multi_complex.c"), 0);
}

TEST_F(SyntaxGapsE2E, ArrayDimensionsAcceptConstExpressions) {
    EXPECT_EQ(runSyntaxSource(R"(
        enum { NArr = 3 };
        const int32 MArr = 2;
        struct SArrayDim { int32 a[1 + 1]; int32 b[NArr]; int32 c[MArr]; };
        int32 gArrayDim[1 + 1];
        int32 firstArrayDim(int32 a[1 + 1]) { return a[0]; }
        int32 main() {
            const int32 LArr = 4;
            int32 a[1 + 1];
            int32 b[NArr];
            int32 c[MArr];
            int32 d[LArr];
            int32 x[2] = {7, 8};
            struct SArrayDim s;
            if (sizeof(gArrayDim) != 8) return 1;
            if (sizeof(a) != 8) return 2;
            if (sizeof(b) != 12) return 3;
            if (sizeof(c) != 8) return 4;
            if (sizeof(d) != 16) return 5;
            if (sizeof(s.a) != 8 || sizeof(s.b) != 12 || sizeof(s.c) != 8) return 6;
            if (firstArrayDim(x) != 7) return 7;
            return 0;
        }
    )", "syntax_array_const_dim.c"), 0);
}

TEST_F(SyntaxGapsE2E, TypeIdArrayDimensionsAcceptConstExpressions) {
    EXPECT_EQ(runSyntaxSource(R"(
        enum { KArr = 2 };
        using AArray = int32[1 + 1];
        int32 main() {
            if (sizeof(AArray) != 8) return 1;
            if (sizeof(int32[1 + 1]) != 8) return 2;
            if (sizeof(int32[KArr]) != 8) return 3;
            return 0;
        }
    )", "syntax_typeid_array_const_dim.c"), 0);
}

TEST_F(SyntaxGapsE2E, MemberCallOperator) {
    EXPECT_EQ(runSyntaxSource(R"(
        class CMemberCall {
        public:
            int32 operator()(int32 x) { return x + 1; }
        };
        int32 main() {
            CMemberCall c;
            return c(1) - 2;
        }
    )", "syntax_member_operator_call.c"), 0);
}

TEST_F(SyntaxGapsE2E, MemberSubscriptOperatorRead) {
    EXPECT_EQ(runSyntaxSource(R"(
        class CMemberIndex {
        public:
            int32 data[3];
            int32 operator[](int32 i) { return this->data[i]; }
        };
        int32 main() {
            CMemberIndex c;
            c.data[0] = 5;
            return c[0] - 5;
        }
    )", "syntax_member_operator_index.c"), 0);
}

TEST_F(SyntaxGapsE2E, LeadingDotFloatAndLongLongSuffix) {
    EXPECT_EQ(runSyntaxSource(R"(
        float64 a = .5;
        int64 b = 1LL;
        uint64 c = 2ULL;
        int32 main() { return (a == .5 && b == 1 && c == 2) ? 0 : 1; }
    )", "syntax_c_literals.c"), 0);
}

TEST_F(SyntaxGapsE2E, AsmAdjacentEmptyOperandSections) {
    EXPECT_EQ(runSyntaxSource(R"(
        int32 main() {
            int32 x = 1;
            asm("nop" ::: "memory");
            return x - 1;
        }
    )", "syntax_asm_adjacent_colons.c"), 0);
}

TEST_F(SyntaxGapsE2E, OutOfClassStaticDataDefinition) {
    EXPECT_EQ(runSyntaxSource(R"(
        struct SStaticData { static int32 x; };
        int32 SStaticData::x = 7;
        int32 main() { return SStaticData::x - 7; }
    )", "syntax_static_data_ooc.c"), 0);
}

TEST_F(SyntaxGapsE2E, MultipleFunctionDeclarators) {
    EXPECT_EQ(runSyntaxSource(R"(
        int32 fMultiDecl(), gMultiDecl();
        int32 fMultiDecl() { return 1; }
        int32 gMultiDecl() { return 2; }
        int32 main() { return fMultiDecl() + gMultiDecl() - 3; }
    )", "syntax_multi_function_decl.c"), 0);
}

TEST_F(SyntaxGapsE2E, OutOfClassStaticMethodDefinition) {
    EXPECT_EQ(runSyntaxSource(R"(
        struct SStaticOoc { static int32 f(); };
        int32 SStaticOoc::f() { return 7; }
        int32 main() { return SStaticOoc::f() - 7; }
    )", "syntax_static_method_ooc.c"), 0);
}

TEST_F(SyntaxGapsE2E, FunctionTypeParameterDecaysToPointer) {
    EXPECT_EQ(runSyntaxSource(R"(
        int32 applyFnType(int32 gFnType(int32), int32 x);
        int32 applyFnType(int32 gFnType(int32), int32 x) { return gFnType(x); }
        int32 incFnType(int32 x) { return x + 1; }
        int32 main() { return applyFnType(incFnType, 1) - 2; }
    )", "syntax_function_type_param.c"), 0);
}

TEST_F(SyntaxGapsE2E, ParenthesizedDeclarators) {
    EXPECT_EQ(runSyntaxSource(R"(
        int32 (gParen) = 3;
        int32 (fParen)(void) { return 4; }
        int32 (aParen)[3];
        int32 main() {
            aParen[0] = 1;
            return gParen + fParen() + aParen[0] - 8;
        }
    )", "syntax_paren_declarator.c"), 0);
}

TEST_F(SyntaxGapsE2E, GlobalStringViewLiteralDoesNotCrash) {
    EXPECT_EQ(runSyntaxSource(R"(
        string_view gStringView = "abc";
        int32 main() {
            if (gStringView.len() != 3) return 1;
            if (gStringView[0] != 'a') return 2;
            return 0;
        }
    )", "syntax_global_string_view.c"), 0);
}

TEST_F(SyntaxGapsE2E, BitFieldWidthsAcceptEnumAndConst) {
    EXPECT_EQ(runSyntaxSource(R"(
        enum { WBits = 2 };
        const int32 VBits = 3;
        struct SBits { int32 a : WBits; int32 b : VBits; int32 c : 1 + 1; };
        int32 main() {
            struct SBits s;
            s.a = 1; s.b = 2; s.c = 1;
            if (s.a != 1 || s.b != 2 || s.c != 1) return 1;
            return 0;
        }
    )", "syntax_bitfield_const_width.c"), 0);
}
