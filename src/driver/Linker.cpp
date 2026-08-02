#include "Linker.h"
#include <cstdlib>
#include <sstream>

Linker::Linker() {
    systemLinkerPath = findSystemLinker();
}

std::string Linker::findSystemLinker() {
    const char* linkers[] = {"cc", "gcc", "ld", "ld.lld", "ld.gold", nullptr};
    for (const char** l = linkers; *l; l++) {
        std::string cmd = std::string("which ") + *l + " 2>/dev/null";
        if (system(cmd.c_str()) == 0) {
            return *l;
        }
    }
    return "ld";
}

std::string Linker::constructLinkCommand(const std::vector<std::string>& objects, const std::string& output) {
    std::ostringstream cmd;
    cmd << systemLinkerPath << " -o " << output;
    for (const auto& obj : objects) {
        cmd << " " << obj;
    }
    return cmd.str();
}

int Linker::link(const std::vector<std::string>& objects, const std::string& output) {
    std::string cmd = constructLinkCommand(objects, output);
    return system(cmd.c_str());
}
