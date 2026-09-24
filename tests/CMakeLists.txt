find_package(GTest QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    )
    FetchContent_MakeAvailable(googletest)
endif()

include(GoogleTest)

file(GLOB_RECURSE TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(compiler_tests ${TEST_SOURCES})

target_include_directories(compiler_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(compiler_tests PRIVATE
    TEST_INPUT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/test_input"
    E2E_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/e2e/resources"
    SNAPSHOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/diagnostics/snapshots"
    STD_DIR="${CMAKE_SOURCE_DIR}/libs"
    SPEC_DIR="${CMAKE_SOURCE_DIR}/docs/spec"
)

target_link_libraries(compiler_tests
    PRIVATE
        my_llvm_c_lib
        safec
        GTest::gtest_main
        GTest::gmock
)

gtest_discover_tests(compiler_tests)