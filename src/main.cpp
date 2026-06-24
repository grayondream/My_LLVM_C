#include <string>
#include "frontend/Token.h"
#include "support/Log.h"
#include "support/File.h"
#include "support/ScopeGuard.h"
#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "support/Utils.h"
#include "codegen/CodegenContext.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/Support/TargetSelect.h"

inline static constexpr const char* INPUT_C_FILE = RESOURCE_DIR "/main_min.c";

int main(int argc, char* argv[]){
    LOGI("start llvm c compiler");
    File file(INPUT_C_FILE);
    if(!file.isOpen()){
        LOGE("open file {} failed", INPUT_C_FILE);
        return -1;
    }

    ScopeGuard scope_guard = ScopeGuard([&file]() { file.close(); });
    std::string content = file.readAll();
    if(!content.empty()){
        LOGI("read file {} success", INPUT_C_FILE);
        LOGI("content:\n{}", content);
    }

    Token token(TokenType::TOKEN_IDENTIFIER, "x");
    LOGI("Token: {}", to_string(token));

    Lexer lexer(INPUT_C_FILE, content);
    auto tokens = lexer.tokenize();
    for(const auto& token : tokens) {
        LOGI("Token: {}", to_string(token));
    }

    Parser parser(tokens);
    auto ast = parser.parse();
    if(ast) {
        LOGI("ast: {}", to_string(*ast));
    } else {
        LOGE("parse ast failed");
    }

    if(ast) {
        CodegenContext codegenCtx;
        ast->codegen(codegenCtx);

        llvm::LoopAnalysisManager LAM;
        llvm::FunctionAnalysisManager FAM;
        llvm::CGSCCAnalysisManager CGAM;
        llvm::ModuleAnalysisManager MAM;

        llvm::PassBuilder PB;
        PB.registerModuleAnalyses(MAM);
        PB.registerCGSCCAnalyses(CGAM);
        PB.registerFunctionAnalyses(FAM);
        PB.registerLoopAnalyses(LAM);
        PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

        llvm::ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
        MPM.run(codegenCtx.getModule(), MAM);

        LOGI("generated llvm ir:");
        codegenCtx.getModule().print(llvm::outs(), nullptr);

        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();

        auto jtmb = llvm::orc::JITTargetMachineBuilder::detectHost();
        if (!jtmb) {
            LOGE("failed to detect host target: {}", llvm::toString(jtmb.takeError()));
        } else {
            auto dl = jtmb->getDefaultDataLayoutForTarget();
            if (dl) {
                codegenCtx.getModule().setDataLayout(*dl);
                codegenCtx.getModule().setTargetTriple(jtmb->getTargetTriple());
            }

            auto jit = llvm::orc::LLJITBuilder().create();
            if (!jit) {
                LOGE("failed to create jit: {}", llvm::toString(jit.takeError()));
            } else {
                auto tsModule = llvm::orc::ThreadSafeModule(
                    codegenCtx.takeModule(), codegenCtx.takeContext());
                if (auto err = (*jit)->addIRModule(std::move(tsModule))) {
                    LOGE("failed to add module to jit: {}", llvm::toString(std::move(err)));
                } else {
                    auto mainSym = (*jit)->lookup("main");
                    if (!mainSym) {
                        LOGE("failed to lookup main: {}", llvm::toString(mainSym.takeError()));
                    } else {
                        auto* mainFn = (int (*)())(intptr_t)mainSym->getValue();
                        int result = mainFn();
                        LOGI("jit execute main() = {}", result);
                    }
                }
            }
        }
    }

    LOGI("llvm c compile run success");
    return 0;
}
