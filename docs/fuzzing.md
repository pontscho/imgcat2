# Fuzzing libimg2

libimg2 parses untrusted bytes (every image decoder, the EXIF/XMP readers, raw YUV geometry). It is fuzzed with
[libFuzzer](https://llvm.org/docs/LibFuzzer.html) under AddressSanitizer + UndefinedBehaviorSanitizer (with
LeakSanitizer).

## Build configuration (`IMG2_FUZZ`)

`-DIMG2_FUZZ=ON` (default OFF, Clang only; `cmake/Img2Fuzz.cmake`) builds the targets in `src/tests/fuzz/`:

- libimg2's own sources: `-fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=undefined`
  (any UB in our code aborts like an ASan report);
- the vendored codecs: `-fsanitize=fuzzer-no-link,address,undefined` with UBSan in recover mode, passed through
  the existing hardening plumbing (vendor-section `COMPILE_OPTIONS`, and `CMAKE_C_FLAGS`/`CMAKE_CXX_FLAGS` of the
  ExternalProject builds of libjpeg-turbo and aom); `vendor/` is not modified. Hand-written assembly (libjpeg-turbo
  and aom SIMD) is not instrumented;
- the fuzz targets link `-fsanitize=fuzzer,address,undefined` with the C++ driver.

Fuzz-config-only deviations from the normal build: `-D_FORTIFY_SOURCE` and `-fstack-protector-strong` are
replaced by the sanitizer flags, and the `-static` / `-static-pie` executable link flags are dropped (the sanitizer
runtimes need a dynamic link). `IMG2_FUZZ` and `IMG2_COVERAGE` are mutually exclusive. Use a separate build tree
(`build-fuzz/`).

## Targets

| Target | Entry points | Input layout |
|---|---|---|
| `fuzz_decode` | `img2_detect_mime`, `img2_is_animated`, `img2_decode` (auto-detection, `max_frames` 3), then `img2_apply_orientation` + `img2_scale_fit` on frame 0 | the file |
| `fuzz_decode_forced` | `img2_decode` with `force_mime` (every decoder also sees data without its magic) | byte 0: type, byte 1: `max_frames` (bits 0-1) and quantsmooth on (bit 2), rest: the file |
| `fuzz_exif` | `img2_exif_parse_tiff`, `img2_exif_parse_jpeg` | byte 0 bit 0: TIFF / JPEG, rest: data |
| `fuzz_xmp_xml` | `img2_xmp_parse_xml`, `img2_xmp_parse_jpeg`, the internal XML DOM parser + accessors | byte 0 % 3: XMP packet / XMP in JPEG / DOM walk, rest: data |
| `fuzz_yuv` | `img2_yuv_parse_spec`, `img2_yuv_format_from_name`, `img2_yuv_frame_size`, `img2_decode` (`IMG2_MIME_YUV`) | spec mode (byte 0 bit 7: `format:w:h[:ys][:uvs]\n` + frame) or binary geometry (u16, or u32/u64 with bit 0) + frame |

ICO/CUR directories, PNM/PAM headers and the magic detection are reached through `fuzz_decode` /
`fuzz_decode_forced`. Every target checks result invariants (zeroed `*out` on error, frame geometry, string
fields NUL-terminated) and frees everything (leak detection is on). libimg2's log runs at `IMG2_LOG_DEBUG` into a
silent callback, so the message formatting sees fuzzed strings; `IMG2_FUZZ_LOG=1` prints it.

Dictionaries: `src/tests/fuzz/dict/` (PNG chunks, JPEG markers, GIF blocks, RIFF/WebP chunks, ISOBMFF boxes,
TIFF/RAW, JXL, PNM/PAM, SVG; TIFF tags; XMP names; YUV format names).

Seeds: `fuzz_seedgen` writes small synthetic inputs (libimg2's encoders, stb_image_write, qoi.h, hand-built
PNM/ICO/CUR/GIF/SVG, JPEGs with EXIF in all eight orientations and with XMP, TIFF/EXIF blobs, XMP packets, YUV
frames) plus every file of `src/tests/data` and `src/tests/fixtures` below 300 KB, with the per-target prefixes.

## Running

```sh
scripts/fuzz.sh                                    # build, seed, fuzz every target 600 s with 8 processes
FUZZ_TIME=1800 FUZZ_JOBS=8 scripts/fuzz.sh fuzz_decode
scripts/fuzz.sh --no-build --no-run --replay       # replay the corpora, collect vendor UBSan reports
```

Defaults: `-rss_limit_mb=2048`, `-timeout=10`, `-max_len` 128 KiB (decode), 64 KiB (EXIF, XMP), 16 KiB (YUV);
fork mode (`-fork=N -ignore_crashes=1 -ignore_timeouts=1 -ignore_ooms=1`) so a run collects every finding, or
`FUZZ_MODE=jobs` for `-jobs/-workers`. Environment knobs and paths are documented in the script header.

Resource limits: the library's own limits (`IMG2_MAX_DIMENSION`, `IMG2_MAX_PIXELS` = 200 MP per frame,
`IMG2_MAX_TOTAL_PIXELS` = 2^30 pixels per animation, 200 frames) admit images of up to 800 MB per frame, which is
legitimate but turns into OOM/timeout reports under ASan. They are compile-time constants (there is no runtime
limit knob), so the harness tightens them from the outside: ASan's `max_allocation_size_mb=256` with
`allocator_may_return_null=1` makes any single allocation above 256 MB fail, which libimg2 must handle as
`IMG2_E_NOMEM` (C++ `operator new` still reports such an allocation as an OOM: see the notes of a run).

Findings land in `<work>/artifacts/<target>/` (`crash-*`, `leak-*`, `timeout-*`, `oom-*`). Reproduce with
`build-fuzz/src/tests/fuzz/<target> <file>`, minimise with `-minimize_crash=1 -runs=10000 <file>`.
Every fixed finding becomes a regression test in `src/tests/unit/test_fuzz_regress.c` (the input embedded as a
C array), registered between the `fuzz regressions` markers at the end of `src/tests/CMakeLists.txt`.
