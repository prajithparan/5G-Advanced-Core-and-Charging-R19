# redis-plus-plus: real persistence for KausfStore (gap-closure, docs/CAPABILITY_GAP_ANALYSIS.md
# task #104/ADR-0081) -- same real dependency every other Redis-backed hot-path store in this
# project already uses (AMF's UeSecurityContextStore, CHF's ChargingDataStore).
find_package(redis++ CONFIG REQUIRED)

add_executable(ausf src/main.cpp src/stores.cpp src/kausf_store.cpp)
target_link_libraries(ausf PRIVATE sbi_core sbi_generated aka_crypto redis++::redis++_static nf_config)
# See nfs/hello-nf/CMakeLists.txt for why this is a compile-time absolute path.
# CONFIG_DIR: docs/DECISIONS.md ADR-0077 -- separate checked-in config/ausf.json, not hardcoded
# defaults inside main.cpp.
target_compile_definitions(ausf PRIVATE CERTS_DIR="${CMAKE_SOURCE_DIR}/certs" CONFIG_DIR="${CMAKE_SOURCE_DIR}/config")
