#include "driver/CompilerDriver.h"
#include "support/Log.h"
#include "llvm/Support/TargetSelect.h"
#include <spdlog/spdlog.h>

int main(int argc, char* argv[]) {
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    CompilerDriver driver;
    if (!driver.parseArguments(argc, const_cast<const char**>(argv))) {
        return 1;
    }

    // Keep normal compilations quiet: only warnings/errors by default, verbose
    // (token/lexer) logging with -v.
    spdlog::set_level(driver.getVerbose() ? spdlog::level::info
                                          : spdlog::level::warn);

    return driver.run();
}
