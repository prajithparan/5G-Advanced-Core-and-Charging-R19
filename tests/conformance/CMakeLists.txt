find_package(GTest CONFIG REQUIRED)

add_executable(conformance_tests test_round_trip.cpp test_multipart.cpp test_milenage.cpp test_eap_aka_prime.cpp test_nas_codec.cpp test_nas_security.cpp test_nas_5gsm_codec.cpp test_pfcp_core.cpp test_bss_sid.cpp test_bss_sid_product.cpp test_diameter_core.cpp test_ss7_core.cpp test_tcap_core.cpp test_ss7_tcap_stack.cpp test_cap_core.cpp
    ${CMAKE_SOURCE_DIR}/nfs/amf/src/nas_codec.cpp
    ${CMAKE_SOURCE_DIR}/nfs/smf/src/nas_5gsm_codec.cpp)
target_link_libraries(conformance_tests PRIVATE GTest::gtest GTest::gtest_main sbi_generated aka_crypto pfcp_core bss_sid diameter_core ss7_core tcap_core cap_core)
target_include_directories(conformance_tests PRIVATE ${CMAKE_SOURCE_DIR}/nfs/amf/src ${CMAKE_SOURCE_DIR}/nfs/smf/src)
target_compile_features(conformance_tests PRIVATE cxx_std_23)

set(SBI_CONFORMANCE_SAMPLES_DIR ${CMAKE_CURRENT_BINARY_DIR}/samples)

include(GoogleTest)
gtest_discover_tests(conformance_tests
    PROPERTIES ENVIRONMENT "SBI_CONFORMANCE_SAMPLES_DIR=${SBI_CONFORMANCE_SAMPLES_DIR}"
)

# Structural schema-conformance check against the real OpenAPI YAML, run
# after the C++ round-trip test has written its sample JSON. See
# validate_structural_conformance.py for what this does and does not check.
add_test(
    NAME structural_conformance
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/validate_structural_conformance.py ${SBI_CONFORMANCE_SAMPLES_DIR}
)
set_tests_properties(structural_conformance PROPERTIES
    DEPENDS "RoundTrip.GuamiNestedObjectRoundTrips;RoundTrip.NFTypeUnknownValueRoundTrips"
)
