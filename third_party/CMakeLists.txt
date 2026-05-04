# ---------------------------------------------------------------------------
# third-party dependencies via FetchContent.
# Pinned tags only; no floating refs.
# ---------------------------------------------------------------------------

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---- GoogleTest -----------------------------------------------------------
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)
set(gtest_force_shared_crt ON  CACHE BOOL "" FORCE)
set(BUILD_GMOCK            ON  CACHE BOOL "" FORCE)
set(INSTALL_GTEST          OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ---- spdlog ---------------------------------------------------------------
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.13.0
    GIT_SHALLOW    TRUE
)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL      OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

# ---- CLI11 ----------------------------------------------------------------
FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.4.1
    GIT_SHALLOW    TRUE
)
set(CLI11_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cli11)

# ---- Asio (standalone, no Boost) ------------------------------------------
# Asio's repository has no top-level CMakeLists.txt, so we Populate() and wire
# an INTERFACE target by hand.
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG        asio-1-30-2
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(asio)
if(NOT asio_POPULATED)
    FetchContent_Populate(asio)
endif()

add_library(asio INTERFACE)
target_include_directories(asio SYSTEM INTERFACE ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(asio INTERFACE
    ASIO_STANDALONE
    ASIO_NO_DEPRECATED
    ASIO_DISABLE_BOOST_DATE_TIME
    ASIO_DISABLE_BOOST_REGEX
)

find_package(Threads REQUIRED)
target_link_libraries(asio INTERFACE Threads::Threads)

# OpenSSL for asio::ssl::*.
find_package(OpenSSL 3.0 REQUIRED)
target_link_libraries(asio INTERFACE OpenSSL::SSL OpenSSL::Crypto)

if(WIN32)
    target_link_libraries(asio INTERFACE ws2_32 mswsock crypt32)
    target_compile_definitions(asio INTERFACE _WIN32_WINNT=0x0A00)
endif()

# ---- BLAKE3 (official C reference impl) -----------------------------------
# We compile the portable C source ourselves to keep the build simple and
# warning-clean. Phase 8 may switch to SIMD-accelerated variants.
FetchContent_Declare(
    blake3
    GIT_REPOSITORY https://github.com/BLAKE3-team/BLAKE3.git
    GIT_TAG        1.5.1
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(blake3)
if(NOT blake3_POPULATED)
    FetchContent_Populate(blake3)
endif()

set(_BLAKE3_SOURCES
    ${blake3_SOURCE_DIR}/c/blake3.c
    ${blake3_SOURCE_DIR}/c/blake3_dispatch.c
    ${blake3_SOURCE_DIR}/c/blake3_portable.c
)

# On aarch64, blake3_dispatch.c unconditionally calls into the NEON path;
# adding blake3_neon.c gives us SIMD acceleration on Jetson and resolves
# the link symbol. On other arches we stay portable-only (SSE/AVX disabled
# below) — the SIMD asm files require nasm or specific compiler flags we
# don't want to pull in for a baseline build.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    list(APPEND _BLAKE3_SOURCES ${blake3_SOURCE_DIR}/c/blake3_neon.c)
endif()

add_library(blake3 STATIC ${_BLAKE3_SOURCES})
target_include_directories(blake3 SYSTEM PUBLIC ${blake3_SOURCE_DIR}/c)
target_compile_definitions(blake3 PUBLIC
    BLAKE3_NO_SSE2
    BLAKE3_NO_SSE41
    BLAKE3_NO_AVX2
    BLAKE3_NO_AVX512
)
set_target_properties(blake3 PROPERTIES POSITION_INDEPENDENT_CODE ON)
