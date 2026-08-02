#include "CompilerDriver.h"
#include "Linker.h"
#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "codegen/CodegenContext.h"
#include "support/File.h"
#include "support/Log.h"
#include <cxxopts.hpp>
#include <iostream>
#include <cstdlib>

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Bitcode/BitcodeWriter.h"

static std::string getHostTargetTriple() {
    return llvm::sys::getDefaultTargetTriple();
}

static std::unique_ptr<llvm::TargetMachine>
createTargetMachine(llvm::Module& module) {
    std::string triple = getHostTargetTriple();
    llvm::Triple llvmTriple(triple);
    module.setTargetTriple(llvmTriple.str());

    std::string error;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(llvmTriple.str(), error);
    if (!target) {
        LOGE("Target lookup failed: {}", error);
        return nullptr;
    }

    llvm::TargetOptions options;
    auto tm = target->createTargetMachine(
        llvmTriple.str(), "generic", "", options, llvm::Reloc::Model());
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
    dest.flush();
    dest.close();
    return true;
}

bool CompilerDriver::parseArguments(int argc, const char* argv[]) {
    inputFiles.clear();
    outputFile.clear();
    includePaths.clear();
    defines.clear();
    libs.clear();
    libPaths.clear();
    compileOnly = false;
    emitIR = false;
    emitObj = false;
    preprocessOnly = false;
    syntaxOnly = false;
    jitMode = false;
    verbose = false;
    wall = false;
    werror = false;
    optLevel = 0;
    includeDebug = false;
    stdStandard.clear();

    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-Wall") {
            wall = true;
        } else if (arg == "-Werror") {
            werror = true;
        } else if (arg.substr(0, 5) == "-std=") {
            stdStandard = arg.substr(5);
        } else if (arg == "-std" && i + 1 < argc) {
            stdStandard = argv[++i];
        } else if (arg == "-fsyntax-only") {
            syntaxOnly = true;
        } else {
            args.push_back(arg);
        }
    }

    std::vector<const char*> cArgs;
    cArgs.push_back("my_llvm_c");
    for (const auto& a : args) {
        cArgs.push_back(a.c_str());
    }
    int newArgc = static_cast<int>(cArgs.size());
    const char** newArgv = cArgs.data();

    cxxopts::Options options("my_llvm_c", "A C11 subset compiler built with LLVM");
    
    options.add_options()
        ("c", "Compile only, do not link")
        ("o,output", "Output file", cxxopts::value<std::string>())
        ("S", "Emit IR only")
        ("E", "Preprocess only")
        ("I", "Add include path", cxxopts::value<std::vector<std::string>>())
        ("D", "Add preprocessor definition", cxxopts::value<std::vector<std::string>>())
        ("O", "Optimization level (0-3)", cxxopts::value<int>())
        ("g", "Include debug information")
        ("v,verbose", "Verbose output")
        ("l", "Link library", cxxopts::value<std::vector<std::string>>())
        ("L", "Library search path", cxxopts::value<std::vector<std::string>>())
        ("h,help", "Show this help message")
        ("positional", "Input files", cxxopts::value<std::vector<std::string>>())
        ;

    options.parse_positional({"positional"});

    try {
        auto result = options.parse(newArgc, newArgv);

        if (result.count("help")) {
            printHelp();
            return false;
        }

        if (result.count("c")) compileOnly = true;
        if (result.count("S")) emitIR = true;
        if (result.count("E")) preprocessOnly = true;
        if (result.count("g")) includeDebug = true;
        if (result.count("v")) verbose = true;

        if (result.count("o")) outputFile = result["o"].as<std::string>();
        if (result.count("O")) optLevel = result["O"].as<int>();

        if (result.count("I")) includePaths = result["I"].as<std::vector<std::string>>();
        if (result.count("D")) defines = result["D"].as<std::vector<std::string>>();
        if (result.count("l")) libs = result["l"].as<std::vector<std::string>>();
        if (result.count("L")) libPaths = result["L"].as<std::vector<std::string>>();

        if (result.count("positional")) {
            inputFiles = result["positional"].as<std::vector<std::string>>();
        }
    } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return false;
    }

    if (inputFiles.empty()) {
        jitMode = true;
    }

    if (outputFile.empty() && !inputFiles.empty()) {
        outputFile = "a.out";
    }

    return true;
}

