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
