include(GoogleTest)

# ---------------------------------------------------------------------------
# Unit tests: pure-logic, no networking.
# ---------------------------------------------------------------------------
add_executable(ftx_unit_tests
    smoke_test.cpp
    util/crc32c_test.cpp
    util/blake3_test.cpp
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
# Test certs — generated on demand by scripts/gen-test-certs.sh.
# ---------------------------------------------------------------------------
set(FTX_TEST_CERTS_DIR "${CMAKE_BINARY_DIR}/test-certs")
set(FTX_TEST_CERTS
    "${FTX_TEST_CERTS_DIR}/ca.crt"
    "${FTX_TEST_CERTS_DIR}/server.crt"
    "${FTX_TEST_CERTS_DIR}/server.key"
    "${FTX_TEST_CERTS_DIR}/client.crt"
    "${FTX_TEST_CERTS_DIR}/client.key"
)
add_custom_command(
    OUTPUT ${FTX_TEST_CERTS}
    COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/gen-test-certs.sh" "${FTX_TEST_CERTS_DIR}"
    DEPENDS "${CMAKE_SOURCE_DIR}/scripts/gen-test-certs.sh"
    COMMENT "Generating ftx test certificates"
    VERBATIM
)
add_custom_target(ftx_test_certs DEPENDS ${FTX_TEST_CERTS})

# ---------------------------------------------------------------------------
# Integration tests: spin up server + client over loopback. Plain TCP suite
# and TLS+mTLS suite share the same fixtures.
# ---------------------------------------------------------------------------
add_executable(ftx_integration_tests
    transport/loopback_test.cpp
    transport/tls_loopback_test.cpp
)

add_dependencies(ftx_integration_tests ftx_test_certs)

target_compile_definitions(ftx_integration_tests
    PRIVATE
        FTX_TEST_CERTS_DIR="${FTX_TEST_CERTS_DIR}"
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
