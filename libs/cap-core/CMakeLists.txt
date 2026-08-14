# CAP (3GPP TS 29.078, CAMEL Application Part) operation-argument codec -- the real gsmSSF/gsmSCF
# circuit-switched call-control operations needed for the prepaid-charging flow (InitialDP ->
# RequestReportBCSMEvent -> ApplyCharging -> EventReportBCSM -> ApplyChargingReport). Builds on
# tcap_core's own BER primitives and Invoke/Component framing; does not duplicate them.
# P4.5/ADR-0059 Stage 6 kickoff. Real, disclosed field-subset scope -- see cap_operations.hpp's own
# header comment.

add_library(cap_core STATIC
    src/cap_types.cpp
    src/cap_operations.cpp
)

target_include_directories(cap_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(cap_core PUBLIC
    5gc_project_options
    tcap_core
)

target_compile_features(cap_core PUBLIC cxx_std_23)
