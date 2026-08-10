add_executable(product-catalog src/main.cpp src/store.cpp)
target_link_libraries(product-catalog PRIVATE sbi_core bss_sid)
# See nfs/hello-nf/CMakeLists.txt for why this is a compile-time absolute path.
target_compile_definitions(product-catalog PRIVATE CERTS_DIR="${CMAKE_SOURCE_DIR}/certs")
