cmake_minimum_required(VERSION 3.22)

project(ftx
    VERSION 0.1.0
    DESCRIPTION "Cross-platform secure TCP file transfer utility"
    HOMEPAGE_URL "https://github.com/"
    LANGUAGES C CXX
)

# ---------------------------------------------------------------------------
# Standards & global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(FTX_BUILD_TESTS    "Build unit and integration tests"  ON)
option(FTX_BUILD_BENCH    "Build benchmark targets"           OFF)
option(FTX_ENABLE_ASAN    "Enable AddressSanitizer"           OFF)
option(FTX_ENABLE_UBSAN   "Enable UndefinedBehaviorSanitizer" OFF)
option(FTX_ENABLE_TSAN    "Enable ThreadSanitizer"            OFF)
option(FTX_WARN_AS_ERROR  "Treat warnings as errors"          OFF)

# ---------------------------------------------------------------------------
# Warnings
# ---------------------------------------------------------------------------
add_library(ftx_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ftx_warnings INTERFACE
        -Wall -Wextra -Wpedantic
        -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
        -Wunused -Woverloaded-virtual -Wconversion -Wsign-conversion
        -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough
    )
    if(FTX_WARN_AS_ERROR)
        target_compile_options(ftx_warnings INTERFACE -Werror)
    endif()
elseif(MSVC)
    target_compile_options(ftx_warnings INTERFACE /W4 /permissive- /utf-8)
    if(FTX_WARN_AS_ERROR)
        target_compile_options(ftx_warnings INTERFACE /WX)
    endif()
    target_compile_definitions(ftx_warnings INTERFACE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

# ---------------------------------------------------------------------------
# Sanitizers
# ---------------------------------------------------------------------------
add_library(ftx_sanitizers INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    if(FTX_ENABLE_ASAN)
        target_compile_options(ftx_sanitizers INTERFACE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(ftx_sanitizers INTERFACE -fsanitize=address)
    endif()
    if(FTX_ENABLE_UBSAN)
        target_compile_options(ftx_sanitizers INTERFACE -fsanitize=undefined -fno-omit-frame-pointer)
        target_link_options(ftx_sanitizers INTERFACE -fsanitize=undefined)
    endif()
    if(FTX_ENABLE_TSAN)
        target_compile_options(ftx_sanitizers INTERFACE -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(ftx_sanitizers INTERFACE -fsanitize=thread)
    endif()
endif()

# ---------------------------------------------------------------------------
# ccache
# ---------------------------------------------------------------------------
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "ccache found: ${CCACHE_PROGRAM}")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
include(FetchContent)
add_subdirectory(third_party)

# ---------------------------------------------------------------------------
# Source tree
# ---------------------------------------------------------------------------
add_subdirectory(src)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(FTX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(FTX_BUILD_BENCH)
    add_subdirectory(bench)
endif()

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "================ ftx build configuration ================")
message(STATUS "  Version       : ${PROJECT_VERSION}")
message(STATUS "  Build type    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler      : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  C++ standard  : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Tests         : ${FTX_BUILD_TESTS}")
message(STATUS "  ASan / UBSan  : ${FTX_ENABLE_ASAN} / ${FTX_ENABLE_UBSAN}")
message(STATUS "  TSan          : ${FTX_ENABLE_TSAN}")
message(STATUS "=========================================================")
message(STATUS "")
