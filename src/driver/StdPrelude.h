#pragma once

#include <memory>
#include <string>

class TranslationUnitAST;

namespace smc {

// Built-in C binding layer (TODO MOD-09 / STD-23).
//
// The language has no preprocessor and does not parse real C headers; instead
// libc symbols are made available through `extern` declarations. This is the
// seed of the `std.c` module: it is injected into every translation unit
// (prepended, so its declarations are visible to user code) unless disabled
// with `--no-prelude`.
//
// It is embedded as source text for now; once the module system (MOD-04) lands
// it should move to a `std.c` module file.
const char* builtinStdCPrelude();

// Parse prelude source into its own translation unit. Returns nullptr on a
// null/empty source. Parse errors are surfaced through the parser's diagnostics
// but do not throw.
std::unique_ptr<TranslationUnitAST> parseStdCPrelude(const std::string& source,
                                                     const std::string& filename);

// Move the declarations of `prelude` to the front of `target` so that they are
// declared before any user declaration that may reference them.
void prependDeclarations(TranslationUnitAST& target, TranslationUnitAST& prelude);

} // namespace smc
