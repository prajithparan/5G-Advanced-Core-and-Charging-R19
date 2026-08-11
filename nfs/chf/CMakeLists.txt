# redis-plus-plus (Valkey-protocol-compatible): real persistence for CHF's stores (E3 Session
# Establishment's explicit "must be idempotent and recoverable across restarts and network
# partitions" requirement, docs/DATA_MODEL.md's own Redis/Valkey assignment) -- see
# docs/DECISIONS.md for the ADR this session added.
find_package(redis++ CONFIG REQUIRED)

add_executable(chf src/main.cpp src/stores.cpp)
target_link_libraries(chf PRIVATE sbi_core sbi_generated bss_sid redis++::redis++_static)
# See nfs/hello-nf/CMakeLists.txt for why this is a compile-time absolute path.
target_compile_definitions(chf PRIVATE CERTS_DIR="${CMAKE_SOURCE_DIR}/certs")
