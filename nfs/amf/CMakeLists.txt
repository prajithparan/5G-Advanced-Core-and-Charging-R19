add_executable(amf src/main.cpp src/ue_context_store.cpp src/ngap_codec.cpp src/ngap_task.cpp
                   src/nas_codec.cpp)
target_link_libraries(amf PRIVATE sbi_core sbi_generated ngap_core ngap_generated aka_crypto)
# See nfs/hello-nf/CMakeLists.txt for why this is a compile-time absolute path.
target_compile_definitions(amf PRIVATE CERTS_DIR="${CMAKE_SOURCE_DIR}/certs")
