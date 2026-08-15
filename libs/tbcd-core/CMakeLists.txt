# TBCD-STRING codec (TS 23.003 clause 2.2) -- genuinely new to this codebase, needed at the
# legacy MAP/CAP wire boundary (IMSI/MSISDN/dialled-number fields) where subscriber identity is
# TBCD-packed bytes, not the plain SUPI digit string 5G SBI JSON uses everywhere else.
# P4.5/ADR-0061.

add_library(tbcd_core STATIC
    src/tbcd.cpp
)

target_include_directories(tbcd_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(tbcd_core PUBLIC
    5gc_project_options
)

target_compile_features(tbcd_core PUBLIC cxx_std_23)
