# PFCP (N4/Sx, TS 29.244) message header + Information Element TLV codec. Pure codec library, no
# transport of its own -- unlike libs/ngap-core's SCTP (which Boost.Asio doesn't support natively),
# PFCP runs over plain UDP, which Boost.Asio already supports directly (boost::asio::ip::udp), so
# NFs using this library talk UDP themselves rather than needing a dedicated wrapper class here.
# See docs/DECISIONS.md ADR-0039.

add_library(pfcp_core STATIC
    src/header.cpp
    src/ie.cpp
    src/common_ies.cpp
    src/session_ies.cpp
)

target_include_directories(pfcp_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(pfcp_core PUBLIC
    5gc_project_options
)

target_compile_features(pfcp_core PUBLIC cxx_std_23)
