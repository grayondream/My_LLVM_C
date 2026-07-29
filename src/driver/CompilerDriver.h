#pragma once

#include <string>
#include <vector>

class CompilerDriver {
public:
    CompilerDriver() = default;

    bool parseArguments(int argc, const char* argv[]);
    int run();

    const std::vector<std::string>& getInputFiles() const { return inputFiles; }
    const std::string& getOutputFile() const { return outputFile; }
    const std::vector<std::string>& getIncludePaths() const { return includePaths; }
    const std::vector<std::string>& getDefines() const { return defines; }
    bool getCompileOnly() const { return compileOnly; }
    bool getEmitIR() const { return emitIR; }
    bool getEmitObj() const { return emitObj; }
    bool getJitMode() const { return jitMode; }
    bool getVerbose() const { return verbose; }
    int getOptLevel() const { return optLevel; }
    bool getIncludeDebug() const { return includeDebug; }

    void printHelp() const;

private:
    std::vector<std::string> inputFiles;
    std::string outputFile;
    std::vector<std::string> includePaths;
    std::vector<std::string> defines;
    bool compileOnly = false;
    bool emitIR = false;
    bool emitObj = false;
    bool jitMode = false;
    bool verbose = false;
    int optLevel = 0;
    bool includeDebug = false;
};
