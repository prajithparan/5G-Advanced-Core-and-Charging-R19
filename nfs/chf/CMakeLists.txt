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

# onnxruntime (P4.8/ADR-0074: real in-process C++ inference for predictive quota sizing).
# find_package(onnxruntime CONFIG) genuinely works for this port (unlike clickhouse-cpp above) --
# confirmed by successful compilation. Real, disclosed static-linking bug found via this ADR's own
# unit tests (tests/conformance/test_ai_inference.cpp): a plain static link of ONNX::onnx (pulled
# in transitively by onnxruntime::onnxruntime) drops most of its operator-schema
# self-registration object files, since nothing in this executable directly references their
# symbols (ar-style static archives only pull in .o files that resolve an undefined symbol) --
# ONNX Runtime then fails to load ANY real model with "SchemasRegisterer" assertion failures ("4
# schema were exposed... 741 were expected"). Fixed with CMake's portable whole-archive link-library
# generator expression, forcing every object file in libonnx.a into the final binary regardless of
# whether anything references it directly -- the standard real fix for self-registering-global
# static libraries, not a workaround specific to this project.
find_package(onnxruntime CONFIG REQUIRED)
find_package(ONNX CONFIG REQUIRED)

add_executable(chf src/main.cpp src/stores.cpp src/cdr.cpp src/cdr_asn1.cpp src/diameter_server.cpp src/rating_decision_store.cpp src/charging_engine.cpp src/cap_server.cpp src/ai_inference.cpp)
target_link_libraries(chf PRIVATE sbi_core sbi_generated bss_sid redis++::redis++_static clickhouse_cpp diameter_core libpqxx::pqxx ss7_core tcap_core cap_core tbcd_core onnxruntime::onnxruntime "$<LINK_LIBRARY:WHOLE_ARCHIVE,ONNX::onnx>" nf_config)
# See nfs/hello-nf/CMakeLists.txt for why this is a compile-time absolute path.
# CONFIG_DIR: docs/DECISIONS.md ADR-0077 -- separate checked-in config/chf.json (partial retrofit
# this turn, see main.cpp's own comment for what's deliberately still deferred).
target_compile_definitions(chf PRIVATE CERTS_DIR="${CMAKE_SOURCE_DIR}/certs" CONFIG_DIR="${CMAKE_SOURCE_DIR}/config")
