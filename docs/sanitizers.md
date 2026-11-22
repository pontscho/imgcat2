# Sanitizer builds

libimg2, the vendored codecs, the tests and imgcat2 can be built with AddressSanitizer + LeakSanitizer +
UndefinedBehaviorSanitizer or with ThreadSanitizer (CMake option `IMG2_SANITIZE`, default empty = off).

## Running

```sh
scripts/sanitize.sh asan                       # configure + build build-asan/ (clang), run ctest
scripts/sanitize.sh tsan                       # the same with ThreadSanitizer in build-tsan/
scripts/sanitize.sh asan --no-build --cli src/tests/data src/tests/fixtures   # + imgcat2 over every file
```

Options: `--no-build`, `--no-test`, `--cli <file-or-dir>...` (imgcat2 runs over every regular file with
typical option sets: ANSI, kitty and iTerm2 (JPEG and PNG) rendering, `--info --json --exif-detailed`, resize
with another filter, frame selection, conversion to PNG / JPEG / WebP (lossy, lossless) / HEIF / JXL (lossy,
lossless) into `$OUT_DIR/out/<n>/`, and a 3 s `-a` animation loop ended with SIGINT; `CLI_JOBS` files at a
time). Every sanitizer report of the sweep goes to `$OUT_DIR/logs/` (`log_path`) and fails the run, as does any
signal exit other than SIGINT and any run that exceeds `CLI_TIMEOUT` seconds.

Environment: `BUILD_DIR` (default `<repo>/build-asan` / `<repo>/build-tsan`), `OUT_DIR` (`$BUILD_DIR/sanitize`),
`JOBS` (16), `CTEST_JOBS` (8), `CLI_JOBS` (`$CTEST_JOBS`), `CLI_TIMEOUT` (900), `CC`/`CXX` (clang/clang++),
`TMPDIR` (`$BUILD_DIR/tmp`); `ASAN_OPTIONS`, `LSAN_OPTIONS`, `UBSAN_OPTIONS`, `TSAN_OPTIONS` replace the
defaults:

| Variable | Default |
|---|---|
| `ASAN_OPTIONS` | `detect_leaks=1:abort_on_error=1:strict_string_checks=1:detect_stack_use_after_return=1` |
| `LSAN_OPTIONS` | `suppressions=scripts/sanitizers/lsan.supp` |
| `UBSAN_OPTIONS` | `print_stacktrace=1:halt_on_error=1:suppressions=scripts/sanitizers/ubsan.supp` |
| `TSAN_OPTIONS` | `halt_on_error=1:second_deadlock_stack=1:suppressions=scripts/sanitizers/tsan.supp` |

(plus `external_symbolizer_path` when `llvm-symbolizer[-17]` is installed).

The ctest run excludes the labels `guard` (nm/objdump checks of the archive: the instrumented objects reference
the sanitizer runtimes, which the no-terminate and mutable-state guards reject by design), `consumer` (they install
or rebuild img2 in their own trees without the sanitizer flags), `expensive` and `no-sanitize`.

## What is instrumented

`cmake/Img2Sanitize.cmake` appends `-fsanitize=<list> -fno-omit-frame-pointer -U_FORTIFY_SOURCE` to
`IMG2_HARDENING_FLAGS`, so the flags reach every target the hardening flags reach: libimg2, the CLI and the tests
(`img2_apply_project_settings()`), the `add_subdirectory()` vendors (vendor section `COMPILE_OPTIONS`, C/C++
sources) and the ExternalProject vendors libjpeg-turbo and aom (`CMAKE_C_FLAGS` / `CMAKE_CXX_FLAGS`). The link
flag `-fsanitize=<list>` is a directory link option of the top-level project. Assembly sources (libjpeg-turbo
and aom SIMD, highway dispatch targets are C++) are not instrumented. Header-only vendors (stb, nanosvg, qoi)
are compiled inside libimg2's translation units.

libimg2's own targets add `-fno-sanitize-recover=undefined`: every UBSan finding in them aborts and cannot be
suppressed. The vendors keep clang's recoverable UBSan handlers, so a documented vendor finding can be listed in
`scripts/sanitizers/ubsan.supp`; with `halt_on_error=1` any other vendor finding still stops the run.

Deviations from the normal build, sanitizer configuration only:

| What | Why |
|---|---|
| dynamic link (no `-static` / `-static-pie`) for imgcat2 and the vendor probes | the sanitizer runtimes need the dynamic loader |
| `-U_FORTIFY_SOURCE` | fortified calls go to glibc's `__*_chk` entry points, which the sanitizer runtimes do not intercept |
| test timeouts x10 | instrumented codecs are several times slower |
| `IMG2_WERROR=OFF` (`scripts/sanitize.sh`) | clang 17 warnings the project's GCC does not have (see docs/coverage.md) |
| no OpenMP with clang (no libomp installed) | jpeg-quantsmooth and LibRaw run their loops single-threaded |

## Tests that cannot run under the sanitizers

| Test | Label / guard | Why |
|---|---|---|
| `test_cov_c_alloc` | `no-sanitize` | forks children of a process that has run kvazaar's worker threads and expects them to crash inside kvazaar after injected allocation failures: under ASan every such crash is a report and a child symbolising it blocks on the parent's symbolizer pipe; under TSan the children deadlock; its leak checks read glibc `mallinfo2()`, which the sanitizer allocators bypass |
| `test_alloc_fail` resampler failures | `AF_ASAN` (`src/tests/unit/alloc_fail.h`) | under ASan stb_image_resize2 switches to `STBIR__SEPARATE_ALLOCATIONS`, whose cleanup after a failed allocation dereferences the unallocated `split_info` (vendor bug, ASan builds only); the injection into `img2_scale()` is skipped in ASan builds only |

