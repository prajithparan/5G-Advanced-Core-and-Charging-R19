# TCAP (ITU-T Q.773, Transaction Capabilities Application Part) message codec -- the generic
# dialogue/component layer real MAP/CAP operations ride inside. Pure codec library, no SCCP/M3UA
# transport wiring yet (P4.5/ADR-0059 Stage 5b kickoff -- MAP/CAP operation-specific ASN.1 types
# are a further, later increment; TCAP itself treats their argument/result bytes as opaque). Hand-
# rolled rather than linking RestComm's real jss7 -- see docs/DECISIONS.md's own Stage 5b ADR
# update for the full real license-check evidence (AGPL-3.0, incompatible with linking into this
# project's Apache-2.0 code, same conclusion Stage 5a already reached for Osmocom/SCCP) and the
# real, vendored arms-length reference material (simulators/reference/jss7/) every tag constant in
# tag_constants.hpp is grounded in.

add_library(tcap_core STATIC
    src/ber.cpp
    src/component.cpp
    src/message.cpp
    src/dialogue_portion.cpp
)

target_include_directories(tcap_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(tcap_core PUBLIC
    5gc_project_options
)

target_compile_features(tcap_core PUBLIC cxx_std_23)
