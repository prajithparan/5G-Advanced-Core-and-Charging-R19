# libpqxx: real PostgreSQL persistence for UDR's context-data stores (ADR-0068, gap-closure Tier
# 1a from the free5GC/open5gs source comparison -- see stores.hpp's own header).
find_package(libpqxx CONFIG REQUIRED)

add_executable(udr src/main.cpp src/stores.cpp)
target_link_libraries(udr PRIVATE sbi_core sbi_generated libpqxx::pqxx)
# See nfs/hello-nf/CMakeLists.txt for why this is a compile-time absolute path.
target_compile_definitions(udr PRIVATE CERTS_DIR="${CMAKE_SOURCE_DIR}/certs")
