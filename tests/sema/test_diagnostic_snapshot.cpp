// INF-03 / INF-13: diagnostic snapshot tests.
//
// Each case formats the analyzer diagnostics for a source and compares them to
// a golden file under tests/diagnostics/snapshots/<name>.txt. Regenerate with:
//   SMC_UPDATE_SNAPSHOTS=1 ./build/bin/compiler_tests --gtest_filter='*Snapshot*'
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "sema/SemanticAnalyzer.h"

#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace {

class DiagnosticSnapshotTest : public ::testing::Test {
protected:
    void SetUp() override { spdlog::set_level(spdlog::level::off); }
};

std::string analyzeDiagnostics(const std::string& source) {
    Lexer lexer("snapshot.c", source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();

    std::ostringstream out;
    if (!ast) {
        out << "<parse failed>\n";
        return out.str();
    }
    for (const auto& d : parser.getErrors()) {
        out << d.formatWithSeverity() << "\n";
    }
    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    for (const auto& d : analyzer.getErrors()) {
        out << d.formatWithSeverity() << "\n";
    }
    return out.str();
}

void expectSnapshot(const std::string& name, const std::string& actual) {
    fs::path file = fs::path(SNAPSHOT_DIR) / (name + ".txt");

    if (std::getenv("SMC_UPDATE_SNAPSHOTS")) {
        fs::create_directories(file.parent_path());
        std::ofstream out(file);
        out << actual;
        return;
    }

    ASSERT_TRUE(fs::exists(file)) << "missing snapshot: " << file
                                  << " (regenerate with SMC_UPDATE_SNAPSHOTS=1)";
    std::ifstream in(file);
    std::string expected((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    EXPECT_EQ(actual, expected) << "snapshot mismatch: " << file;
}

} // namespace

TEST_F(DiagnosticSnapshotTest, UndeclaredIdentifier) {
    expectSnapshot("undeclared_identifier", analyzeDiagnostics(R"(
int main() {
    return missing;
}
)"));
}

TEST_F(DiagnosticSnapshotTest, IncompatibleAssignment) {
    expectSnapshot("incompatible_assignment", analyzeDiagnostics(R"(
struct S { int x; };
int main() {
    struct S s;
    int a = 1;
    a = s;
    return 0;
}
)"));
}

TEST_F(DiagnosticSnapshotTest, FunctionRedefinition) {
    expectSnapshot("function_redefinition", analyzeDiagnostics(R"(
int f() { return 1; }
int f() { return 2; }
)"));
}

TEST_F(DiagnosticSnapshotTest, IncompatibleCastWarning) {
    expectSnapshot("incompatible_cast", analyzeDiagnostics(R"(
struct S { int x; };
int main() {
    struct S s;
    float f = (float)s;
    return 0;
}
)"));
}
