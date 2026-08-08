add_executable(smf src/main.cpp src/sm_context_store.cpp src/nas_5gsm_codec.cpp)
target_link_libraries(smf PRIVATE sbi_core sbi_generated pfcp_core)
# See nfs/hello-nf/CMakeLists.txt for why this is a compile-time absolute path.
target_compile_definitions(smf PRIVATE CERTS_DIR="${CMAKE_SOURCE_DIR}/certs")
