#include "driver/ModuleLoader.h"

#include "ast/Decl.h"
#include "frontend/Lexer.h"
#include "frontend/Parser.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fs = std::filesystem;

namespace smc {

namespace {

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string canonicalPath(const std::string& path) {
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(fs::path(path), ec);
    return ec ? fs::absolute(fs::path(path)).string() : canonical.string();
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

// Candidate relative paths for a specifier (`std.c` -> `std/c.smc`).
std::vector<std::string> importCandidates(const std::string& specifier) {
    std::vector<std::string> candidates;
    // Explicit path (has a separator or the module extension): use as-is.
    if (specifier.find('/') != std::string::npos || endsWith(specifier, ".smc")) {
        candidates.push_back(specifier);
        return candidates;
    }
    // Dotted module path: `std.c` -> `std/c.smc`.
    std::string dotted = specifier;
    for (char& c : dotted) {
        if (c == '.') c = '/';
    }
    candidates.push_back(dotted + ".smc");
    candidates.push_back(dotted);
    candidates.push_back(specifier + ".smc");
    candidates.push_back(specifier);
    return candidates;
}

} // namespace

std::string resolveImport(const std::string& specifier, const ModuleSearchPaths& paths) {
    if (specifier.empty()) return "";
    for (const std::string& dir : paths.dirs) {
        for (const std::string& candidate : importCandidates(specifier)) {
            fs::path full = fs::path(dir) / candidate;
            std::error_code ec;
            if (fs::is_regular_file(full, ec)) {
                return full.string();
            }
        }
    }
    return "";
}

void loadModuleFile(const std::string& path,
                    const ModuleSearchPaths& paths,
                    std::set<std::string>& loaded,
                    std::vector<std::unique_ptr<DeclAST>>& out,
                    std::vector<std::string>& errors) {
    const std::string canonical = canonicalPath(path);
    if (loaded.count(canonical)) {
        return; // already loaded (also breaks import cycles)
    }
    loaded.insert(canonical);

    std::string source;
    if (!readFile(canonical, source)) {
        errors.push_back("cannot read module '" + canonical + "'");
        return;
    }

    Lexer lexer(canonical, source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    if (!ast) {
        errors.push_back(canonical + ": failed to parse module");
        return;
    }
    if (!parser.getErrors().empty()) {
        for (const auto& diag : parser.getErrors()) {
            errors.push_back(diag.formatWithSeverity());
        }
        return;
    }

    // Resolve this module's imports first (imported declarations before ours).
    ModuleSearchPaths childPaths = paths;
    childPaths.dirs.insert(childPaths.dirs.begin(), canonicalPath(fs::path(canonical).parent_path().string()));
    for (const std::string& spec : ast->imports) {
        std::string resolved = resolveImport(spec, childPaths);
        if (resolved.empty()) {
            errors.push_back(canonical + ": cannot resolve import '" + spec + "'");
            continue;
        }
        loadModuleFile(resolved, childPaths, loaded, out, errors);
    }

    for (auto& decl : ast->declarations) {
        out.push_back(std::move(decl));
    }
}

void processImports(TranslationUnitAST& tu,
                    const std::string& importerDir,
                    const ModuleSearchPaths& paths,
                    std::set<std::string>& loaded,
                    std::vector<std::string>& errors) {
    if (tu.imports.empty()) return;

    ModuleSearchPaths localPaths = paths;
    if (!importerDir.empty()) {
        localPaths.dirs.insert(localPaths.dirs.begin(), importerDir);
    }

    std::vector<std::unique_ptr<DeclAST>> imported;
    for (const std::string& spec : tu.imports) {
        std::string resolved = resolveImport(spec, localPaths);
        if (resolved.empty()) {
            errors.push_back("cannot resolve import '" + spec + "'");
            continue;
        }
        loadModuleFile(resolved, localPaths, loaded, imported, errors);
    }

    if (!imported.empty()) {
        tu.declarations.insert(
            tu.declarations.begin(),
            std::make_move_iterator(imported.begin()),
            std::make_move_iterator(imported.end()));
    }
}

} // namespace smc
