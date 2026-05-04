# ---------------------------------------------------------------------------
# ftx_lib — core library, depended on by the CLI and the tests.
# ---------------------------------------------------------------------------
add_library(ftx_lib STATIC
    lib/version.cpp
    lib/util/crc32c.cpp
    lib/proto/types.cpp
    lib/proto/frame.cpp
    lib/proto/decoder.cpp
)

target_include_directories(ftx_lib
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(ftx_lib
    PUBLIC
        spdlog::spdlog
    PRIVATE
        ftx_warnings
        ftx_sanitizers
)

target_compile_features(ftx_lib PUBLIC cxx_std_20)

# ---------------------------------------------------------------------------
# ftx — CLI executable.
# ---------------------------------------------------------------------------
add_executable(ftx
    cli/main.cpp
)

target_link_libraries(ftx
    PRIVATE
        ftx_lib
        CLI11::CLI11
        ftx_warnings
        ftx_sanitizers
)
