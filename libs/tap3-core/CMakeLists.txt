# TAP3 (GSMA TD.57, "TAP 3.12 Format Specification") roaming CDR codec -- P4.11's first slice
# (ADR-0067). Real, disclosed source-material constraint: TAP-SPEC.pdf is GSMA member-confidential
# ("Confidential - Full, Rapporteur, Associate and Affiliate Members"), unlike the freely-published
# 3GPP TS PDFs already committed to this repo -- so, unlike NGAP (asn1c-generated from a freely
# redistributable UERANSIM-vendored .asn file, ADR-0030/0031), this codec is hand-rolled the same
# way TCAP/MAP/CAP already are: real field names/tag numbers/clause references cited in code
# comments, no verbatim spec excerpts, no vendored .asn file. Builds on tcap_core's own generic
# X.690 BER primitives (Tlv/encode_tlv/decode_tlv/decode_tlvs/encode_integer/decode_integer) --
# nothing TCAP-specific in that particular file, a deliberate reuse decision, not duplication.

add_library(tap3_core STATIC
    src/tap3_common.cpp
    src/tap3_charging.cpp
    src/tap3_envelope.cpp
    src/tap3_mo_call.cpp
    src/tap3_mt_call.cpp
    src/tap3_suppl_service.cpp
    src/tap3_scu.cpp
    src/tap3_gprs_call.cpp
    src/tap3_content_transaction.cpp
    src/tap3_location_service.cpp
    src/tap3_messaging_event.cpp
    src/tap3_mobile_session.cpp
    src/tap3_aggregated_usage.cpp
)

target_include_directories(tap3_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(tap3_core PUBLIC
    5gc_project_options
    tcap_core
)

target_compile_features(tap3_core PUBLIC cxx_std_23)
