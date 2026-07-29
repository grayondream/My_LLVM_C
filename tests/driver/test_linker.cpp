#include <gtest/gtest.h>
#include "driver/Linker.h"

TEST(LinkerTest, FindLinker) {
    Linker linker;
    std::string path = linker.findSystemLinker();
    EXPECT_FALSE(path.empty());
}

TEST(LinkerTest, ConstructLinkCommand) {
    Linker linker;
    std::vector<std::string> objects = {"a.o", "b.o"};
    std::string output = "program";
    std::string cmd = linker.constructLinkCommand(objects, output);
    EXPECT_NE(cmd.find("a.o"), std::string::npos);
    EXPECT_NE(cmd.find("b.o"), std::string::npos);
    EXPECT_NE(cmd.find("-o program"), std::string::npos);
}
