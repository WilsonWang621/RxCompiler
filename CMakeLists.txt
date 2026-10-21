cmake_minimum_required(VERSION 3.16)

project(rx_compiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# The upstream runtime enables its own tests and shared library by default.
# Neither is needed when embedding the runtime in this compiler.
set(ANTLR_BUILD_CPP_TESTS OFF CACHE BOOL "" FORCE)
set(ANTLR_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ANTLR_BUILD_STATIC ON CACHE BOOL "" FORCE)

# Keep the runtime version aligned with the ANTLR tool version recorded in the
# grammar files and used to produce generated/.
set(ANTLR4_RUNTIME_ARCHIVE
    "${CMAKE_CURRENT_SOURCE_DIR}/.antlr/antlr4-cpp-runtime-4.13.2-source.zip")
if(EXISTS "${ANTLR4_RUNTIME_ARCHIVE}")
    set(ANTLR4_RUNTIME_URL "${ANTLR4_RUNTIME_ARCHIVE}")
else()
    set(ANTLR4_RUNTIME_URL
        "https://www.antlr.org/download/antlr4-cpp-runtime-4.13.2-source.zip")
endif()

FetchContent_Declare(
    antlr4_runtime
    URL "${ANTLR4_RUNTIME_URL}"
    URL_HASH SHA256=0ed13668906e86dbc0dcddf30fdee68c10203dea4e83852b4edb810821bee3c4
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(antlr4_runtime)

add_executable(compiler
    src/main.cpp
    src/ast/ast.cpp
    src/frontend/ast_builder.cpp
    src/semantic/semantic_checker.cpp
    src/semantic/declaration_collector.cpp
    src/semantic/function_collector.cpp
    src/semantic/resolution.cpp
    src/semantic/const_evaluate.cpp
    src/semantic/statement_collector.cpp
    src/semantic/expression_collector.cpp
    src/semantic/control_flow_collector.cpp
    src/semantic/types_checker.cpp
    generated/Lexer.cpp
    generated/Parser.cpp
    generated/ParserBaseVisitor.cpp
    generated/ParserVisitor.cpp
)

target_compile_definitions(compiler PRIVATE ANTLR4CPP_STATIC)
target_include_directories(compiler PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ast
    ${CMAKE_CURRENT_SOURCE_DIR}/src/frontend
    ${CMAKE_CURRENT_SOURCE_DIR}/src/semantic
    ${CMAKE_CURRENT_SOURCE_DIR}/generated
    ${antlr4_runtime_SOURCE_DIR}/runtime/src
)
target_link_libraries(compiler PRIVATE antlr4_static)

set_target_properties(compiler PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/target
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(compiler PRIVATE -Wall -Wextra -Wpedantic)
endif()
