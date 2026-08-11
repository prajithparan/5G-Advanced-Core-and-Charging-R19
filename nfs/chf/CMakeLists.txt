# redis-plus-plus (Valkey-protocol-compatible): real persistence for CHF's stores (E3 Session
# Establishment's explicit "must be idempotent and recoverable across restarts and network
# partitions" requirement, docs/DATA_MODEL.md's own Redis/Valkey assignment) -- see
# docs/DECISIONS.md for the ADR this session added.
find_package(redis++ CONFIG REQUIRED)

# clickhouse-cpp (P4.4/ADR-0058: real CDF/CDR generation, docs/DATA_MODEL.md's E4 ClickHouse
# assignment). Real, disclosed vcpkg-port limitation, confirmed by inspecting the installed
# package directly (no *Config.cmake/*.pc file is installed at all, unlike every other vcpkg
# dependency this project uses): `find_package(clickhouse-cpp CONFIG)` does not work for this
# port. Linked manually as an IMPORTED target instead, against its own real upstream CMakeLists.txt
# (vcpkg's buildtree copy of clickhouse-cpp v2.6.2) to confirm its exact real dependency set
# (absl::int128, cityhash, lz4::lz4, zstd::libzstd) rather than guessing which abseil components a
# static-link would actually need.
find_package(absl CONFIG REQUIRED)
find_package(cityhash CONFIG REQUIRED)
find_package(lz4 CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)

find_library(CLICKHOUSE_CPP_LIBRARY NAMES clickhouse-cpp-lib REQUIRED)
find_path(CLICKHOUSE_CPP_INCLUDE_DIR NAMES clickhouse/client.h REQUIRED)

add_library(clickhouse_cpp STATIC IMPORTED)
set_target_properties(clickhouse_cpp PROPERTIES
    IMPORTED_LOCATION "${CLICKHOUSE_CPP_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${CLICKHOUSE_CPP_INCLUDE_DIR}"
)
target_link_libraries(clickhouse_cpp INTERFACE absl::int128 cityhash lz4::lz4 zstd::libzstd)

# libpqxx (P4.5/ADR-0060: E5 RatingDecision audit table, docs/DATA_MODEL.md's PostgreSQL
# assignment) -- CHF's first real PostgreSQL connection.
find_package(libpqxx CONFIG REQUIRED)

add_executable(chf src/main.cpp src/stores.cpp src/cdr.cpp src/diameter_server.cpp src/rating_decision_store.cpp src/charging_engine.cpp)
target_link_libraries(chf PRIVATE sbi_core sbi_generated bss_sid redis++::redis++_static clickhouse_cpp diameter_core libpqxx::pqxx)
# See nfs/hello-nf/CMakeLists.txt for why this is a compile-time absolute path.
target_compile_definitions(chf PRIVATE CERTS_DIR="${CMAKE_SOURCE_DIR}/certs")
