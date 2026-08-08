add_executable(upf src/main.cpp)
target_link_libraries(upf PRIVATE sbi_core sbi_generated pfcp_core)
# See nfs/hello-nf/CMakeLists.txt for why this is a compile-time absolute path.
target_compile_definitions(upf PRIVATE CERTS_DIR="${CMAKE_SOURCE_DIR}/certs")
