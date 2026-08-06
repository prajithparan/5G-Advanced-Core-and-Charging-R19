add_executable(udr src/main.cpp src/stores.cpp)
target_link_libraries(udr PRIVATE sbi_core sbi_generated)
# See nfs/hello-nf/CMakeLists.txt for why this is a compile-time absolute path.
target_compile_definitions(udr PRIVATE CERTS_DIR="${CMAKE_SOURCE_DIR}/certs")
