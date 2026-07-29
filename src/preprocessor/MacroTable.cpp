#include "preprocessor/MacroTable.h"

void MacroTable::define(const Macro& macro) {
    macros[macro.name] = macro;
}

void MacroTable::undefine(const std::string& name) {
    macros.erase(name);
}

const Macro* MacroTable::lookup(const std::string& name) const {
    auto it = macros.find(name);
    if (it != macros.end()) {
        return &it->second;
    }
    return nullptr;
}

bool MacroTable::isDefined(const std::string& name) const {
    return macros.find(name) != macros.end();
}
