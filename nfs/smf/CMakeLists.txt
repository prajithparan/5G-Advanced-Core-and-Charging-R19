add_executable(smf src/main.cpp src/sm_context_store.cpp src/nas_5gsm_codec.cpp src/pfcp_peer.cpp)
target_link_libraries(smf PRIVATE sbi_core sbi_generated pfcp_core nf_config)
# See nfs/hello-nf/CMakeLists.txt for why this is a compile-time absolute path.
# CONFIG_DIR: docs/DECISIONS.md ADR-0077 -- separate checked-in config/smf.json.
target_compile_definitions(smf PRIVATE CERTS_DIR="${CMAKE_SOURCE_DIR}/certs" CONFIG_DIR="${CMAKE_SOURCE_DIR}/config")
