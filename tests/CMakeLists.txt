include(GoogleTest)

# ---------------------------------------------------------------------------
# Unit tests: pure-logic, no networking.
# ---------------------------------------------------------------------------
add_executable(ftx_unit_tests
    smoke_test.cpp
    util/crc32c_test.cpp
    proto/frame_test.cpp
    proto/decoder_test.cpp
    proto/messages_test.cpp
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

# ---------------------------------------------------------------------------
# Integration tests: spin up server + client over loopback.
# ---------------------------------------------------------------------------
add_executable(ftx_integration_tests
    transport/loopback_test.cpp
)

target_link_libraries(ftx_integration_tests
    PRIVATE
        ftx_lib
        GTest::gtest_main
        ftx_warnings
        ftx_sanitizers
)

gtest_discover_tests(ftx_integration_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS "integration"
)
