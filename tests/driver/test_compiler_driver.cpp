#include <gtest/gtest.h>
#include "driver/CompilerDriver.h"

class CompilerDriverTest : public ::testing::Test {
protected:
    void SetUp() override {
        driver = CompilerDriver();
    }

    CompilerDriver driver;
};

TEST_F(CompilerDriverTest, ParseCompileOnlyFlag) {
    const char* argv[] = {"my_llvm_c", "-c", "test.c"};
    ASSERT_TRUE(driver.parseArguments(3, argv));
    EXPECT_TRUE(driver.getCompileOnly());
}

TEST_F(CompilerDriverTest, ParseOutputFlag) {
    const char* argv[] = {"my_llvm_c", "-o", "out.o", "test.c"};
    ASSERT_TRUE(driver.parseArguments(4, argv));
    EXPECT_EQ(driver.getOutputFile(), "out.o");
}

TEST_F(CompilerDriverTest, ParseSingleIncludePath) {
    const char* argv[] = {"my_llvm_c", "-I/usr/include", "test.c"};
    ASSERT_TRUE(driver.parseArguments(3, argv));
    ASSERT_EQ(driver.getIncludePaths().size(), 1u);
    EXPECT_EQ(driver.getIncludePaths()[0], "/usr/include");
}

TEST_F(CompilerDriverTest, ParseMultipleIncludePaths) {
    const char* argv[] = {"my_llvm_c", "-I/usr/include", "-I", "/usr/local/include", "test.c"};
    ASSERT_TRUE(driver.parseArguments(5, argv));
    ASSERT_EQ(driver.getIncludePaths().size(), 2u);
    EXPECT_EQ(driver.getIncludePaths()[0], "/usr/include");
    EXPECT_EQ(driver.getIncludePaths()[1], "/usr/local/include");
}

TEST_F(CompilerDriverTest, ParseDefine) {
    const char* argv[] = {"my_llvm_c", "-DDEBUG", "test.c"};
    ASSERT_TRUE(driver.parseArguments(3, argv));
    ASSERT_EQ(driver.getDefines().size(), 1u);
    EXPECT_EQ(driver.getDefines()[0], "DEBUG");
}

TEST_F(CompilerDriverTest, ParseMultipleDefines) {
    const char* argv[] = {"my_llvm_c", "-DDEBUG=1", "-DRELEASE", "test.c"};
    ASSERT_TRUE(driver.parseArguments(4, argv));
    ASSERT_EQ(driver.getDefines().size(), 2u);
    EXPECT_EQ(driver.getDefines()[0], "DEBUG=1");
    EXPECT_EQ(driver.getDefines()[1], "RELEASE");
}

TEST_F(CompilerDriverTest, DefaultBehaviorIsJitMode) {
    const char* argv[] = {"my_llvm_c"};
    ASSERT_TRUE(driver.parseArguments(1, argv));
    EXPECT_TRUE(driver.getJitMode());
    EXPECT_TRUE(driver.getInputFiles().empty());
}

TEST_F(CompilerDriverTest, ParseInputFile) {
    const char* argv[] = {"my_llvm_c", "test.c"};
    ASSERT_TRUE(driver.parseArguments(2, argv));
    ASSERT_EQ(driver.getInputFiles().size(), 1u);
    EXPECT_EQ(driver.getInputFiles()[0], "test.c");
    EXPECT_FALSE(driver.getJitMode());
}

TEST_F(CompilerDriverTest, ParseMultipleInputFiles) {
    const char* argv[] = {"my_llvm_c", "a.c", "b.c"};
    ASSERT_TRUE(driver.parseArguments(3, argv));
    ASSERT_EQ(driver.getInputFiles().size(), 2u);
    EXPECT_EQ(driver.getInputFiles()[0], "a.c");
    EXPECT_EQ(driver.getInputFiles()[1], "b.c");
}

