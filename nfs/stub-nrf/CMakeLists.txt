add_executable(stub-nrf src/main.cpp)
target_link_libraries(stub-nrf PRIVATE sbi_core)
