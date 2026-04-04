file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(my_llvm_c ${SOURCES})

target_include_directories(my_llvm_c PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

find_package(spdlog CONFIG REQUIRED)
target_link_libraries(my_llvm_c PRIVATE spdlog::spdlog)

target_compile_definitions(my_llvm_c PRIVATE RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resources")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(my_llvm_c PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)
else()
    target_compile_definitions(my_llvm_c PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO)
endif()
