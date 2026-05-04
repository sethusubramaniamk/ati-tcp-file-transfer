include(GoogleTest)

add_executable(ftx_unit_tests
    smoke_test.cpp
    util/crc32c_test.cpp
    proto/frame_test.cpp
    proto/decoder_test.cpp
)

target_link_libraries(ftx_unit_tests
    PRIVATE
        ftx_lib
        GTest::gtest_main
        ftx_warnings
        ftx_sanitizers
)

gtest_discover_tests(ftx_unit_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS "unit"
)
