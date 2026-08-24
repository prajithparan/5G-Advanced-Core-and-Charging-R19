# Wires tools/sbi-codegen into the build: generates C++ DTOs + JSON
# serializers from the R19 3GPP OpenAPI YAML and compiles them into the
# sbi_generated library. See docs/DECISIONS.md ADR-0010 for why this exists
# (a custom generator) instead of openapi-generator.
#
# KNOWN LIMITATION: relies on `python3` on PATH having jinja2 and pyyaml
# installed (not vcpkg-managed -- there is no vcpkg C++ substitute for a
# Python code generator). CI installs these explicitly; see
# .github/workflows/ci.yml. Adding or removing a *pilot* YAML file, or a
# schema that changes which output file-group appears, needs a CMake
# re-configure for the file(GLOB) below to notice new/removed generated
# files -- a known limitation of globbing dynamically-generated sources,
# not silently hidden.

set(SBI_CODEGEN_SCRIPT ${CMAKE_SOURCE_DIR}/tools/sbi-codegen/generate.py)
set(SBI_CODEGEN_SPECS_DIR ${CMAKE_SOURCE_DIR}/specs/5G_APIs-REL-19)
set(SBI_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated/sbi_gen)

set(SBI_CODEGEN_PILOT_FILES
    TS29510_Nnrf_NFManagement.yaml
    TS29510_Nnrf_NFDiscovery.yaml
    TS29510_Nnrf_AccessToken.yaml
    TS29518_Namf_Communication.yaml
    TS29502_Nsmf_PDUSession.yaml
    TS29503_Nudm_UECM.yaml
    TS29503_Nudm_SDM.yaml
    TS29503_Nudm_UEAU.yaml
    TS29503_Nudm_EE.yaml
    TS29503_Nudm_PP.yaml
    TS29505_Subscription_Data.yaml
    TS29509_Nausf_UEAuthentication.yaml
    TS29507_Npcf_AMPolicyControl.yaml
    TS29512_Npcf_SMPolicyControl.yaml
    TS29514_Npcf_PolicyAuthorization.yaml
    TS29509_Nausf_SoRProtection.yaml
    TS32291_Nchf_ConvergedCharging.yaml
    TS32291_Nchf_OfflineOnlyCharging.yaml
    TS29594_Nchf_SpendingLimitControl.yaml
    TS29519_Policy_Data.yaml
    TS29571_CommonData.yaml
    TS29531_Nnssf_NSSelection.yaml
    TS29531_Nnssf_NSSAIAvailability.yaml
)
set(SBI_CODEGEN_PILOT_PATHS
    ${SBI_CODEGEN_SPECS_DIR}/TS29510_Nnrf_NFManagement.yaml
    ${SBI_CODEGEN_SPECS_DIR}/TS29510_Nnrf_NFDiscovery.yaml
    ${SBI_CODEGEN_SPECS_DIR}/TS29510_Nnrf_AccessToken.yaml
    ${SBI_CODEGEN_SPECS_DIR}/TS29502_Nsmf_PDUSession.yaml
    ${SBI_CODEGEN_SPECS_DIR}/TS29518_Namf_Communication.yaml
    ${SBI_CODEGEN_SPECS_DIR}/TS29503_Nudm_UECM.yaml
    ${SBI_CODEGEN_SPECS_DIR}/TS29503_Nudm_SDM.yaml
    ${SBI_CODEGEN_SPECS_DIR}/TS29503_Nudm_UEAU.yaml
    ${SBI_CODEGEN_SPECS_DIR}/TS29503_Nudm_EE.yaml
    ${SBI_CODEGEN_SPECS_DIR}/TS29503_Nudm_PP.yaml
    ${SBI_CODEGEN_SPECS_DIR}/TS29505_Subscription_Data.yaml
    ${SBI_CODEGEN_SPECS_DIR}/TS29509_Nausf_UEAuthentication.yaml
    ${SBI_CODEGEN_SPECS_DIR}/TS29507_Npcf_AMPolicyControl.yaml
    ${SBI_CODEGEN_SPECS_DIR}/TS29512_Npcf_SMPolicyControl.yaml
    ${SBI_CODEGEN_SPECS_DIR}/TS29514_Npcf_PolicyAuthorization.yaml
    ${SBI_CODEGEN_SPECS_DIR}/TS29509_Nausf_SoRProtection.yaml
    ${SBI_CODEGEN_SPECS_DIR}/TS32291_Nchf_ConvergedCharging.yaml
    ${SBI_CODEGEN_SPECS_DIR}/TS32291_Nchf_OfflineOnlyCharging.yaml
    ${SBI_CODEGEN_SPECS_DIR}/TS29594_Nchf_SpendingLimitControl.yaml
    ${SBI_CODEGEN_SPECS_DIR}/TS29519_Policy_Data.yaml
    ${SBI_CODEGEN_SPECS_DIR}/TS29571_CommonData.yaml
    ${SBI_CODEGEN_SPECS_DIR}/TS29531_Nnssf_NSSelection.yaml
    ${SBI_CODEGEN_SPECS_DIR}/TS29531_Nnssf_NSSAIAvailability.yaml
)

