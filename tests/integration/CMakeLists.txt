find_package(GTest CONFIG REQUIRED)

add_executable(integration_tests test_hello_nf_registration.cpp)
target_link_libraries(integration_tests PRIVATE GTest::gtest GTest::gtest_main)
add_dependencies(integration_tests nrf hello-nf)
target_compile_definitions(integration_tests PRIVATE
    NRF_PATH="$<TARGET_FILE:nrf>"
    HELLO_NF_PATH="$<TARGET_FILE:hello-nf>"
)

include(GoogleTest)
gtest_discover_tests(integration_tests)
