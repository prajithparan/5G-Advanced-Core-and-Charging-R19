add_executable(pcf src/main.cpp src/stores.cpp)
target_link_libraries(pcf PRIVATE sbi_core sbi_generated)
# See nfs/hello-nf/CMakeLists.txt for why this is a compile-time absolute path.
target_compile_definitions(pcf PRIVATE CERTS_DIR="${CMAKE_SOURCE_DIR}/certs")
