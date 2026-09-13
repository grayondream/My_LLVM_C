#pragma once
#include <string>
#include <vector>

class Type;

std::string typeToMangled(Type* type);
std::string mangleFunction(const std::string& name, const std::vector<Type*>& paramTypes);

// Mark a function name as having C linkage (extern "C" / extern declarations):
// it will be emitted with its plain, unmangled symbol name.
void markCName(const std::string& name);
bool isCName(const std::string& name);