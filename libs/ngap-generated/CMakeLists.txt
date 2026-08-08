# Generates the NGAP (N2 interface, TS 38.413) ASN.1 PER codec at configure time via asn1c and
# compiles it into the ngap_generated library. Mirrors libs/sbi-generated's configure-time-codegen
# CMake shape (execute_process now, add_custom_command to regenerate on source change) -- the
# codegen *tool* is entirely different (asn1c compiling an ASN.1 module, not tools/sbi-codegen's
# Python OpenAPI pipeline), since NGAP has no OpenAPI representation to generate from. See
# docs/DECISIONS.md ADR-0030.
#
# asn1c is vanilla asn1c 0.9.29 (official release tarball, no sudo/autotools needed), built to
# build-tools/asn1c/. Vanilla asn1c only ships Unaligned PER (uper_*) at runtime -- real gNB/UE
# peers speaking TS 38.413 Aligned PER cannot decode that, confirmed empirically against
# UERANSIM's real nr-gnb during Stage 1 bring-up. Rather than depend on a different asn1c fork
# (tried and abandoned -- osmocom/asn1c's aper-prefix branch has real Aligned PER but its compiler
# cannot parse the real NGAP-17.9 module, and its runtime skeleton is ABI-incompatible with
# vanilla 0.9.29's newer asn_bit_data_t-based architecture, so the two cannot be mixed), this
# project patches vanilla asn1c 0.9.29's own skeleton sources in place
# (build-tools/asn1c/share/asn1c/{asn_bit_data,per_support,per_encoder,per_decoder,INTEGER,
# NativeEnumerated,constr_SEQUENCE_OF,constr_SET_OF,constr_CHOICE,OCTET_STRING,BIT_STRING}.{c,h})
# to add real Aligned PER support (aper_encode_to_new_buffer/aper_decode_complete plus the X.691
# octet-alignment logic they need), self-authored from the X.691 spec text (not copied from any
# other asn1c fork) -- see docs/DECISIONS.md ADR-0031 for exactly what was added and why. This
# also means asn1c's own compiler binary needed no changes: the same per-type C descriptor tables
# it always generated now pick up Aligned PER behavior at runtime, selected by which top-level
# entry point (aper_* vs uper_*) the caller invokes, with zero type-code regeneration.
#
# Not vcpkg-manageable (it's a C compiler-generator tool, not a library) and not guaranteed to be
# on PATH -- CI is expected to build it the same way (see build-tools/asn1c/ setup, gitignored,
# matching the existing build-*/ ignore pattern, not committed like any other local build tool).

find_program(ASN1C_EXECUTABLE asn1c
    PATHS ${CMAKE_SOURCE_DIR}/build-tools/asn1c/bin
    DOC "Path to vanilla asn1c 0.9.29, patched in place for Aligned PER (BSD-licensed, https://github.com/vlm/asn1c). See ADR-0031."
)
if(NOT ASN1C_EXECUTABLE)
    message(FATAL_ERROR
        "asn1c not found on PATH or at ${CMAKE_SOURCE_DIR}/build-tools/asn1c/bin. Build from "
        "https://github.com/vlm/asn1c/releases (a release tarball, not a git checkout, ships a "
        "pre-generated configure script needing no autoconf/bison/flex) into build-tools/asn1c/, "
        "then apply this project's Aligned PER patch to its share/asn1c/ skeleton sources -- see "
        "docs/DECISIONS.md ADR-0030/ADR-0031.")
endif()

set(NGAP_ASN1_MODULE ${CMAKE_SOURCE_DIR}/specs/NGAP/ngap-17.9.asn)
set(NGAP_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated/ngap_gen)

# Same asn1c invocation flags UERANSIM's own vendored NGAP codec was generated with (confirmed via
# that codec's own generated-file header comment citing this exact command line) -- proven correct
# against this exact ASN.1 module, not guessed.
set(NGAP_CODEGEN_FLAGS
    -pdu=all -fcompound-names -findirect-choice -fno-include-deps
    -no-gen-OER -gen-PER -no-gen-example
)

file(MAKE_DIRECTORY ${NGAP_GENERATED_DIR})

# Run once at configure time so the file(GLOB) below has output to find.
execute_process(
    COMMAND ${ASN1C_EXECUTABLE} ${NGAP_CODEGEN_FLAGS} -D ${NGAP_GENERATED_DIR} ${NGAP_ASN1_MODULE}
    WORKING_DIRECTORY ${NGAP_GENERATED_DIR}
    RESULT_VARIABLE NGAP_CODEGEN_RESULT
    OUTPUT_VARIABLE NGAP_CODEGEN_OUTPUT
    ERROR_VARIABLE NGAP_CODEGEN_OUTPUT
)
if(NOT NGAP_CODEGEN_RESULT EQUAL 0)
    message(FATAL_ERROR "asn1c failed generating the NGAP codec at configure time:\n${NGAP_CODEGEN_OUTPUT}")
endif()

# Re-run automatically on rebuild whenever the ASN.1 module changes.
add_custom_command(
    OUTPUT ${NGAP_GENERATED_DIR}/.stamp
    COMMAND ${ASN1C_EXECUTABLE} ${NGAP_CODEGEN_FLAGS} -D ${NGAP_GENERATED_DIR} ${NGAP_ASN1_MODULE}
    COMMAND ${CMAKE_COMMAND} -E touch ${NGAP_GENERATED_DIR}/.stamp
    WORKING_DIRECTORY ${NGAP_GENERATED_DIR}
    DEPENDS ${NGAP_ASN1_MODULE}
    COMMENT "Regenerating NGAP ASN.1 PER codec from ${NGAP_ASN1_MODULE} (asn1c)"
)
add_custom_target(ngap_codegen_run DEPENDS ${NGAP_GENERATED_DIR}/.stamp)

file(GLOB NGAP_GENERATED_SOURCES ${NGAP_GENERATED_DIR}/*.c)

add_library(ngap_generated STATIC ${NGAP_GENERATED_SOURCES})
add_dependencies(ngap_generated ngap_codegen_run)
target_include_directories(ngap_generated PUBLIC ${NGAP_GENERATED_DIR})
set_target_properties(ngap_generated PROPERTIES LINKER_LANGUAGE C)
# Generated C, not our own code -- no warnings-as-errors treatment, same as sbi_generated.
target_compile_options(ngap_generated PRIVATE -w)
# -no-gen-OER (above) only suppresses per-type OER function BODIES; the shared constr_TYPE.h
# skeleton still unconditionally #includes <oer_decoder.h>/<oer_encoder.h> unless this macro is
# defined, guarding that include with a dummy typedef instead (asn1c's own constr_TYPE.h has an
# #ifdef ASN_DISABLE_OER_SUPPORT/#else split around it). Confirmed necessary and correct against
# real evidence, not guessed: simulators/ransim/vendor/UERANSIM/src/asn/asn1c/CMakeLists.txt sets
# this exact define for its own identically-flagged (-no-gen-OER) NGAP codec build. PUBLIC because
# any consumer including these generated headers needs the same macro defined consistently.
target_compile_definitions(ngap_generated PUBLIC ASN_DISABLE_OER_SUPPORT)
