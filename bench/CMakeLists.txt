add_executable(ftx_bench
    throughput_bench.cpp
)

target_link_libraries(ftx_bench
    PRIVATE
        ftx_lib
        ftx_warnings
        ftx_sanitizers
)