The other `--wrap` allocation-failure tests (`test_cov_*_alloc`, `test_cov_a_nostrdup`, `test_cov_p3_leak`,
`test_cov_p3_font`) run under both sanitizers: `__real_malloc` resolves to the sanitizer's allocator.

## Findings fixed (S16 sweep)

Every fix has a test that failed or was flagged on the unfixed code.

1. Test harness, `src/tests/ctest.h`: clang's ASan pads the `.ctest` section records with redzones, so
   `ctest_main()`'s fixed-stride walk found only the first record: every test binary ran **0 tests** and passed.
   The records are now `no_sanitize("address")` (clang). Red: `RESULTS: 0 tests` for all 78 binaries.
2. `decoders/decoder_heif.c` / `decoder_avif.c`: the big-endian Exif offset prefix was assembled with
   `exif_data[0] << 24` in `int` (signed overflow for a first byte >= 0x80). Red: `test_cov_c_heif`
   `exif_payload_layouts` (prefix `FF FF FF 00`) flagged by UBSan in both decoders.
3. `decoders/decoder_raw.c`: LibRaw's float ISO was converted with `(uint16_t)` (undefined above 65535); ISO
   102400 (Nikon Z 9 fixture) came out as 36864. Now saturated at 65535 (EXIF ISOSpeedRatings is a SHORT).
   Red: `test_sanitize_regress` `raw_iso_above_65535_saturates` ("expected 65535, got 36864"; UBSan
   float-cast-overflow). Golden: the three `nikon_z_9` info/json cases now print ISO 65535.
4. `decoders/decoder_nanosvg.c`: `roundf(width)` was converted to `uint32_t` before the dimension check
   (undefined for sizes >= 2^32 or infinite); a width of 4294967808 became 512 and decoded. The size is now
   clamped to `IMG2_MAX_DIMENSION + 1` first (IMG2_E_LIMIT). Red: `test_sanitize_regress`
   `svg_size_beyond_uint32_is_limit` ("expected -5, got 0"; UBSan float-cast-overflow).

## Suppressions

Vendor frames only; each entry names the finding, its root cause and a reproducer
(`scripts/sanitizers/*.supp`).

| File | Entry | Finding |
|---|---|---|
| ubsan.supp | `alignment:*/vendor/kvazaar/src/strategies/avx2/*` | kvazaar AVX2 kernels load pixels through `int64_t*` / `uint64_t*` casts of byte pointers |
| ubsan.supp | `function:*/vendor/kvazaar/src/*` | kvazaar strategy pointers (`quant_func`: `coeff_t *`) call implementations declared with `const coeff_t *` |
| ubsan.supp | `pointer-overflow:*/vendor/kvazaar/src/encoderstate.c` | `&frame->sao_luma[i]` / `&frame->sao_chroma[i]` of a NULL array (not dereferenced) |
| ubsan.supp | `shift-base:*/vendor/kvazaar/src/encoder_state-bitstream.c` | picture-hash SEI: `checksum[i][0] << 24` on a promoted `uint8_t` (data-dependent; reproducer `imgcat2 --heif 60` of the `nikon_z_9` NEF) |
| ubsan.supp | `function:*/vendor/openjpeg/src/lib/openjp2/*` | openjpeg calls `opj_j2k_*` / `opj_jp2_*` through generic `void (*)(void *, ...)` codec pointers (reproducer: any J2K HEIF, `heif/j2k_64x48.heif`) |
| ubsan.supp | `nonnull-attribute:*/vendor/libjxl/lib/jxl/enc_fast_lossless.cc` | `memcpy(out, NULL, 0)` for an empty bit writer |
| ubsan.supp | `float-cast-overflow`, `signed-integer-overflow` `:*/vendor/jpeg-quantsmooth/quantsmooth.h` | `a2 / a3` with `a3 == 0` is NaN; `roundf()` of it converted to `int` (x86: INT_MIN), which then overflows `coef1 - range`; reproducer `test_raw_exif_integration` (`nikon_d3.nef` preview) |
| ubsan.supp | `pointer-overflow:*/vendor/libwebp/src/enc/predictor_enc.c` | offsets added to the `WebPSafeCalloc()` result before its NULL check (allocation failure only) |
| lsan.supp | `leak:apng_build` | libpng APNG patch, write side: `png_write_reset()` restarts the row counter, so every later frame's `png_write_start_row()` allocates new row buffers without freeing the old ones; only the APNG fixture writer of `test_animation_timing` writes APNG |
| tsan.supp | `race:log_set_current_POC` | libde265 debug-log global written by every decoder (libheif decodes grid tiles in parallel); upstream libde265 #535 |

In libimg2's own translation units (no runtime suppression possible there), `src/libimg2/core/stbir_impl.h`
compiles the vendored stb_image_resize2 functions with `no_sanitize("alignment", "bounds")`: deliberate
unaligned 8-byte moves (`STBIR_MOVE_2`) and table lookups through a base pointer below the sRGB table start. ASan
still checks those functions.

Other vendor findings (not suppressed, avoided): stb_image_resize2's ASan-only separate-allocation mode crashes
after an allocation failure (`AF_ASAN` above); kvazaar dereferences NULL after most of its own allocation
failures (known, docs/coverage.md "Known limitations"; only `test_cov_c_alloc` injects those).
