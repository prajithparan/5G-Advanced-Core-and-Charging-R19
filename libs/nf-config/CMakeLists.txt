# nf-config: shared config-file loader (docs/DECISIONS.md ADR-0077, user-directed, mandatory) --
# no DB URL or connection/deployment parameter may be a hardcoded literal default in an NF's
# source file. Header-only: just an include path plus nlohmann-json, no .cpp needed.

add_library(nf_config INTERFACE)

target_include_directories(nf_config INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(nf_config INTERFACE nlohmann_json::nlohmann_json)

target_compile_features(nf_config INTERFACE cxx_std_23)
