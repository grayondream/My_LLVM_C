#include <string>
#include <cstdlib>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include "frontend/Token.h"
#include "support/Log.h"
#include "support/File.h"
#include "support/ScopeGuard.h"
#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "support/Utils.h"
#include "codegen/CodegenContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Program.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"

static inline constexpr const char* INPUT_C_FILE = RESOURCE_DIR "/main_min.c";

static std::string getHostTargetTriple() {
    return llvm::sys::getDefaultTargetTriple();
}

static std::unique_ptr<llvm::TargetMachine>
createTargetMachine(llvm::Module& module) {
    std::string triple = getHostTargetTriple();
    llvm::Triple llvmTriple(triple);
    module.setTargetTriple(llvmTriple);

    std::string error;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(llvmTriple, error);
    if (!target) {
        LOGE("Target lookup failed: {}", error);
        return nullptr;
    }

    llvm::TargetOptions options;
    auto tm = target->createTargetMachine(
        llvmTriple, "generic", "", options, llvm::Reloc::Model());
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
    llvm::MachineModuleInfoWrapperPass MMIWP(tm.get());
    LOGI("adding object emission passes...");
    if (tm->addPassesToEmitFile(passManager, dest, nullptr,
            llvm::CodeGenFileType::ObjectFile, true, &MMIWP)) {
        LOGE("TargetMachine cannot emit object file");
        return false;
    }

    struct sigaction sa[6];
    struct { int signum; } sigs[] = {{SIGILL},{SIGTRAP},{SIGABRT},{SIGBUS},{SIGFPE},{SIGSEGV}};
    for (int i = 0; i < 6; i++) sigaction(sigs[i].signum, nullptr, &sa[i]);

    LOGI("running pass manager...");
    passManager.run(module);

    for (int i = 0; i < 6; i++) sigaction(sigs[i].signum, &sa[i], nullptr);
    LOGI("pass manager done, flushing...");
    dest.flush();
    dest.close();
    LOGI("object file written to {}", outputPath);
    return true;
}

static std::string findSystemLinker() {
    auto ldPath = llvm::sys::findProgramByName("ld");
    if (ldPath) return *ldPath;
    auto ldLldPath = llvm::sys::findProgramByName("ld.lld");
    if (ldLldPath) return *ldLldPath;
    return "";
}

static int linkWithSystemLinker(const std::string& objectPath,
                                 const std::string& executablePath) {
    std::string linker = findSystemLinker();
    if (linker.empty()) {
        LOGE("cannot find system linker (ld)");
        return 1;
    }
    LOGI("using linker: {}", linker);

    std::string sdkPath = "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";

#if defined(__aarch64__)
    std::string arch = "arm64";
#else
    std::string arch = "x86_64";
#endif

    std::string cmd = linker
        + " -o " + executablePath
        + " -arch " + arch
        + " -platform_version macos 15.0 15.0"
        + " -syslibroot " + sdkPath
        + " -L " + sdkPath + "/usr/lib"
        + " " + objectPath
        + " -lSystem"
        + " -no_fixup_chains";

    LOGI("link command: {}", cmd);
    int ret = system(cmd.c_str());
    if (ret != 0) {
        LOGE("linker failed with exit code {}", ret);
    }
    return ret;
}

int main(int argc, char* argv[]) {
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

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
    codegenCtx.setSourceFile(inputFile);
    ast->codegen(codegenCtx);
    codegenCtx.finalizeDebugInfo();

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
        std::string bitcodePath = objectPath + ".bc";

        {
            std::error_code ec;
            llvm::raw_fd_ostream bitcodeFile(bitcodePath, ec, llvm::sys::fs::OF_None);
            if (ec) {
                LOGE("Failed to write bitcode: {}", ec.message());
                return 1;
            }
            llvm::WriteBitcodeToFile(codegenCtx.getModule(), bitcodeFile);
        }

        pid_t pid = fork();
        if (pid == 0) {
            auto childCtx = std::make_unique<llvm::LLVMContext>();
            auto bufferOrErr = llvm::MemoryBuffer::getFile(bitcodePath);
            if (!bufferOrErr) {
                LOGE("failed to read bitcode");
                _exit(1);
            }
            auto modOrErr = llvm::parseBitcodeFile(**bufferOrErr, *childCtx);
            if (!modOrErr) {
                LOGE("failed to parse bitcode");
                _exit(1);
            }
            auto& childMod = **modOrErr;

            llvm::InitializeAllTargetInfos();
            llvm::InitializeAllTargets();
            llvm::InitializeAllTargetMCs();
            llvm::InitializeAllAsmParsers();
            llvm::InitializeAllAsmPrinters();

            if (!emitObjectFile(childMod, objectPath)) {
                _exit(1);
            }

            int ret = linkWithSystemLinker(objectPath, outputPath);
            _exit(ret);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
            if (WIFEXITED(status)) {
                int ret = WEXITSTATUS(status);
                if (ret != 0) return ret;
            } else {
                return 1;
            }
            LOGI("executable linked: {}", outputPath);
        } else {
            LOGE("fork failed");
            return 1;
        }
    } else {
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
