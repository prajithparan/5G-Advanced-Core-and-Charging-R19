add_executable(hello-nf src/main.cpp)
target_link_libraries(hello-nf PRIVATE sbi_core)
# See nfs/stub-nrf/CMakeLists.txt for why this is a compile-time absolute path.
target_compile_definitions(hello-nf PRIVATE CERTS_DIR="${CMAKE_SOURCE_DIR}/certs")
