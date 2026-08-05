#pragma once
#include <string>
#include <vector>

class Type;

std::string typeToMangled(Type* type);
std::string mangleFunction(const std::string& name, const std::vector<Type*>& paramTypes);