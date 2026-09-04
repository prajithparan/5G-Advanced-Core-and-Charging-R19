add_executable(nsacf src/main.cpp src/stores.cpp)
target_link_libraries(nsacf PRIVATE sbi_core sbi_generated nf_config)
# See nfs/hello-nf/CMakeLists.txt for why this is a compile-time absolute path.
# CONFIG_DIR: docs/DECISIONS.md ADR-0077/ADR-0273 -- checked-in config/nsacf.json.
target_compile_definitions(nsacf PRIVATE CERTS_DIR="${CMAKE_SOURCE_DIR}/certs" CONFIG_DIR="${CMAKE_SOURCE_DIR}/config")
