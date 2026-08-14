# M3UA (RFC 4666, MTP3-User Adaptation Layer) + SCCP (ITU-T Q.773) message codec, plus a real
# kernel SCTP transport primitive (M3UA's own real port 2905/PPID 3). Hand-rolled rather than
# linking Osmocom's real libosmo-sccp/libosmocore -- see docs/DECISIONS.md's own Stage 5 ADR
# update for the full real license-check evidence (GPL-2+, incompatible with linking into this
# project's Apache-2.0 code) and the real, vendored arms-length reference material
# (simulators/reference/osmocom/) every M3UA/SCCP constant in
# m3ua_dictionary.hpp/sccp_dictionary.hpp is grounded in. `sctp_socket.cpp` mirrors
# libs/ngap-core's own real, already-working kernel-SCTP-socket pattern (this project's own code,
# not a third-party dependency) with M3UA's own real PPID/port in place of NGAP's -- see
# docs/DECISIONS.md ADR-0030 for the original SCTP-via-system-library decision this reuses.

find_path(SCTP_INCLUDE_DIR netinet/sctp.h)
find_library(SCTP_LIBRARY sctp)
if(NOT SCTP_INCLUDE_DIR OR NOT SCTP_LIBRARY)
    message(FATAL_ERROR
        "System SCTP library (libsctp-dev) not found. Install via 'apt install libsctp-dev'. "
        "See docs/DECISIONS.md ADR-0030.")
endif()

add_library(ss7_core STATIC
    src/m3ua_header.cpp
    src/m3ua_tlv.cpp
    src/m3ua_protocol_data.cpp
    src/m3ua_asp.cpp
    src/sccp_address.cpp
    src/sccp_udt.cpp
    src/sctp_socket.cpp
)

target_include_directories(ss7_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(ss7_core PUBLIC
    5gc_project_options
    ${SCTP_LIBRARY}
)

target_compile_features(ss7_core PUBLIC cxx_std_23)
