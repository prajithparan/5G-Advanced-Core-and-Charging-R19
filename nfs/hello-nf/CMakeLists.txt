add_executable(hello-nf src/main.cpp)
target_link_libraries(hello-nf PRIVATE sbi_core nf_config)
# See nfs/stub-nrf/CMakeLists.txt for why this is a compile-time absolute path.
# CONFIG_DIR: docs/DECISIONS.md ADR-0077 -- separate checked-in config/hello-nf.json.
target_compile_definitions(hello-nf PRIVATE CERTS_DIR="${CMAKE_SOURCE_DIR}/certs" CONFIG_DIR="${CMAKE_SOURCE_DIR}/config")
