add_executable(amf src/main.cpp src/ue_context_store.cpp)
target_link_libraries(amf PRIVATE sbi_core sbi_generated)
# See nfs/hello-nf/CMakeLists.txt for why this is a compile-time absolute path.
target_compile_definitions(amf PRIVATE CERTS_DIR="${CMAKE_SOURCE_DIR}/certs")
