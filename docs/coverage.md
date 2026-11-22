# libimg2 unit-test coverage

Goal: every reachable line and branch outcome of libimg2's own sources (`src/libimg2/`) exercised by the unit
and integration tests, measured with clang source-based coverage (`llvm-cov`). Code that cannot be reached
is listed with a reason in [Reviewed exclusions](#reviewed-exclusions); everything else that is uncovered is a
gap and appears in the [Gap inventory](#gap-inventory).

Status: phase 1 (infrastructure, baseline, inventory, first gap-closing round). Not at 100% yet.

## Running

```sh
scripts/coverage.sh                  # configure + build build-cov/, run the tests, write the reports
scripts/coverage.sh --no-build       # re-run the tests and the reports only
scripts/coverage.sh --no-build --no-test   # reports from the existing profiles
```

Environment overrides: `BUILD_DIR` (default `build-cov/`), `OUT_DIR` (default `$BUILD_DIR/coverage`), `JOBS`
(16), `CTEST_JOBS` (8), `CC`/`CXX` (clang/clang++), `LLVM_PROFDATA`/`LLVM_COV` (first of `llvm-profdata`,
`llvm-profdata-17` / `llvm-cov`, `llvm-cov-17`), `TMPDIR` (default `$BUILD_DIR/tmp`).

Outputs in `$OUT_DIR`: `report.txt` (per file: regions, functions, lines, branches), `show.txt` (annotated
sources with branch counts), `html/index.html`, `summary.json` (`llvm-cov export -summary-only`), `merged.profdata`,
`profraw/` (one `%p-%m.profraw` per test process).

### How it works

* CMake option `IMG2_COVERAGE` (default OFF, `cmake/Img2Coverage.cmake`). It requires Clang and fails the
  configure step with a clear message otherwise. When ON, `img2_coverage_instrument(img2)` adds
  `-fprofile-instr-generate -fcoverage-mapping` to img2's own sources only: the vendored codecs and the
  imgcat2 CLI sources are compiled as usual.
* The profile runtime is linked through img2's in-tree link interface: `$<BUILD_INTERFACE:-fprofile-instr-generate>`
  on the `img2` target and `-fprofile-instr-generate` appended to `IMG2_INTERFACE_LINK_OPTIONS` of the imported
  `img2_bundled` (fat archive) target in `src/libimg2/CMakeLists.txt`. Every test executable and the CLI
  therefore link `libclang_rt.profile` without per-test changes; the installed package (`IMG2_CONFIG_LINK_OPTIONS`,
  exported targets) never sees the flag. The fat archive (`img2_fat`, duplicate-symbol check) works unchanged.
* Hardening flags (`-fstack-protector-strong`, `_FORTIFY_SOURCE`) stay on; none conflicts with the instrumentation.
* `scripts/coverage.sh` configures `build-cov/` with clang, `IMG2_COVERAGE=ON` and `IMG2_WERROR=OFF`, builds,
  runs `ctest -LE 'guard|consumer'` with `LLVM_PROFILE_FILE=<out>/profraw/%p-%m.profraw`, merges with
  `llvm-profdata merge -sparse` and reports over every executable in `build-cov/src/tests/` plus `build-cov/imgcat2`.

Deviations from a plain build, coverage configuration only:

| What | Why |
|---|---|
| `IMG2_WERROR=OFF` | the sources are `-Werror` clean with the project's GCC; clang 17 adds warnings (e.g. `-Wunknown-warning-option` for the GCC-only `-Wmaybe-uninitialized` pragma in `decoder_stb.c`, `-Wunused-but-set-variable` in `font_manager.c`). |
| ctest labels `guard` and `consumer` excluded | `guard` runs nm/objdump checks on the archive (the instrumented objects carry `__llvm_prf_*` counters and profile-runtime references, which the mutable-state and no-terminate guards reject by design); `consumer` tests (incl. the only `expensive` test, `img2_consumer_subdir`) rebuild or install img2 in their own trees and add nothing to this build's profile. Every other label, `expensive` included, runs. |

### What is measured

All of `src/libimg2/` except (`-ignore-filename-regex` in `scripts/coverage.sh`):

* `vendor/`, `src/tests/`, `src/imgcat2/`, build trees, the generated `embedded_fonts.c` (data only);
* `src/libimg2/decoders/nanosvg.h` and `nanosvgrast.h`: third-party nanosvg sources copied into the library
  tree (they differ from `vendor/nanosvg`); they are vendored code, not libimg2 logic. Baseline with them
  included: regions 63.99%, lines 55.39%, branches 50.27%.

## Numbers

Totals (Clang 17.0.6, Linux x86-64, all optional codecs built except resvg):

| | Regions | Functions | Lines | Branches |
|---|---|---|---|---|
| Baseline (838c631) | 74.84% (1457 of 5791 missed) | 89.08% (31 of 284) | 63.84% (2820 of 7799) | 59.26% (1734 of 4256) |
| Phase 1 (this branch) | 83.20% (975 of 5803 missed) | 96.84% (9 of 285) | 75.88% (1878 of 7787) | 70.53% (1256 of 4262) |

Per file, baseline -> current (the raw `llvm-cov report` of both runs is kept outside the tree):

| File | Lines base | Lines now | Branches base | Branches now | Regions base | Regions now | Functions base | Functions now |
|---|---|---|---|---|---|---|---|---|
| core/api.c | 91.67% | 100.00% | 75.00% | 100.00% | 94.03% | 100.00% | 100.00% | 100.00% |
| core/image.c | 82.84% | 93.69% | 79.13% | 93.31% | 88.86% | 96.66% | 100.00% | 100.00% |
| core/init.c | 100.00% | 100.00% | 87.50% | 87.50% | 100.00% | 100.00% | 100.00% | 100.00% |
| core/log.c | 94.44% | 100.00% | 54.55% | 100.00% | 80.00% | 100.00% | 100.00% | 100.00% |
| core/status.c | 100.00% | 100.00% | 100.00% | 100.00% | 100.00% | 100.00% | 100.00% | 100.00% |
| decoders/decoder.c | 86.67% | 88.89% | 78.05% | 90.24% | 92.74% | 95.16% | 100.00% | 100.00% |
| decoders/decoder.h | 77.14% | 82.86% | 50.00% | 71.43% | 78.57% | 92.86% | 100.00% | 100.00% |
| decoders/decoder_avif.c (off-limits) | 41.53% | 41.53% | 34.18% | 34.18% | 54.69% | 54.69% | 100.00% | 100.00% |
| decoders/decoder_gif.c | 67.93% | 78.15% | 63.31% | 71.37% | 76.65% | 82.69% | 100.00% | 100.00% |
| decoders/decoder_heif.c (off-limits) | 51.41% | 51.41% | 43.37% | 43.37% | 67.35% | 67.35% | 100.00% | 100.00% |
| decoders/decoder_ico.c | 0.00% | 98.71% | 0.00% | 96.32% | 0.00% | 99.47% | 0.00% | 100.00% |
| decoders/decoder_jpeg.c | 67.15% | 82.35% | 58.33% | 83.33% | 73.53% | 89.71% | 100.00% | 100.00% |
| decoders/decoder_jxl.c | 49.57% | 54.89% | 49.21% | 53.17% | 70.09% | 73.50% | 100.00% | 100.00% |
| decoders/decoder_nanosvg.c | 21.40% | 21.40% | 16.67% | 16.67% | 30.77% | 30.77% | 100.00% | 100.00% |
| decoders/decoder_png.c | 61.73% | 78.91% | 54.32% | 65.83% | 72.79% | 80.88% | 85.71% | 100.00% |
| decoders/decoder_pnm.c | 87.81% | 97.78% | 79.62% | 97.31% | 94.48% | 99.08% | 100.00% | 100.00% |
| decoders/decoder_qoi.c | 0.00% | 100.00% | 0.00% | 92.86% | 0.00% | 100.00% | 0.00% | 100.00% |
| decoders/decoder_raw.c | 54.58% | 54.58% | 50.82% | 50.82% | 65.77% | 65.77% | 100.00% | 100.00% |
| decoders/decoder_stb.c | 70.21% | 91.49% | 66.67% | 83.33% | 84.62% | 96.15% | 100.00% | 100.00% |
| decoders/decoder_tiff.c | 34.16% | 34.16% | 30.39% | 30.39% | 40.00% | 40.00% | 57.14% | 57.14% |
| decoders/decoder_webp.c | 58.04% | 74.90% | 51.69% | 63.56% | 71.95% | 81.71% | 100.00% | 100.00% |
| decoders/decoder_yuv.c | 95.91% | 98.91% | 80.39% | 84.64% | 97.91% | 98.84% | 100.00% | 100.00% |
| decoders/magic.c | 69.23% | 100.00% | 73.28% | 94.66% | 81.75% | 100.00% | 80.00% | 100.00% |
| encoders/encoder.c | 81.67% | 86.67% | 72.22% | 75.00% | 85.25% | 86.89% | 100.00% | 100.00% |
| encoders/encoder_heif.c | 53.24% | 53.24% | 54.00% | 54.00% | 71.64% | 71.64% | 100.00% | 100.00% |
| encoders/encoder_jpeg.c | 64.77% | 87.50% | 73.08% | 84.62% | 77.78% | 91.67% | 50.00% | 100.00% |
| encoders/encoder_jxl.c | 59.85% | 78.03% | 66.67% | 80.95% | 78.46% | 87.69% | 100.00% | 100.00% |
| encoders/encoder_png.c | 59.35% | 87.80% | 61.90% | 80.95% | 69.12% | 88.24% | 40.00% | 80.00% |
| encoders/encoder_webp.c | 69.44% | 88.89% | 72.22% | 83.33% | 82.61% | 91.30% | 100.00% | 100.00% |
| metadata/exif_reader.c | 79.16% | 79.16% | 61.21% | 61.21% | 81.26% | 81.26% | 100.00% | 100.00% |
| metadata/xml_parser.c | 74.62% | 79.59% | 64.52% | 69.29% | 81.96% | 85.97% | 93.55% | 93.55% |
| text/font_manager.c | 79.49% | 87.65% | 67.21% | 78.28% | 84.35% | 91.12% | 97.37% | 100.00% |

## Allocation failures

libimg2 has no allocator wrapper: 28 source files call `malloc`/`calloc`/`realloc`/`strdup` directly (133 call
sites), and the vendored codecs allocate on their own. Options considered:

1. **Link-time interposition in the test executable (chosen).** `src/tests/unit/alloc_fail.h` defines
   `__wrap_malloc/calloc/realloc/strdup`; `test_alloc_fail` is linked with
   `-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=strdup` (GNU ld, registered on Linux only). Every call
   from the test's own objects and from the statically linked img2 archive (libimg2 and the C vendors) goes
   through the wrappers; calls inside shared libraries (libc internals, libstdc++'s `operator new`) do not.
   `af_for_each_failure()` runs an operation once per allocation it makes, failing exactly that allocation.
   Nothing is compiled into the library, no public API change.
2. An `img2_malloc()`-style internal wrapper with a test hook: touches every allocation site (a large diff in
   files other builders are changing) and still misses the vendors' allocations. Not done.
3. A public allocator API (`img2_set_allocator()`): a public API change, out of scope.
4. `LD_PRELOAD` malloc shims: the tests link img2 statically, so this would also need a dynamic libc-level
   hook and loses the per-call-site control. Not needed given option 1.

Not exercised under allocation failure, because the vendored code crashes (vendor bugs, see below): JPEG with
quantsmooth deblocking (the tests use `jpeg_deblock = 0`), TIFF decoding, HEIF encoding. The HEIF/AVIF decoders
are off-limits in phase 1. RAW decoding was not attempted (LibRaw allocates through `operator new`).

## Gap inventory

Current gaps (lines / branch outcomes not covered and not in the exclusion list), grouped by cause.
Line numbers refer to this revision.

### Missing test input

* `decoders/decoder_tiff.c`: the whole multi-page path (`decode_tiff_multipage`, 381-503) needs a TIFF with
  more than one directory (can be written in-test with the vendored libtiff); `tiff_size`/`tiff_seek` SEEK_END
  and clamp (103, 107, 136-138), the libtiff error/warning handlers (158-201, malformed TIFF), `>200` pages
  (406-408), EXIF parse failure (343-344).
* `decoders/decoder_raw.c`: orientation 3/6/other (114-117), lens make (130), description (148), GPS block
  (158-169), XMP failure (192-199), embedded-thumbnail fallback (277-308), unpack/process/make_mem_image
  failures (316-359), and the full `libraw_dcraw_process` path (336-407) is not reached by the current fixtures.
* `decoders/decoder_gif.c`: interlaced frames (311-316), `DISPOSE_BACKGROUND` / `DISPOSE_PREVIOUS` (532-541,
  576-594), frames without a color map (509-511), transparent index outside the palette (79-83), GIF without
  images (446-448), record-walk errors (233-262, 293-357), frame cap (455-465, 555-556).
* `decoders/decoder_png.c`, `decoders/decoder_webp.c`, `decoders/decoder_jxl.c`: animation/metadata variants
  (WebP XMP chunk 147-170, EXIF parse failure 91-108, animation-info failure 335-345, WebP LIMIT 392; PNG/JXL
  multi-frame and colour variants); see `show.txt`.
* `decoders/decoder_nanosvg.c` (21% lines): SVG decoding is barely tested (text elements, font lookup, sizing).
* `metadata/exif_reader.c`, `metadata/xml_parser.c`: malformed EXIF IFDs, XMP variants, malformed XML
  (unterminated tags, mismatched close tags, attribute syntax errors).
* `text/font_manager.c`: `~` expansion with/without `$HOME`, user font directories, TTF name-table parsing and
  UTF-16 conversion, filename weight/style heuristics. Partly host-dependent (the system font directories are
  scanned); a `$HOME/.fonts` fixture built in-test would cover most of it.
* Encoders: quality clamps below 0 / above 100 on the native scale (`encoder_webp.c` 52/55, `encoder_jxl.c`
  52/55, `encoder_heif.c` 114/117), reachable through `img2_encoder_dispatch()`.
* Trivial argument checks still open: `img2_fit_size(..., NULL)` (image.c B140), NULL pixels in
  `img2_image_destroy` (B88) and in the internal scale entry points (B193, B246), `img2_decode_ico(buf, 0, ...)`
  (decoder_ico.c B503), a non-best ICO entry after the best one (B91).

### Error paths needing malformed data or a failing codec

* `decoders/decoder_jpeg.c`: `jpeg_read_header`/`jpeg_start_decompress` failures (119-136), unexpected
  channel count (145), scanline read failure (177), `finish_decompress` warnings (202, 205).
* Encoders: libjpeg/libpng/libjxl/libheif API failures (`encoder_jxl.c` 99-124, `encoder_heif.c` 123-191,
  `encoder_jpeg.c` 161, `encoder_png.c` 44/63) and "encoder produced no output" (`encoder_jpeg.c` 179,
  `encoder_png.c` 243, `encoder_jxl.c` 198, `encoder_heif.c` 228). Most need a codec that fails on valid input;
  candidates for the allocation-failure harness once the vendor crashes are avoided.

### Allocation failure

Covered by `test_alloc_fail` for core, PNM/QOI/BMP/ICO/YUV, PNG/JPEG/WebP/JXL/GIF decoding, PNG/JPEG/WebP/JXL
encoding, XML/EXIF/XMP and the font cache. Still open: TIFF and HEIF-encoder allocation paths (vendor crashes,
see above), RAW, the HEIF/AVIF decoders (off-limits), `realloc` growth paths of the HEIF/JXL encoders' output
buffers (`encoder_heif.c` 62-69, `encoder_jxl.c` 170).

### Off-limits in phase 1

`decoders/decoder_heif.c` (172 lines, 111 branch outcomes missed) and `decoders/decoder_avif.c` (207 lines,
129 branch outcomes missed) are being changed on `feature/split`; measured only.

## Reviewed exclusions

Code that no input can reach. Each entry: location, why. These stay in the report (llvm-cov has no exclusion
markers); they are the expected residue of a "100%" run.

| Location | Reason |
|---|---|
| core/image.c:38-40 | `byte_count / 4 != pixel_count`: pixel_count <= IMG2_MAX_PIXELS, cannot overflow a 64-bit size_t (32-bit only). |
| core/image.c:162-165, 171-174 | `new_height > max_h` / `new_width > max_w` after `fit_round_clamp(v, max)`, which already clamps to max. |
| core/image.c:178-181 | `new_width == 0 \|\| new_height == 0`: `fit_round_clamp()` returns at least 1. |
| core/image.c:225-227 | invalid filter in `scale_impl()`: `img2_scale*()` validate the filter first, the internal entry points pass IMG2_FILTER_DEFAULT. |
| core/image.c:296-298 | `stride > SIZE_MAX / height`: stride is uint32_t and height <= 65536 (64-bit size_t). |
| core/image.c:464-467, 504-507, 546-549, 586-589, 628-631 | `transform_*()` with NULL source: `img2_image_orient()` rejects NULL pixels first. |
| core/image.c:739 | orientation `default:` after the 1..8 range check. |
| core/init.c:52 (B52 false outcomes) | `img2_font_manager_init_lazy()` returning false needs another thread to build the cache between `font_lock_initialized()`'s check and the call (race only); `s_font_implicit_ref` set while `s_refs == 0` is impossible (the implicit reference is counted in `s_refs`). |
| decoders/decoder.c:241-268 | dispatcher output validation (count, NULL frame, zero/oversized dimensions, NULL pixels): every registered decoder returns an error or frames from `img2_image_alloc()`. Defensive. |
| decoders/decoder.h:83 (B83 true) | `img2_decode_fail(NULL, ...)`: every caller passes a status pointer. |
| decoders/decoder_ico.c:439-442 | bit depth `default:`: `parse_bmp_header()` admits only 1/4/8/24/32. |
| decoders/decoder_ico.c:477 (B477 `fr.count < 1`) | `img2_decode_png()` never returns IMG2_OK without a frame. |
| decoders/decoder_ico.c:503, decoder_qoi.c:45, decoder_stb.c:45, decoder_pnm.c:479 (`frame_count == NULL`) | the static helpers are only called with a local counter. |
| decoders/decoder_stb.c:62-66 | stb_image never returns pixels with width or height <= 0. |
| decoders/decoder_pnm.c:129-131, 523-526, 536-539 | size_t overflow of width*height (<= 2^32), *channels (<= 4), *bps (<= 2): impossible with 64-bit size_t (32-bit only). |
| decoders/decoder_yuv.c:106 (B106 `a == 0`) | strides are never 0 (minimal stride >= 1 is substituted). |
| decoders/decoder_yuv.c:128 (B128:27) | the name table is lower case; only the user string can be upper case. |
| decoders/decoder_yuv.c:174 (B174:19), 523, 526 (params/frame_count NULL) | internal callers always pass them. |
| decoders/decoder_yuv.c:276 (B276:8/56 false) | `stride * (h - 1)` cannot overflow when `stride * h` did not. |
| decoders/decoder_yuv.c:318 (B318 `s == NULL`) | `parse_field()` is only called with split fields. |
| decoders/decoder_yuv.c:573-577 | format `default:` after `yuv_compute_layout()` accepted the format. |
| decoders/magic.c:194, 199, 218, 226, 232, 267, 286, 304, 309, 329, 351, 359 (false outcomes of `len >= N`, N <= 8) | `img2_detect_mime()` returns early for `len < 8`. |
| decoders/magic.c:154 (B154 `len < 12`) | `is_heif_j2k_ftyp()` is only called inside `if (len >= 12)`. |
| encoders/encoder.c:84 (missing-encoder names JPEG..JXL) | configuration-specific: all encoders are built in the measured configuration. |
| encoders/encoder.c:127-135 | encoder returned OK without output: every encoder checks this itself. Defensive. |

Size: 25 entries.

## Bugs found

Fixed (contained, covered by the new tests):

1. `decoders/decoder_ico.c`: `image_offset + bytes_in_res` was computed in 32 bits; an entry size near 4 GiB
   wrapped around the bounds check (heap over-read). Now size_t.
2. `decoders/decoder_ico.c`: `palette_size * 4` was computed in 32 bits; a huge `clr_used` wrapped to a small
   palette size and indexed far past the buffer (heap over-read). Now size_t (`ico.huge_palette_count_is_rejected`).
3. `decoders/decoder_jpeg.c`: when the EXIF struct allocation failed but the XMP one succeeded, the
   uninitialised XMP struct was passed to `img2_xmp_clear()` (free of garbage pointers).
4. `text/font_manager.c`: a failed `strdup("[embedded]")` left an embedded entry without its marker path, so the
   cleanup freed the static font data; a failed family `strdup` left NULL names for `strcasecmp()`; a failed
   path `strdup` for a scanned font led to `fopen(NULL)`. Entries are now skipped on allocation failure
   (`add_embedded_font()` replaces four copies of the block).
5. `metadata/xml_parser.c`: a failed arena allocation of an element name, attribute array or attribute name
   was stored as NULL and dereferenced later (`strcmp`, array write). Now a parse error.

Not fixed (reported):

* `decoders/decoder_ico.c` `find_best_ico_entry()`: the score `(area << 16) | bit_count` overflows uint32 for
  256x256 entries (65536 << 16 == 2^32), so a 256x256 image is never preferred over smaller ones. Behaviour
  change for real .ico files; left for review.
* `metadata/xml_parser.c`: `parse_element()` recurses once per nesting level without a depth limit (stack
  exhaustion on deeply nested XMP from a file).
* `text/font_manager.c` `convert_utf16be_to_utf8()`: reads `utf16be[i + 1]` past the name-table string when
  its byte length is odd.
* Vendored code that crashes after an allocation failure (not ours to patch): jpeg-quantsmooth
  (`quantsmooth.h` `upsample_row`, unchecked allocation), libtiff (`TIFFRGBAImageBegin` reads a NULL
  ExtraSamples array after `TIFFFetchNormalTag` ran out of memory), libheif's kvazaar plugin
  (`kvz_config_init(NULL)` after a failed `kvz_config_alloc()`).

## Tests added in phase 1

| Test | Covers |
|---|---|
| `test_core_edges` | image/scale/validate/orient argument and limit paths, `img2_encode` / `img2_apply_orientation` / dispatcher edges, registries, log sanitising and `vsnprintf` failure, MIME detection of every signature, stb dimension limit. |
| `test_decoder_edges` | ICO/CUR (every DIB depth, AND mask, palette bounds, malformed directories and headers, PNG payloads), QOI, PNM/PAM header variants and errors, YUV spec parsing and layout overflows. |
| `test_alloc_fail` | allocation-failure injection (see above), Linux only. |
