# redis-plus-plus (Valkey-protocol-compatible): real persistence for UeSecurityContextStore
# (gap-closure, docs/CAPABILITY_GAP_ANALYSIS.md task #100/ADR-0075) -- same real dependency every
# other NF's own Redis-backed hot-path store already uses (CHF's ChargingDataStore, PCF's
# SpendingLimitTrackingStore).
find_package(redis++ CONFIG REQUIRED)

add_executable(amf src/main.cpp src/ue_context_store.cpp src/ue_security_context_store.cpp
                   src/amf_ue_id_index_store.cpp src/ngap_codec.cpp src/ngap_task.cpp
                   src/nas_codec.cpp)
target_link_libraries(amf PRIVATE sbi_core sbi_generated ngap_core ngap_generated aka_crypto redis++::redis++_static nf_config)
# See nfs/hello-nf/CMakeLists.txt for why this is a compile-time absolute path.
# CONFIG_DIR: docs/DECISIONS.md ADR-0077 -- separate checked-in config/amf.json, not hardcoded
# defaults inside main.cpp.
target_compile_definitions(amf PRIVATE CERTS_DIR="${CMAKE_SOURCE_DIR}/certs" CONFIG_DIR="${CMAKE_SOURCE_DIR}/config")
