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
    }

    LOGI("llvm c compile run success");
    return 0;
}
