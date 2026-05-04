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

if(WIN32)
    target_link_libraries(asio INTERFACE ws2_32 mswsock)
    target_compile_definitions(asio INTERFACE _WIN32_WINNT=0x0A00)
endif()
