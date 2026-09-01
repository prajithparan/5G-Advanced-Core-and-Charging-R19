# Hand-written C++ wrapper around the system kernel SCTP socket API (libsctp-dev), used for AMF's
# N2/NGAP transport. No vcpkg dependency: vcpkg has no kernel-SCTP-socket port (only `usrsctp`, an
# unrelated userspace-over-UDP stack) -- this links the system library directly. See
# docs/DECISIONS.md ADR-0030.

find_path(SCTP_INCLUDE_DIR netinet/sctp.h)
find_library(SCTP_LIBRARY sctp)
if(NOT SCTP_INCLUDE_DIR OR NOT SCTP_LIBRARY)
    message(FATAL_ERROR
        "System SCTP library (libsctp-dev) not found. Install via 'apt install libsctp-dev'. "
        "See docs/DECISIONS.md ADR-0030.")
endif()

# ADR-0249: ngap_codec moved here from nfs/amf/src/. These ProtocolIE-Container encode/decode
# helpers are generic NGAP codec infrastructure, not AMF policy -- and nfs/smf now needs them too
# (it builds a real PDUSessionResourceSetupRequestTransfer for N2 handover). CLAUDE.md forbids one
# NF including another NF's private headers, so the correct fix is promoting shared protocol code
# into the shared library rather than duplicating ~60 lines of ASN.1 boilerplate into a second NF.
add_library(ngap_core STATIC
    src/sctp_socket.cpp
    src/ngap_codec.cpp
)

target_include_directories(ngap_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(ngap_core PUBLIC
    5gc_project_options
    ngap_generated
    ${SCTP_LIBRARY}
)

target_compile_features(ngap_core PUBLIC cxx_std_23)