file(GLOB_RECURSE SBI_CODEGEN_TOOL_SOURCES
    ${CMAKE_SOURCE_DIR}/tools/sbi-codegen/*.py
    ${CMAKE_SOURCE_DIR}/tools/sbi-codegen/templates/*.j2
)

# Real bug found and fixed (2026-08-16, while adding TS29519_Policy_Data.yaml as a pilot file):
# generate.py only ever WRITES files, never deletes ones from a previous run -- so a pilot-file-
# list change that moves a type from its own standalone header into a merged SCC group (or vice
# versa, see render.py's own module docstring on SCC grouping) leaves the OLD, now-stale header
# behind on disk. file(GLOB) below then picks up BOTH the stale standalone header and the new
# merged one, and the same types get compiled twice -- a real, reproducible "redefinition of
# struct" build failure, not a hypothetical one. Removing the whole output directory first, both
# here and in the build-time add_custom_command below, makes every regeneration a clean one --
# this was already flagged as a known limitation in this file's own header comment before being
# actually hit; fixed properly (clean-slate regeneration) rather than left as a recurring trap.
file(REMOVE_RECURSE ${SBI_GENERATED_DIR})

# Run once at configure time so the file(GLOB) below has output to find.
execute_process(
    COMMAND python3 ${SBI_CODEGEN_SCRIPT}
            ${SBI_CODEGEN_PILOT_FILES}
            --specs-dir ${SBI_CODEGEN_SPECS_DIR}
            --out ${SBI_GENERATED_DIR}
    RESULT_VARIABLE SBI_CODEGEN_RESULT
)
if(NOT SBI_CODEGEN_RESULT EQUAL 0)
    message(FATAL_ERROR "tools/sbi-codegen/generate.py failed at configure time (see output above)")
endif()

file(GLOB SBI_GENERATED_SOURCES ${SBI_GENERATED_DIR}/*.cpp)
file(GLOB SBI_GENERATED_HEADERS ${SBI_GENERATED_DIR}/*.hpp)

# Re-run automatically on rebuild whenever the generator, its templates, or
# the pilot YAML change.
add_custom_command(
    OUTPUT ${SBI_GENERATED_DIR}/.stamp
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${SBI_GENERATED_DIR}
    COMMAND python3 ${SBI_CODEGEN_SCRIPT}
            ${SBI_CODEGEN_PILOT_FILES}
            --specs-dir ${SBI_CODEGEN_SPECS_DIR}
            --out ${SBI_GENERATED_DIR}
    COMMAND ${CMAKE_COMMAND} -E touch ${SBI_GENERATED_DIR}/.stamp
    DEPENDS ${SBI_CODEGEN_TOOL_SOURCES} ${SBI_CODEGEN_PILOT_PATHS}
    COMMENT "Regenerating SBI DTOs from 3GPP R19 OpenAPI YAML (tools/sbi-codegen)"
)
add_custom_target(sbi_codegen_run DEPENDS ${SBI_GENERATED_DIR}/.stamp)

add_library(sbi_generated STATIC ${SBI_GENERATED_SOURCES})
add_dependencies(sbi_generated sbi_codegen_run)
target_include_directories(sbi_generated PUBLIC ${SBI_GENERATED_DIR})
target_link_libraries(sbi_generated PUBLIC sbi_core)
target_compile_features(sbi_generated PUBLIC cxx_std_23)
# Generated code doesn't get the hand-written-code warning treatment.
target_compile_options(sbi_generated PRIVATE -w)
