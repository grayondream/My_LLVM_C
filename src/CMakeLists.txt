file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(FILTER SOURCES EXCLUDE REGEX ".*main\\.cpp$")

add_library(my_llvm_c_lib ${SOURCES})

target_include_directories(my_llvm_c_lib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

find_package(spdlog CONFIG REQUIRED)
target_link_libraries(my_llvm_c_lib PUBLIC spdlog::spdlog)

find_package(cxxopts CONFIG REQUIRED)
target_link_libraries(my_llvm_c_lib PUBLIC cxxopts::cxxopts)

if(LLVM_LINK_LLVM_DYLIB)
    target_link_libraries(my_llvm_c_lib PUBLIC LLVM)
else()
    llvm_map_components_to_libnames(llvm_libs
        core
        support
        irreader
        bitreader
        bitwriter
        analysis
        transformutils
        instcombine
        scalaropts
        passes
        orcjit
        target
        mc
        mcparser
        mcdisassembler
        x86asmparser
        x86codegen
        x86desc
        x86info
        x86disassembler
    )
    target_link_libraries(my_llvm_c_lib PUBLIC ${llvm_libs})
endif()

target_compile_definitions(my_llvm_c_lib PRIVATE RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resources")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(my_llvm_c_lib PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)
else()
    target_compile_definitions(my_llvm_c_lib PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO)
endif()

add_executable(my_llvm_c main.cpp)
target_include_directories(my_llvm_c PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(my_llvm_c PRIVATE my_llvm_c_lib)
target_compile_definitions(my_llvm_c PRIVATE RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resources")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(my_llvm_c PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)
else()
    target_compile_definitions(my_llvm_c PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO)
endif()

add_custom_target(copy_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/compile_commands.json"
    COMMENT "Copying compile_commands.json to source directory"
)