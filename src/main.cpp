#include <string>
#include <cstdlib>
#include "frontend/Token.h"
#include "support/Log.h"
#include "support/File.h"
#include "support/ScopeGuard.h"
#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "support/Utils.h"
#include "codegen/CodegenContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Program.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/Verifier.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include <array>
#include <vector>
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

static inline constexpr const char* INPUT_C_FILE = RESOURCE_DIR "/main_min.c";

static std::string getHostTargetTriple() {
#if defined(__aarch64__)
    return "arm64-apple-darwin";
#elif defined(__x86_64__)
    return "x86_64-apple-darwin";
#else
    return llvm::sys::getDefaultTargetTriple();
#endif
}

static std::unique_ptr<llvm::TargetMachine>
createTargetMachine(llvm::Module& module) {
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    std::string triple = getHostTargetTriple();
    llvm::Triple llvmTriple(triple);
    module.setTargetTriple(llvmTriple);

    std::string error;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, error);
    if (!target) {
        LOGE("Target lookup failed: {}", error);
        return nullptr;
    }

    llvm::TargetOptions options;
    auto tm = target->createTargetMachine(
        triple, "generic", "", options, llvm::Reloc::Model());
    if (!tm) {
        LOGE("Failed to create TargetMachine");
        return nullptr;
    }

    module.setDataLayout(tm->createDataLayout());
    return std::unique_ptr<llvm::TargetMachine>(tm);
}

static bool emitObjectFile(llvm::Module& module, const std::string& outputPath) {
    auto tm = createTargetMachine(module);
    if (!tm) return false;

    std::error_code ec;
    llvm::raw_fd_ostream dest(outputPath, ec, llvm::sys::fs::OF_None);
    if (ec) {
        LOGE("Failed to open output file: {} ({})", outputPath, ec.message());
        return false;
    }

    llvm::legacy::PassManager passManager;
    if (tm->addPassesToEmitFile(passManager, dest, nullptr,
            llvm::CodeGenFileType::ObjectFile)) {
        LOGE("TargetMachine cannot emit object file");
        return false;
    }

    passManager.run(module);
    dest.close();
    return true;
}

static std::string runCommand(const std::string& cmd) {
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) {
        result += buf;
    }
    pclose(pipe);
    // trim trailing newline
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    return result;
}

static std::string findSystemLinker() {
    auto ldPath = llvm::sys::findProgramByName("ld");
    if (ldPath) return *ldPath;

    auto ldLldPath = llvm::sys::findProgramByName("ld.lld");
    if (ldLldPath) return *ldLldPath;

    return "";
}

static std::string findSDKPath() {
    std::string sdkPath = runCommand("xcrun --show-sdk-path");
    if (!sdkPath.empty()) return sdkPath;
    return "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk";
}

static int linkWithSystemLinker(const std::string& objectPath,
                                 const std::string& executablePath) {
    std::string linker = findSystemLinker();
    if (linker.empty()) {
        LOGE("cannot find system linker (ld)");
        return 1;
    }
    LOGI("using linker: {}", linker);

    std::string sdkPath = findSDKPath();
    LOGI("SDK path: {}", sdkPath);

    std::string sysrootLib = sdkPath + "/usr/lib";

#if defined(__aarch64__)
    std::string arch = "arm64";
#else
    std::string arch = "x86_64";
#endif

    std::vector<std::string> args;
    args.push_back(linker);
    args.push_back("-o");
    args.push_back(executablePath);
    args.push_back("-arch");
    args.push_back(arch);
    args.push_back("-platform_version");
    args.push_back("macos");
    args.push_back("15.0");
    args.push_back("15.0");
    args.push_back("-syslibroot");
    args.push_back(sdkPath);
    args.push_back("-L");
    args.push_back(sysrootLib);
    args.push_back(objectPath);
    args.push_back("-lSystem");
    args.push_back("-no_fixup_chains");
    args.push_back("-dead_strip");

    std::vector<llvm::StringRef> sArgs;
    for (const auto& a : args) {
        sArgs.push_back(a);
    }

    auto result = llvm::sys::ExecuteAndWait(linker, sArgs);
    if (result < 0) {
        LOGE("linker execution failed");
        return 1;
    }
    if (result != 0) {
        LOGE("linker failed with exit code {}", result);
        return result;
    }

    return 0;
}

int main(int argc, char* argv[]) {
    bool emitMode = false;
    std::string outputPath = "output";
    std::string inputFile = INPUT_C_FILE;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--emit-obj") {
            emitMode = true;
            if (i + 1 < argc) outputPath = argv[++i];
        } else if (arg[0] != '-') {
            inputFile = arg;
        }
    }

    LOGI("start llvm c compiler");
    LOGI("input: {}", inputFile);

    File file(inputFile.c_str());
    if (!file.isOpen()) {
        LOGE("open file {} failed", inputFile);
        return -1;
    }

    ScopeGuard scope_guard = ScopeGuard([&file]() { file.close(); });
    std::string content = file.readAll();
    if (!content.empty()) {
        LOGI("read file {} success", inputFile);
    }

    Lexer lexer(inputFile, content);
    auto tokens = lexer.tokenize();

    Parser parser(tokens);
    auto ast = parser.parse();
    if (!ast) {
        LOGE("parse ast failed");
        return 1;
    }

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

    if (emitMode) {
        std::string objectPath = outputPath + ".o";
        if (!emitObjectFile(codegenCtx.getModule(), objectPath)) {
            return 1;
        }
        LOGI("object file emitted: {}", objectPath);

        int ret = linkWithSystemLinker(objectPath, outputPath);
        if (ret != 0) return ret;
        LOGI("executable linked: {}", outputPath);
    } else {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();

        auto jtmb = llvm::orc::JITTargetMachineBuilder::detectHost();
        if (!jtmb) {
            LOGE("failed to detect host target");
            return 1;
        }

        auto jit = llvm::orc::LLJITBuilder()
            .setJITTargetMachineBuilder(std::move(*jtmb))
            .create();
        if (!jit) {
            LOGE("failed to create jit");
            return 1;
        }

        auto tsModule = llvm::orc::ThreadSafeModule(
            codegenCtx.takeModule(), codegenCtx.takeContext());
        if (auto err = (*jit)->addIRModule(std::move(tsModule))) {
            LOGE("failed to add module to jit");
            return 1;
        }

        auto mainSym = (*jit)->lookup("main");
        if (!mainSym) {
            LOGE("failed to lookup main");
            return 1;
        }

        auto* mainFn = (int (*)())(intptr_t)mainSym->getValue();
        int result = mainFn();
        LOGI("jit execute main() = {}", result);
    }

    LOGI("llvm c compile run success");
    return 0;
}
