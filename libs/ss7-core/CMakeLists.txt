# M3UA (RFC 4666, MTP3-User Adaptation Layer) + SCCP (ITU-T Q.713) message codec. Pure codec
# library, no transport of its own yet (Stage 5a of ADR-0059's own staged plan -- SCTP transport,
# TCAP, and MAP/CAP are later, not-yet-built stages, same "wire-codec-first" pattern already
# established for diameter-core/pfcp_core). Hand-rolled rather than linking Osmocom's real
# libosmo-sccp/libosmocore -- see docs/DECISIONS.md's own Stage 5 ADR update for the full real
# license-check evidence (GPL-2+, incompatible with linking into this project's Apache-2.0 code)
# and the real, vendored arms-length reference material (simulators/reference/osmocom/) every
# M3UA/SCCP constant in m3ua_dictionary.hpp/sccp_dictionary.hpp is grounded in.

add_library(ss7_core STATIC
    src/m3ua_header.cpp
    src/m3ua_tlv.cpp
    src/m3ua_protocol_data.cpp
    src/sccp_address.cpp
    src/sccp_udt.cpp
)

target_include_directories(ss7_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(ss7_core PUBLIC
    5gc_project_options
)

target_compile_features(ss7_core PUBLIC cxx_std_23)
