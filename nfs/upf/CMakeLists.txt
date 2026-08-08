find_package(PkgConfig REQUIRED)
pkg_check_modules(libbpf REQUIRED IMPORTED_TARGET libbpf)

# Compiles the XDP program to BPF bytecode (an ELF object with a "xdp" section, per convention --
# not a normal x86 object) via clang's -target bpf backend. This is NOT the same clang invocation
# CMake uses for the rest of this project's C++ -- BPF bytecode compilation is architecture-
# specific and has nothing to do with this build's normal compiler/toolchain settings. See
# docs/DECISIONS.md ADR-0043.
find_program(CLANG_EXECUTABLE NAMES clang clang-18 REQUIRED)
set(GTPU_BPF_SRC ${CMAKE_CURRENT_SOURCE_DIR}/bpf/gtpu_decap.bpf.c)
set(GTPU_BPF_OBJ ${CMAKE_CURRENT_BINARY_DIR}/gtpu_decap.bpf.o)
add_custom_command(
    OUTPUT ${GTPU_BPF_OBJ}
    COMMAND ${CLANG_EXECUTABLE} -O2 -g -target bpf -D__TARGET_ARCH_x86
            -I/usr/include/x86_64-linux-gnu
            -c ${GTPU_BPF_SRC} -o ${GTPU_BPF_OBJ}
    DEPENDS ${GTPU_BPF_SRC}
    COMMENT "Compiling gtpu_decap.bpf.c to BPF bytecode"
    VERBATIM
)
add_custom_target(gtpu_bpf_obj ALL DEPENDS ${GTPU_BPF_OBJ})

add_executable(upf src/main.cpp src/datapath.cpp)
add_dependencies(upf gtpu_bpf_obj)
target_link_libraries(upf PRIVATE sbi_core sbi_generated pfcp_core PkgConfig::libbpf)
# See nfs/hello-nf/CMakeLists.txt for why this is a compile-time absolute path.
target_compile_definitions(upf PRIVATE
    CERTS_DIR="${CMAKE_SOURCE_DIR}/certs"
    GTPU_BPF_OBJ_PATH="${GTPU_BPF_OBJ}"
)
