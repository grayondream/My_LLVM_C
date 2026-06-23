#include "Symbol.h"

Symbol* Scope::lookup(const std::string& name) const {
    auto it = symbols.find(name);
    if(it != symbols.end()) {
        return it->second;
    }

    if(parent) {
        return parent->lookup(name);
    }
    
    return nullptr;
}
