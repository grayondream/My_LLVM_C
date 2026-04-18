#pragma once
#include <string>

class Token;
class TranslationUnitAST;

std::string to_string(const Token& token);

std::string to_string(const TranslationUnitAST& ast);


