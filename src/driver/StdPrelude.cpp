#include "driver/StdPrelude.h"

#include "ast/Decl.h"
#include "frontend/Lexer.h"
#include "frontend/Parser.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

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
extern void   abort();

// stderr without exposing the `FILE` type: dprintf(2, ...) writes to stderr.
extern int    dprintf(int fd, char* format, ...);

// Buffered file I/O. A `FILE*` is passed around as an opaque `void*`.
extern void*  fopen(char* path, char* mode);
extern int    fclose(void* stream);
extern usize  fread(void* ptr, usize size, usize count, void* stream);
extern usize  fwrite(void* ptr, usize size, usize count, void* stream);
extern char*  fgets(char* str, int n, void* stream);
extern int    fputs(char* s, void* stream);
extern int    fgetc(void* stream);
extern int    fputc(int c, void* stream);
extern int    fprintf(void* stream, char* format, ...);

// Formatted input from stdin.
extern int    scanf(char* format, ...);
)PRELUDE";
}

std::string loadStdCPrelude(const std::vector<std::string>& dirs) {
    for (const std::string& dir : dirs) {
        std::filesystem::path candidate =
            std::filesystem::path(dir) / "std" / "c.smc";
        std::error_code ec;
        if (!std::filesystem::is_regular_file(candidate, ec)) {
            continue;
        }
        std::ifstream in(candidate, std::ios::binary);
        if (!in.is_open()) {
            continue;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }
    return builtinStdCPrelude();
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
