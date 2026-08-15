add_executable(udm src/main.cpp src/stores.cpp src/map_client.cpp)
target_link_libraries(udm PRIVATE sbi_core sbi_generated aka_crypto ss7_core tcap_core map_core)
# See nfs/hello-nf/CMakeLists.txt for why this is a compile-time absolute path.
target_compile_definitions(udm PRIVATE CERTS_DIR="${CMAKE_SOURCE_DIR}/certs")
