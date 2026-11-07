idf_component_register(
    SRCS "src/crypto.c"
         "src/wireguard.c"
         "src/wireguardif.c"
         "src/wireguard-platform.c"
         "src/crypto/refc/blake2s.c"
         "src/crypto/refc/chacha20.c"
         "src/crypto/refc/chacha20poly1305.c"
         "src/crypto/refc/poly1305-donna.c"
         "src/crypto/refc/x25519.c"
         "src/esp_wireguard.c"
         "src/nacl/crypto_scalarmult/curve25519/ref/smult.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_netif lwip esp_hw_support)

# (arcade) ESP-IDF 6.1 / GCC 15: these warnings of the old code are not errors
set_source_files_properties(src/crypto/refc/x25519.c
    PROPERTIES COMPILE_FLAGS -Wno-error=stringop-overread)
set_source_files_properties(src/wireguard.c
    PROPERTIES COMPILE_FLAGS "-Wno-error=unterminated-string-initialization -Wno-unterminated-string-initialization")
set_source_files_properties(src/nacl/crypto_scalarmult/curve25519/ref/smult.c
    PROPERTIES COMPILE_FLAGS -Wno-unused-variable)
