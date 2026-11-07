# third_party/zstd: the zstd decoder (Zstandard 1.5.6, Meta Platforms; the BSD licence in LICENSE)

- `zstddeclib.c`: the single-file decoder, made from the release with its own tool:
  `python build/single_file_libs/combine.py -r lib -x legacy/zstd_legacy.h -o zstddeclib.c build/single_file_libs/zstddeclib-in.c`
  (the release zstd-1.5.6.tar.gz, SHA-256 8c29e06cf42aacc1eafc4077ae2ec6c6fcb96a626157e0593d5e82a34fd403c1).
- `zstd.h`: the header of the same release.
- The users: `libs/zimread` (the ZIM files of WIKI and DICTIONARY). `zstd` is a module of the firmware: the stable
  decoder functions of `zstd.h` (`ZSTD_decompress`, `ZSTD_createDCtx`, `ZSTD_decompressStream` ...) are ABI names
  (main/tfn_abi_extra.json). An app includes `zstd.h` and calls the firmware (no copy of `zstddeclib.c` in the app file).
- Not changed. Compile it with `-DZSTD_DISABLE_ASM` (no x86 assembler on the boards).
