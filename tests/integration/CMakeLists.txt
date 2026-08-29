find_package(GTest CONFIG REQUIRED)

add_executable(integration_tests
    test_hello_nf_registration.cpp
    test_nrf_gap_closure.cpp
    test_amf_namf_communication.cpp
    test_amf_namf_location_eventexposure.cpp
    test_amf_namf_aiot_mbs_mt.cpp
    test_smf_pdu_session.cpp
    test_smf_event_exposure_nidd.cpp
    test_udm_uecm_sdm.cpp
    test_udm_ueau.cpp
    test_udm_ueau_gap_closure_214.cpp
    test_udm_uecm_gap_closure_215.cpp
    test_udm_uecm_gap_closure_216.cpp
    test_udm_uecm_gap_closure_217.cpp
    test_udm_uecm_gap_closure_218.cpp
    test_udm_uecm_gap_closure_219.cpp
    test_udm_uecm_gap_closure_220.cpp
    test_udm_uecm_gap_closure_221.cpp
    test_udm_uecm_gap_closure_227.cpp
    test_udm_sdm_gap_closure_228.cpp
    test_udm_sdm_gap_closure_230.cpp
    test_udm_sdm_gap_closure_232.cpp
    test_udm_sdm_gap_closure_233.cpp
    test_udm_sdm_gap_closure_234.cpp
    test_udm_sdm_gap_closure_235.cpp
    test_udm_mt_niddau_rsds_ssau_ueid.cpp
    test_upf_event_exposure_gueip.cpp
    test_udr_context_data.cpp
    test_udr_ondatachange_webhook.cpp
    test_udr_gap_closure_212.cpp
    test_udr_policy_data_gap_closure_213.cpp
    test_ausf_ue_authentication.cpp
    test_ausf_upu_protection.cpp
    test_pcf_policy_control.cpp
    test_pcf_ue_policy_event_exposure.cpp
    test_pcf_am_mbs_policy_auth_control.cpp
    test_pcf_pdtq_bdt_policy_control.cpp
    test_nef_sms_ueid_dnaimapping_easdeployment.cpp
    test_nef_smcontext_authentication_ecsaddress.cpp
    test_nef_event_exposure.cpp
    test_nef_traffic_influence_inference_training.cpp
    test_n28_spending_limit.cpp
    test_product_catalog_postgres.cpp
    test_subscriber_management_postgres.cpp
    test_roaming_interconnect_postgres.cpp
)
target_link_libraries(integration_tests PRIVATE sbi_core sbi_generated aka_crypto product_catalog_store subscriber_management_store roaming_interconnect_store GTest::gtest GTest::gtest_main)
add_dependencies(integration_tests nrf amf smf udm udr ausf pcf chf hello-nf upf nef)
target_compile_definitions(integration_tests PRIVATE
    NRF_PATH="$<TARGET_FILE:nrf>"
    AMF_PATH="$<TARGET_FILE:amf>"
    SMF_PATH="$<TARGET_FILE:smf>"
    UDM_PATH="$<TARGET_FILE:udm>"
    UPF_PATH="$<TARGET_FILE:upf>"
    UDR_PATH="$<TARGET_FILE:udr>"
    AUSF_PATH="$<TARGET_FILE:ausf>"
    PCF_PATH="$<TARGET_FILE:pcf>"
    CHF_PATH="$<TARGET_FILE:chf>"
    HELLO_NF_PATH="$<TARGET_FILE:hello-nf>"
    NEF_PATH="$<TARGET_FILE:nef>"
    CERTS_DIR="${CMAKE_SOURCE_DIR}/certs"
)

include(GoogleTest)
gtest_discover_tests(integration_tests)
