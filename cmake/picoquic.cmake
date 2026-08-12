# picoquic, from the gateway edge's vendored copy.
#
# The file list and the reasoning behind it are `thirdparty/gateway-edge/cmake/picoquic.cmake`,
# which is vendored byte-identical and therefore cannot be edited here. It is not `include()`d
# because it resolves its sources against `CMAKE_SOURCE_DIR/thirdparty/picoquic`, which is that
# repository's layout and not this one's. So the roots are set here and the rest mirrors it.
#
# Mirroring rather than editing is what keeps the subtree pullable. A fix belongs upstream in
# `fabric-gateway-edge`, and a divergence here would be lost at the next `git subtree pull`.

set(EDGE_ROOT ${CMAKE_SOURCE_DIR}/thirdparty/gateway-edge)
set(PICOQUIC_ROOT ${EDGE_ROOT}/thirdparty/picoquic)
set(PICOTLS_ROOT ${EDGE_ROOT}/thirdparty/picotls)

find_path(OPENSSL_INCLUDE openssl/ssl.h REQUIRED)
find_library(CRYPTO_LIB crypto REQUIRED)

file(GLOB PICOQUIC_CORE_SOURCES "${PICOQUIC_ROOT}/picoquic/*.c")
file(GLOB PICOHTTP_SOURCES "${PICOQUIC_ROOT}/picohttp/*.c")

# Fusion is an x86-only AES-GCM engine behind its own `#if !defined(PTLS_WITHOUT_FUSION)`, and
# winsockloop.c is Windows-only. minicrypto stays: `picoquic_tls_api_init_providers()` calls
# `picoquic_ptls_minicrypto_load()` with no compile-time guard, so dropping it is a link error
# rather than a smaller build. OpenSSL is still the provider that wins, because the latest
# registration wins and mbedtls's glue is not compiled in.
list(FILTER PICOQUIC_CORE_SOURCES EXCLUDE REGEX ".*picoquic_ptls_fusion\\.c$")
list(FILTER PICOQUIC_CORE_SOURCES EXCLUDE REGEX ".*winsockloop\\.c$")

set(PICOTLS_SOURCES
    ${PICOTLS_ROOT}/lib/picotls.c
    ${PICOTLS_ROOT}/lib/pembase64.c
    ${PICOTLS_ROOT}/lib/hpke.c
    ${PICOTLS_ROOT}/lib/asn1.c
    ${PICOTLS_ROOT}/lib/openssl.c
)

set(PICOTLS_MINICRYPTO_SOURCES
    ${PICOTLS_ROOT}/deps/micro-ecc/uECC.c
    ${PICOTLS_ROOT}/deps/cifra/src/aes.c
    ${PICOTLS_ROOT}/deps/cifra/src/blockwise.c
    ${PICOTLS_ROOT}/deps/cifra/src/chacha20.c
    ${PICOTLS_ROOT}/deps/cifra/src/chash.c
    ${PICOTLS_ROOT}/deps/cifra/src/curve25519.c
    ${PICOTLS_ROOT}/deps/cifra/src/drbg.c
    ${PICOTLS_ROOT}/deps/cifra/src/hmac.c
    ${PICOTLS_ROOT}/deps/cifra/src/gcm.c
    ${PICOTLS_ROOT}/deps/cifra/src/gf128.c
    ${PICOTLS_ROOT}/deps/cifra/src/modes.c
    ${PICOTLS_ROOT}/deps/cifra/src/poly1305.c
    ${PICOTLS_ROOT}/deps/cifra/src/sha256.c
    ${PICOTLS_ROOT}/deps/cifra/src/sha512.c
    ${PICOTLS_ROOT}/lib/cifra.c
    ${PICOTLS_ROOT}/lib/cifra/x25519.c
    ${PICOTLS_ROOT}/lib/cifra/chacha20.c
    ${PICOTLS_ROOT}/lib/cifra/aes128.c
    ${PICOTLS_ROOT}/lib/cifra/aes256.c
    ${PICOTLS_ROOT}/lib/cifra/random.c
    ${PICOTLS_ROOT}/lib/minicrypto-pem.c
    ${PICOTLS_ROOT}/lib/uecc.c
    ${PICOTLS_ROOT}/lib/ffx.c
)

add_library(picoquic_vendored STATIC
    ${PICOQUIC_CORE_SOURCES}
    ${PICOHTTP_SOURCES}
    ${PICOTLS_SOURCES}
    ${PICOTLS_MINICRYPTO_SOURCES}
)

# BEFORE is load-bearing upstream, where h2o installs a different picotls into the global include
# path. There is no h2o here, and it is kept anyway: this target must compile against the picotls
# beside it whatever else a future dependency puts on the path.
target_include_directories(picoquic_vendored BEFORE PUBLIC
    ${PICOTLS_ROOT}/include
    ${PICOQUIC_ROOT}/picoquic
    ${PICOQUIC_ROOT}/picohttp
    ${OPENSSL_INCLUDE}
)

# cifra's headers include each other as "ext/..." and "bitops.h", which only resolves with these
# on the path, and micro-ecc needs its own directory.
target_include_directories(picoquic_vendored PRIVATE
    ${PICOTLS_ROOT}/deps/cifra/src/ext
    ${PICOTLS_ROOT}/deps/cifra/src
    ${PICOTLS_ROOT}/deps/micro-ecc
)

target_compile_definitions(picoquic_vendored PUBLIC PTLS_WITHOUT_FUSION DISABLE_DEBUG_PRINTF)

# A static archive only satisfies symbols from libraries placed after it on the link line, so
# this goes through the dependency graph rather than a list at the top level.
target_link_libraries(picoquic_vendored PUBLIC ${CRYPTO_LIB} ${CMAKE_DL_LIBS})

# The vendored tree is somebody else's code and its warnings are not this repository's to fix.
target_compile_options(picoquic_vendored PRIVATE -w)
