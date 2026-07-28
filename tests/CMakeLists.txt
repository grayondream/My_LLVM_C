include(FetchContent)

FetchContent_Declare(
    googletest
    URL /tmp/googletest.zip
    URL_HASH SHA256=1f357c27ca988c3f7c6b4bf68a9395005ac6761f034046e9dde0896e3aba00e4
)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

file(GLOB_RECURSE TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(compiler_tests ${TEST_SOURCES})

target_include_directories(compiler_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(compiler_tests
    PRIVATE
        my_llvm_c_lib
        GTest::gtest_main
        GTest::gmock
)

gtest_discover_tests(compiler_tests)