file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(my_llvm_c ${SOURCES})

target_include_directories(my_llvm_c PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

find_package(spdlog CONFIG REQUIRED)
target_link_libraries(my_llvm_c PRIVATE spdlog::spdlog)

if(LLVM_LINK_LLVM_DYLIB)
    target_link_libraries(my_llvm_c PRIVATE LLVM)
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
        target
        x86asmparser
        x86codegen
        x86desc
        x86info
    )
    target_link_libraries(my_llvm_c PRIVATE ${llvm_libs})
endif()

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
