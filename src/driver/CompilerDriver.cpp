#include "CompilerDriver.h"
#include <iostream>
#include <cstdlib>

bool CompilerDriver::parseArguments(int argc, const char* argv[]) {
    inputFiles.clear();
    outputFile.clear();
    includePaths.clear();
    defines.clear();
    compileOnly = false;
    emitIR = false;
    emitObj = false;
    jitMode = false;
    verbose = false;
    optLevel = 0;
    includeDebug = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printHelp();
            return false;
        } else if (arg == "-c") {
            compileOnly = true;
        } else if (arg == "-o") {
            if (i + 1 < argc) {
                outputFile = argv[++i];
            } else {
                std::cerr << "error: -o requires an argument\n";
                return false;
            }
        } else if (arg == "-S") {
            emitIR = true;
        } else if (arg == "-emit-obj") {
            emitObj = true;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "-g") {
            includeDebug = true;
        } else if (arg.substr(0, 2) == "-I") {
            if (arg.length() > 2) {
                includePaths.push_back(arg.substr(2));
            } else if (i + 1 < argc) {
                includePaths.push_back(argv[++i]);
            } else {
                std::cerr << "error: -I requires a path\n";
                return false;
            }
        } else if (arg.substr(0, 2) == "-D") {
            if (arg.length() > 2) {
                defines.push_back(arg.substr(2));
            } else if (i + 1 < argc) {
                defines.push_back(argv[++i]);
            } else {
                std::cerr << "error: -D requires a definition\n";
                return false;
            }
        } else if (arg == "-O0") {
            optLevel = 0;
        } else if (arg == "-O1") {
            optLevel = 1;
        } else if (arg == "-O2") {
            optLevel = 2;
        } else if (arg == "-O3") {
            optLevel = 3;
        } else if (arg[0] == '-') {
            std::cerr << "error: unknown option: " << arg << "\n";
            return false;
        } else {
            inputFiles.push_back(arg);
        }
    }

    if (inputFiles.empty()) {
        jitMode = true;
    }

    if (outputFile.empty() && !inputFiles.empty()) {
        outputFile = "output";
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
            std::cout << "  opt-level: " << optLevel << "\n";
            std::cout << "  include-debug: " << includeDebug << "\n";
            for (const auto& inc : includePaths) {
                std::cout << "  include: " << inc << "\n";
            }
            for (const auto& def : defines) {
                std::cout << "  define: " << def << "\n";
            }
        }
    }

    return 0;
}

void CompilerDriver::printHelp() const {
    std::cout << "Usage: my_llvm_c [options] <input files>\n"
              << "\n"
              << "Options:\n"
              << "  -c          Compile only, do not link\n"
              << "  -o <file>   Output file\n"
              << "  -S          Emit IR only\n"
              << "  -emit-obj   Emit object file\n"
              << "  -I <path>   Add include path\n"
              << "  -D <def>    Add preprocessor definition\n"
              << "  -O<level>   Optimization level (0-3)\n"
              << "  -g          Include debug information\n"
              << "  -v          Verbose output\n"
              << "  -h          Show this help message\n";
}