int CompilerDriver::run() {
    if (jitMode) {
        if (verbose) std::cout << "running in JIT mode\n";
        return 0;
    }

    int exitCode = 0;
    for (const auto& file : inputFiles) {
        if (compileFile(file) != 0) {
            exitCode = 1;
        }
    }
    return exitCode;
}

int CompilerDriver::compileFile(const std::string& inputFile) {
    if (verbose) {
        std::cout << "compiling: " << inputFile << "\n";
        std::cout << "  output: " << outputFile << "\n";
        std::cout << "  compile-only: " << compileOnly << "\n";
        std::cout << "  emit-ir: " << emitIR << "\n";
        std::cout << "  opt-level: " << optLevel << "\n";
        std::cout << "  include-debug: " << includeDebug << "\n";
    }

    File file(inputFile.c_str());
    if (!file.isOpen()) {
        LOGE("open file {} failed", inputFile);
        return 1;
    }

    std::string content = file.readAll();
    file.close();

    if (content.empty()) {
        LOGE("file {} is empty", inputFile);
        return 1;
    }

    Lexer lexer(inputFile, content);
    auto tokens = lexer.tokenize();

    Parser parser(tokens);
    auto ast = parser.parse();
    if (!ast) {
        LOGE("parse ast failed");
        return 1;
    }

    if (syntaxOnly) {
        return 0;
    }

    if (preprocessOnly) {
        std::cout << content;
        return 0;
    }

    CodegenContext codegenCtx;
    codegenCtx.setSourceFile(inputFile);
    ast->codegen(codegenCtx);
    codegenCtx.finalizeDebugInfo();

    if (emitIR) {
        codegenCtx.getModule().print(llvm::outs(), nullptr);
        return 0;
    }

    if (optLevel > 0) {
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

        llvm::OptimizationLevel optLevelEnum;
        switch (optLevel) {
            case 1: optLevelEnum = llvm::OptimizationLevel::O1; break;
            case 2: optLevelEnum = llvm::OptimizationLevel::O2; break;
            case 3: optLevelEnum = llvm::OptimizationLevel::O3; break;
            default: optLevelEnum = llvm::OptimizationLevel::O2; break;
        }

        llvm::ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(optLevelEnum);
        MPM.run(codegenCtx.getModule(), MAM);
    }

    if (compileOnly) {
        std::string objectPath = outputFile;
        if (!emitObjectFile(codegenCtx.getModule(), objectPath)) {
            LOGE("failed to emit object file");
            return 1;
        }
        return 0;
    }

    std::string objectPath = outputFile + ".o";
    if (!emitObjectFile(codegenCtx.getModule(), objectPath)) {
        LOGE("failed to emit object file");
        return 1;
    }

    Linker linker;
    int ret = linker.link({objectPath}, outputFile);
    if (ret != 0) {
        LOGE("linker failed with exit code {}", ret);
        return ret;
    }

    std::remove(objectPath.c_str());

    return 0;
}

void CompilerDriver::printHelp() const {
    std::cout << "Usage: my_llvm_c [options] <input files>\n"
              << "\n"
              << "Options:\n"
              << "  -c              Compile only, do not link\n"
              << "  -o <file>       Output file\n"
              << "  -S              Emit IR only\n"
              << "  -E              Preprocess only\n"
              << "  -I <path>       Add include path\n"
              << "  -D <def>        Add preprocessor definition\n"
              << "  -O <level>      Optimization level (0-3)\n"
              << "  -g              Include debug information\n"
              << "  -v              Verbose output\n"
              << "  -Wall           Enable all warnings\n"
              << "  -Werror         Treat warnings as errors\n"
              << "  -std <standard> C standard (c99, c11, c17)\n"
              << "  -fsyntax-only   Syntax check only\n"
              << "  -l <lib>        Link library\n"
              << "  -L <path>       Library search path\n"
              << "  -h, --help      Show this help message\n";
}
