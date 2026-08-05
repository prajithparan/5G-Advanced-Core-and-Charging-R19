add_executable(nrf src/main.cpp src/registry.cpp)
target_link_libraries(nrf PRIVATE sbi_core sbi_generated)
# See nfs/hello-nf/CMakeLists.txt for why this is a compile-time absolute path.
target_compile_definitions(nrf PRIVATE CERTS_DIR="${CMAKE_SOURCE_DIR}/certs")
