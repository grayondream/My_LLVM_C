#include <string>
#include "Token.h"
#include "Log.h"
#include "File.h"
#include "ScopeGuard.h"
#include "Lexer.h"

inline static constexpr const char* INPUT_C_FILE = RESOURCE_DIR "/main.c";

int main(int argc, char* argv[]){
    LOGI("start llvm c compiler");
    File file(INPUT_C_FILE);
    if(!file.isOpen()){
        LOGE("open file {} failed", INPUT_C_FILE);
        return -1;
    }

    ScopeGuard scope_guard = ScopeGuard([&file]() { file.close(); });
    std::string content = file.readAll();
    if(!content.empty()){
        LOGI("read file {} success", INPUT_C_FILE);
        LOGI("content:\n{}", content);
    }

    Token token(TokenType::TOKEN_IDENTIFIER, "x");
    LOGI("Token: {}", to_string(token));

    Lexer lexer(INPUT_C_FILE, content);
    auto tokens = lexer.tokenize();
    for(const auto& token : tokens) {
        LOGI("Token: {}", to_string(token));
    }

    LOGI("llvm c compile run success");
    return 0;
}
