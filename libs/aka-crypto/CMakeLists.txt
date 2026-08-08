find_package(OpenSSL REQUIRED)

add_library(aka_crypto STATIC
    src/milenage.cpp
    src/kdf.cpp
    src/eap_aka_prime.cpp
    src/hex.cpp
    src/nas_security.cpp
)

target_include_directories(aka_crypto PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(aka_crypto PUBLIC
    5gc_project_options
    OpenSSL::Crypto
)

target_compile_features(aka_crypto PUBLIC cxx_std_23)