TEST_F(CompilerDriverTest, ParseEmitIR) {
    const char* argv[] = {"my_llvm_c", "-S", "test.c"};
    ASSERT_TRUE(driver.parseArguments(3, argv));
    EXPECT_TRUE(driver.getEmitIR());
}

TEST_F(CompilerDriverTest, ParseEmitObj) {
    const char* argv[] = {"my_llvm_c", "-emit-obj", "test.c"};
    ASSERT_TRUE(driver.parseArguments(3, argv));
    EXPECT_TRUE(driver.getEmitObj());
}

TEST_F(CompilerDriverTest, ParseOptLevels) {
    {
        const char* argv[] = {"my_llvm_c", "-O0", "test.c"};
        ASSERT_TRUE(driver.parseArguments(3, argv));
        EXPECT_EQ(driver.getOptLevel(), 0);
    }
    {
        CompilerDriver d;
        const char* argv[] = {"my_llvm_c", "-O2", "test.c"};
        ASSERT_TRUE(d.parseArguments(3, argv));
        EXPECT_EQ(d.getOptLevel(), 2);
    }
    {
        CompilerDriver d;
        const char* argv[] = {"my_llvm_c", "-O3", "test.c"};
        ASSERT_TRUE(d.parseArguments(3, argv));
        EXPECT_EQ(d.getOptLevel(), 3);
    }
}

TEST_F(CompilerDriverTest, ParseDebugFlag) {
    const char* argv[] = {"my_llvm_c", "-g", "test.c"};
    ASSERT_TRUE(driver.parseArguments(3, argv));
    EXPECT_TRUE(driver.getIncludeDebug());
}

TEST_F(CompilerDriverTest, ParseVerboseFlag) {
    const char* argv[] = {"my_llvm_c", "-v", "test.c"};
    ASSERT_TRUE(driver.parseArguments(3, argv));
    EXPECT_TRUE(driver.getVerbose());
}

TEST_F(CompilerDriverTest, ParseCombinedFlags) {
    const char* argv[] = {"my_llvm_c", "-c", "-g", "-O2", "-I/usr/include", "-DDEBUG", "-o", "out.o", "test.c"};
    ASSERT_TRUE(driver.parseArguments(9, argv));
    EXPECT_TRUE(driver.getCompileOnly());
    EXPECT_TRUE(driver.getIncludeDebug());
    EXPECT_EQ(driver.getOptLevel(), 2);
    ASSERT_EQ(driver.getIncludePaths().size(), 1u);
    EXPECT_EQ(driver.getIncludePaths()[0], "/usr/include");
    ASSERT_EQ(driver.getDefines().size(), 1u);
    EXPECT_EQ(driver.getDefines()[0], "DEBUG");
    EXPECT_EQ(driver.getOutputFile(), "out.o");
    ASSERT_EQ(driver.getInputFiles().size(), 1u);
    EXPECT_EQ(driver.getInputFiles()[0], "test.c");
}

TEST_F(CompilerDriverTest, UnknownOptionFails) {
    const char* argv[] = {"my_llvm_c", "--unknown", "test.c"};
    EXPECT_FALSE(driver.parseArguments(3, argv));
}

TEST_F(CompilerDriverTest, MissingOutputArgumentFails) {
    const char* argv[] = {"my_llvm_c", "-o"};
    EXPECT_FALSE(driver.parseArguments(2, argv));
}

TEST_F(CompilerDriverTest, RunReturnsZero) {
    const char* argv[] = {"my_llvm_c", "test.c"};
    ASSERT_TRUE(driver.parseArguments(2, argv));
    EXPECT_EQ(driver.run(), 0);
}

TEST_F(CompilerDriverTest, RunJitModeReturnsZero) {
    const char* argv[] = {"my_llvm_c"};
    ASSERT_TRUE(driver.parseArguments(1, argv));
    EXPECT_EQ(driver.run(), 0);
}
