#include "CompilerDriver.h"
#include <cxxopts.hpp>
#include <iostream>
#include <cstdlib>
#include <algorithm>
#include <cstring>

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

    std::vector<std::string> remainingArgs;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-fsyntax-only") {
            syntaxOnly = true;
        } else if (arg == "-Wall") {
            wall = true;
        } else if (arg == "-Werror") {
            werror = true;
        } else if (arg == "-std") {
            if (i + 1 < argc) {
                stdStandard = argv[++i];
            } else {
                std::cerr << "error: -std requires an argument\n";
                return false;
            }
        } else {
            remainingArgs.push_back(arg);
        }
    }

    int newArgc = static_cast<int>(remainingArgs.size()) + 1;
    std::vector<const char*> newArgv;
    newArgv.push_back(argv[0]);
    for (const auto& arg : remainingArgs) {
        newArgv.push_back(arg.c_str());
    }

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
        auto result = options.parse(newArgc, newArgv.data());

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

    for (const auto& file : inputFiles) {
        if (verbose) {
            std::cout << "compiling: " << file << "\n";
            std::cout << "  output: " << outputFile << "\n";
            std::cout << "  compile-only: " << compileOnly << "\n";
            std::cout << "  emit-ir: " << emitIR << "\n";
            std::cout << "  emit-obj: " << emitObj << "\n";
            std::cout << "  preprocess-only: " << preprocessOnly << "\n";
            std::cout << "  syntax-only: " << syntaxOnly << "\n";
            std::cout << "  opt-level: " << optLevel << "\n";
            std::cout << "  include-debug: " << includeDebug << "\n";
            std::cout << "  wall: " << wall << "\n";
            std::cout << "  werror: " << werror << "\n";
            if (!stdStandard.empty()) std::cout << "  std: " << stdStandard << "\n";
            for (const auto& inc : includePaths) {
                std::cout << "  include: " << inc << "\n";
            }
            for (const auto& def : defines) {
                std::cout << "  define: " << def << "\n";
            }
            for (const auto& lib : libs) {
                std::cout << "  lib: " << lib << "\n";
            }
            for (const auto& path : libPaths) {
                std::cout << "  lib-path: " << path << "\n";
            }
        }
    }

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
