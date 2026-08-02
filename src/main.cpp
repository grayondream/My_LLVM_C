#include "driver/CompilerDriver.h"
#include "support/Log.h"
#include "llvm/Support/TargetSelect.h"

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

    return driver.run();
}
