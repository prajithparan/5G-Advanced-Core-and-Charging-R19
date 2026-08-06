find_package(GTest CONFIG REQUIRED)

add_executable(integration_tests
    test_hello_nf_registration.cpp
    test_amf_namf_communication.cpp
    test_smf_pdu_session.cpp
    test_udm_uecm_sdm.cpp
    test_udm_ueau.cpp
    test_udr_context_data.cpp
)
target_link_libraries(integration_tests PRIVATE sbi_core sbi_generated GTest::gtest GTest::gtest_main)
add_dependencies(integration_tests nrf amf smf udm udr hello-nf)
target_compile_definitions(integration_tests PRIVATE
    NRF_PATH="$<TARGET_FILE:nrf>"
    AMF_PATH="$<TARGET_FILE:amf>"
    SMF_PATH="$<TARGET_FILE:smf>"
    UDM_PATH="$<TARGET_FILE:udm>"
    UDR_PATH="$<TARGET_FILE:udr>"
    HELLO_NF_PATH="$<TARGET_FILE:hello-nf>"
    CERTS_DIR="${CMAKE_SOURCE_DIR}/certs"
)

include(GoogleTest)
gtest_discover_tests(integration_tests)
