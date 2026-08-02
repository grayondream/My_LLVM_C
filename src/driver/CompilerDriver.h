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
    const std::vector<std::string>& getLibs() const { return libs; }
    const std::vector<std::string>& getLibPaths() const { return libPaths; }
    bool getCompileOnly() const { return compileOnly; }
    bool getEmitIR() const { return emitIR; }
    bool getEmitObj() const { return emitObj; }
    bool getPreprocessOnly() const { return preprocessOnly; }
    bool getSyntaxOnly() const { return syntaxOnly; }
    bool getJitMode() const { return jitMode; }
    bool getVerbose() const { return verbose; }
    bool getWall() const { return wall; }
    bool getWerror() const { return werror; }
    int getOptLevel() const { return optLevel; }
    bool getIncludeDebug() const { return includeDebug; }
    const std::string& getStdStandard() const { return stdStandard; }

    void printHelp() const;

private:
    int compileFile(const std::string& inputFile);

    std::vector<std::string> inputFiles;
    std::string outputFile;
    std::vector<std::string> includePaths;
    std::vector<std::string> defines;
    std::vector<std::string> libs;
    std::vector<std::string> libPaths;
    bool compileOnly = false;
    bool emitIR = false;
    bool emitObj = false;
    bool preprocessOnly = false;
    bool syntaxOnly = false;
    bool jitMode = false;
    bool verbose = false;
    bool wall = false;
    bool werror = false;
    int optLevel = 0;
    bool includeDebug = false;
    std::string stdStandard;
};
