#pragma once
#include <string>
#include <vector>

class Linker {
    std::string systemLinkerPath;
public:
    Linker();
    std::string findSystemLinker();
    std::string constructLinkCommand(const std::vector<std::string>& objects, const std::string& output);
    int link(const std::vector<std::string>& objects, const std::string& output);
};
