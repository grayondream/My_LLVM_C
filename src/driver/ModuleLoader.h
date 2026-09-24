#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>

class TranslationUnitAST;
class DeclAST;

namespace smc {

// Ordered directories used to resolve `import` specifiers (P0-04 / MOD-04).
struct ModuleSearchPaths {
    std::vector<std::string> dirs;
};

// Resolve an import specifier to a readable file path, or "" when not found.
// A specifier containing '/' (or ending in ".smc") is treated as a relative
// path; otherwise dots map to directory separators and ".smc" is appended
// (`std.c` -> `std/c.smc`). `paths.dirs` are searched in order.
std::string resolveImport(const std::string& specifier, const ModuleSearchPaths& paths);

// Load `path` and, recursively, its imports. Declarations are appended to `out`
// in dependency order (imports before importers). `loaded` holds canonical
// paths already processed, which both deduplicates and breaks cycles.
// Diagnostics are appended to `errors`.
void loadModuleFile(const std::string& path,
                    const ModuleSearchPaths& paths,
                    std::set<std::string>& loaded,
                    std::vector<std::unique_ptr<DeclAST>>& out,
                    std::vector<std::string>& errors);

// Resolve and load `tu`'s imports (searching `importerDir` first), splicing the
// imported declarations in front of `tu.declarations` so they are visible to
// the importing unit during semantic analysis.
void processImports(TranslationUnitAST& tu,
                    const std::string& importerDir,
                    const ModuleSearchPaths& paths,
                    std::set<std::string>& loaded,
                    std::vector<std::string>& errors);

} // namespace smc
