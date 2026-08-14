# MAP (3GPP TS 29.002, Mobile Application Part) insertSubscriberData operation-argument codec --
# the real HLR->VLR mechanism that provisions O-CSI/D-CSI CAMEL Subscription Info, which is what
# causes a real switch to invoke CAP's InitialDP (libs/cap-core). Builds on tcap_core's own BER
# primitives and Invoke/Component framing; does not duplicate them. P4.5/ADR-0059 Stage 7 kickoff.
# Real, disclosed field-subset scope -- see map_operations.hpp's own header comment.

add_library(map_core STATIC
    src/map_operations.cpp
)

target_include_directories(map_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(map_core PUBLIC
    5gc_project_options
    tcap_core
)

target_compile_features(map_core PUBLIC cxx_std_23)
