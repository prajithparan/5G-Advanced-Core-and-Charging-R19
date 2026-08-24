add_executable(nssf src/main.cpp src/stores.cpp)
target_link_libraries(nssf PRIVATE sbi_core sbi_generated nf_config)
# See nfs/hello-nf/CMakeLists.txt for why this is a compile-time absolute path.
# CONFIG_DIR: docs/DECISIONS.md ADR-0077 -- separate checked-in config/nssf.json.
target_compile_definitions(nssf PRIVATE CERTS_DIR="${CMAKE_SOURCE_DIR}/certs" CONFIG_DIR="${CMAKE_SOURCE_DIR}/config")
