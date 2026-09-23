#include "driver/StdPrelude.h"

#include "ast/Decl.h"
#include "frontend/Lexer.h"
#include "frontend/Parser.h"

#include <iterator>

namespace smc {

// NOTE: keep this parseable with the current grammar. `usize` is the platform
// size type (see docs/spec/abi.md §1); `char*` is the C string type.
const char* builtinStdCPrelude() {
    return R"PRELUDE(
// ===== std.c — built-in libc binding layer (MOD-09 / STD-23) =====
// Declarations only: the linker resolves them against the C library.
extern int    printf(char* format, ...);
extern int    puts(char* s);
extern int    putchar(int c);
extern int    getchar();

extern void*  malloc(usize size);
extern void*  calloc(usize count, usize size);
extern void*  realloc(void* ptr, usize size);
extern void   free(void* ptr);

extern usize  strlen(char* s);
extern int    strcmp(char* a, char* b);
extern int    strncmp(char* a, char* b, usize n);
extern char*  strcpy(char* dst, char* src);
extern void*  memcpy(void* dst, void* src, usize n);
extern void*  memset(void* dst, int value, usize n);
extern void*  memmove(void* dst, void* src, usize n);
extern int    memcmp(void* a, void* b, usize n);

extern int    abs(int x);
extern void   exit(int status);
)PRELUDE";
}

std::unique_ptr<TranslationUnitAST> parseStdCPrelude(const std::string& source,
                                                     const std::string& filename) {
    if (source.empty()) {
        return nullptr;
    }
    Lexer lexer(filename, source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    return parser.parse();
}

void prependDeclarations(TranslationUnitAST& target, TranslationUnitAST& prelude) {
    if (prelude.declarations.empty()) {
        return;
    }
    target.declarations.insert(
        target.declarations.begin(),
        std::make_move_iterator(prelude.declarations.begin()),
        std::make_move_iterator(prelude.declarations.end()));
    prelude.declarations.clear();
}

} // namespace smc
