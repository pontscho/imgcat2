# libimg2 unit-test coverage

Goal: every reachable line, region, function and branch outcome of libimg2's own sources (`src/libimg2/`)
exercised by the unit and integration tests, measured with clang source-based coverage (`llvm-cov`). Code that
cannot be reached is listed, item by item and with a reason, in the machine-checked manifest
[`docs/coverage-exclusions.txt`](coverage-exclusions.txt); `scripts/coverage.sh` fails when anything uncovered is
not in it, when an entry is covered (stale), or when an entry's source anchor no longer matches.

Status (phase 3): **100% after exclusions on all four metrics**, enforced by `scripts/coverage_check.py`.

## Running

```sh
scripts/coverage.sh                  # configure + build build-cov/, run the tests, write the reports, run the check
scripts/coverage.sh --no-build       # re-run the tests, the reports and the check
scripts/coverage.sh --no-build --no-test   # reports + check from the existing profiles
scripts/coverage.sh --no-check       # everything except the exclusion check
```

Exit status: non-zero when the build fails, a test fails (the reports are still written) or the exclusion check
fails.

Environment overrides: `BUILD_DIR` (default `build-cov/`), `OUT_DIR` (default `$BUILD_DIR/coverage`), `JOBS`
(16), `CTEST_JOBS` (8), `CC`/`CXX` (clang/clang++), `LLVM_PROFDATA`/`LLVM_COV` (first of `llvm-profdata`,
`llvm-profdata-17` / `llvm-cov`, `llvm-cov-17`), `TMPDIR` (default `$BUILD_DIR/tmp`).

Outputs in `$OUT_DIR`: `report.txt` (per file: regions, functions, lines, branches), `show.txt` (annotated
sources with branch counts), `html/index.html`, `summary.json` (`llvm-cov export -summary-only`), `export.json`
(full `llvm-cov export`: functions with their regions and branches), `lcov.info` (`llvm-cov export
-format=lcov`, per-line counts), `merged.profdata`, `profraw/` (one `%p-%m.profraw` per test process).

The check alone (stdlib-only Python):

```sh
python3 -I scripts/coverage_check.py --root . --json build-cov/coverage/export.json \
    --lcov build-cov/coverage/lcov.info --manifest docs/coverage-exclusions.txt [--per-file]
python3 -I scripts/coverage_check.py ... --emit-uncovered   # every uncovered item in manifest syntax
```

### How it works

* CMake option `IMG2_COVERAGE` (default OFF, `cmake/Img2Coverage.cmake`). It requires Clang and fails the
  configure step with a clear message otherwise. When ON, `img2_coverage_instrument(img2)` adds
  `-fprofile-instr-generate -fcoverage-mapping -fprofile-update=atomic` to img2's own sources only: the vendored
  codecs and the imgcat2 CLI sources are compiled as usual. `-fprofile-update=atomic` (phase 3): `test_threads`
  decodes on several threads at once; with plain counter increments updates were lost and outcomes that clang
  derives by subtraction flipped between runs (seen on `decoder_png.c` `png_read_func`), which would make the
  exclusion check flaky.
* The profile runtime is linked through img2's in-tree link interface: `$<BUILD_INTERFACE:-fprofile-instr-generate>`
  on the `img2` target and `-fprofile-instr-generate` appended to `IMG2_INTERFACE_LINK_OPTIONS` of the imported
  `img2_bundled` (fat archive) target in `src/libimg2/CMakeLists.txt`. Every test executable and the CLI
  therefore link `libclang_rt.profile` without per-test changes; the installed package (`IMG2_CONFIG_LINK_OPTIONS`,
  exported targets) never sees the flag. The fat archive (`img2_fat`, duplicate-symbol check) works unchanged.
* Hardening flags (`-fstack-protector-strong`, `_FORTIFY_SOURCE`) stay on; none conflicts with the instrumentation.
* `scripts/coverage.sh` configures `build-cov/` with clang, `IMG2_COVERAGE=ON` and `IMG2_WERROR=OFF`, builds,
  runs `ctest -LE 'guard|consumer'` with `LLVM_PROFILE_FILE=<out>/profraw/%p-%m.profraw`, merges with
  `llvm-profdata merge -sparse`, reports over every executable in `build-cov/src/tests/` plus `build-cov/imgcat2`,
  and runs the exclusion check.

Deviations from a plain build, coverage configuration only:

| What | Why |
|---|---|
| `IMG2_WERROR=OFF` | the sources are `-Werror` clean with the project's GCC; clang 17 adds warnings (e.g. `-Wunknown-warning-option` for the GCC-only `-Wmaybe-uninitialized` pragma in `decoder_stb.c`, `-Wunused-but-set-variable` in `font_manager.c`). |
| ctest labels `guard` and `consumer` excluded | `guard` runs nm/objdump checks on the archive (the instrumented objects carry `__llvm_prf_*` counters and profile-runtime references, which the mutable-state and no-terminate guards reject by design); `consumer` tests (incl. the only `expensive` test, `img2_consumer_subdir`) rebuild or install img2 in their own trees and add nothing to this build's profile. Every other label, `expensive` included, runs. |

### What is measured

All of `src/libimg2/` except (`-ignore-filename-regex` in `scripts/coverage.sh`, `--ignore` in the checker):

* `vendor/`, `src/tests/`, `src/imgcat2/`, build trees, the generated `embedded_fonts.c` (data only);
* `src/libimg2/decoders/nanosvg.h` and `nanosvgrast.h`: third-party nanosvg sources copied into the library
  tree (they differ from `vendor/nanosvg`); they are vendored code, not libimg2 logic.

`decoders/decoder_resvg.c` is not built in the measured configuration (all optional codecs except resvg).

## Numbers

Clang 17.0.6, Linux x86-64. Raw = `llvm-cov report` (`report.txt`); "checker" = the union semantics below.

| | Regions | Functions | Lines | Branches |
|---|---|---|---|---|
| Baseline (838c631), raw | 74.84% (1457 of 5791 missed) | 89.08% (31 of 284) | 63.84% (2820 of 7799) | 59.26% (1734 of 4256) |
| Phase 1, raw | 83.20% (975 of 5803 missed) | 96.84% (9 of 285) | 75.88% (1878 of 7787) | 70.53% (1256 of 4262) |
| Phase 2 (lanes A/B/C merged, e990050), raw | 96.51% (208 of 5967) | 99.33% (2 of 298) | 92.81% (579 of 8052) | 91.31% (382 of 4398) |
| **Phase 3, raw** | **96.56% (207 of 6012)** | **99.34% (2 of 301)** | **92.90% (577 of 8122)** | **91.50% (376 of 4422)** |
| Phase 3, checker (union) | 96.56% (207 of 6012) | 99.34% (2 of 301) | 93.00% (574 of 8200) | 91.50% (376 of 4422) |
| **Phase 3, after exclusions** | **100%** (207 excluded) | **100%** (2) | **100%** (574) | **100%** (376) |

Raw and checker numbers differ only for lines: `llvm-cov report` sums per-function line statistics and, for a
`static inline` helper compiled into several translation units (`decoders/decoder.h`), counts the best single
instance (decoder.h: 3 of 35 lines missed); the checker uses llvm-cov's per-file line statistics (lcov export),
in which a line is covered when any instance ran it (decoder.h: 0 missed; lines 117-119 run in the HEIF/AVIF
instances only). Regions, functions and branches are identical in both.

Per file, phase 3 raw (`report.txt`); every miss is a manifest entry:

| File | Lines | Missed | Branches | Missed | Regions missed | Functions missed |
|---|---|---|---|---|---|---|
| core/api.c | 100.00% | 0 | 100.00% | 0 | 0 | 0 |
| core/image.c | 93.69% | 32 | 94.88% | 13 | 12 | 0 |
| core/init.c | 100.00% | 0 | 93.75% | 1 | 0 | 0 |
| core/log.c | 100.00% | 0 | 100.00% | 0 | 0 | 0 |
| core/status.c | 100.00% | 0 | 100.00% | 0 | 0 | 0 |
| decoders/decoder.c | 88.89% | 15 | 90.24% | 8 | 6 | 0 |
| decoders/decoder.h | 91.43% | 3 (0 union) | 85.71% | 2 | 0 | 0 |
| decoders/decoder_avif.c | 86.01% | 48 | 87.63% | 24 | 12 | 0 |
| decoders/decoder_gif.c | 94.51% | 23 | 92.21% | 19 | 16 | 0 |
| decoders/decoder_heif.c | 86.99% | 74 | 88.68% | 36 | 20 | 0 |
| decoders/decoder_ico.c | 98.71% | 4 | 97.79% | 3 | 1 | 0 |
| decoders/decoder_jpeg.c | 84.51% | 22 | 85.42% | 7 | 6 | 0 |
| decoders/decoder_jxl.c | 71.81% | 139 | 72.80% | 68 | 44 | 0 |
| decoders/decoder_nanosvg.c | 100.00% | 0 | 98.11% | 2 | 0 | 0 |
| decoders/decoder_png.c | 93.54% | 39 | 83.93% | 45 | 19 | 0 |
| decoders/decoder_pnm.c | 97.78% | 8 | 97.31% | 7 | 3 | 0 |
| decoders/decoder_qoi.c | 100.00% | 0 | 92.86% | 1 | 0 | 0 |
| decoders/decoder_raw.c | 97.03% | 8 | 91.79% | 11 | 2 | 0 |
| decoders/decoder_stb.c | 91.49% | 4 | 83.33% | 3 | 1 | 0 |
| decoders/decoder_tiff.c | 90.44% | 28 | 83.02% | 18 | 11 | 1 (tiff_write_proc) |
| decoders/decoder_webp.c | 90.59% | 24 | 82.20% | 21 | 8 | 0 |
| decoders/decoder_yuv.c | 98.91% | 4 | 94.77% | 16 | 5 | 0 |
| decoders/magic.c | 100.00% | 0 | 95.04% | 13 | 0 | 0 |
| encoders/encoder.c | 86.67% | 8 | 75.00% | 9 | 8 | 0 |
| encoders/encoder_heif.c | 75.54% | 34 | 72.00% | 14 | 10 | 0 |
| encoders/encoder_jpeg.c | 93.28% | 8 | 92.86% | 2 | 2 | 0 |
| encoders/encoder_jxl.c | 87.88% | 16 | 90.48% | 4 | 4 | 0 |
| encoders/encoder_png.c | 89.52% | 13 | 85.00% | 6 | 7 | 1 (png_memory_flush_func) |
| encoders/encoder_webp.c | 100.00% | 0 | 94.44% | 1 | 0 | 0 |
| metadata/exif_reader.c | 99.48% | 3 | 99.54% | 2 | 1 | 0 |
| metadata/xml_parser.c | 97.50% | 13 | 95.12% | 12 | 6 | 0 |
| text/font_manager.c | 98.85% | 7 | 97.08% | 8 | 3 | 0 |

The low raw numbers of the HEIF/AVIF/JXL decoders and the HEIF encoder are API checks of libheif/libjxl calls that
cannot fail with the arguments passed (allocation through `new`, events that are never emitted, the same checks
repeated on data an earlier pass already accepted): see their manifest reasons.

## Exclusion check

`scripts/coverage_check.py` (stdlib only, `python3 -I`) reads `export.json` (regions, branches, functions of every
instrumented function) and `lcov.info` (per-line counts) for `src/libimg2` and compares every uncovered item with
the manifest. It prints the raw / excluded / after-exclusion numbers and fails (exit 1) on:

* **UNEXPLAINED**: an uncovered line, branch outcome, code region or function without a manifest entry;
* **STALE**: a manifest entry whose item is covered now (or no longer exists);
* **ANCHOR**: an entry whose anchor text differs from the current source line (code moved: re-verify the entry);
* **MANIFEST**: syntax errors, unknown kinds, duplicates, entries without a reason, reasons without entries.

Each failure is listed precisely (file:line, kind, detail, manifest line). Items:

| Kind | Item | Detail |
|---|---|---|
| `line` | a line with an lcov count of 0 (llvm-cov's per-file line statistics) | none |
| `region` | a code region with count 0 | start column |
| `branch-true` / `branch-false` | a branch condition whose true / false outcome was never taken | start column |
| `function` | a function that never ran | function name |

Rules:

* **Instances are merged** (union): a `static inline` helper compiled into several translation units, and the
  same function in several test executables, is one item; it is covered when any instance covered it.
* **Macro expansions** are located at the expansion site in the function's own file: line = site line, detail =
  `<site column>><line>:<column>` inside the macro (prefixed by the macro's file when it is another file), so two
  expansions of one macro are distinct items.
* **Folded branches** (clang folded the condition to a constant: both outcome counters are the constant zero
  although the condition's code ran) are not items, as in `llvm-cov report`. With this rule the checker's region,
  function and branch totals equal `llvm-cov report` exactly.
* Several regions with the same span inside one function (a macro argument expanded twice) are told apart by
  their occurrence index.

### Manifest format

```
reason: <one-line reason>                      # applies to the entries up to the next "reason:" line
<file>:<line> <kind>[ <detail>] = <anchor>      # file below src/libimg2/, anchor = source line, trimmed
```

Example:

```
reason: heif_context_alloc() uses new (throws, never returns NULL)
decoders/decoder_heif.c:97 region 19 = if (ctx == NULL) {
decoders/decoder_heif.c:97 branch-true 6 = if (ctx == NULL) {
decoders/decoder_heif.c:98 line = return IMG2_HEIF_LAYOUT_SINGLE;
```

Workflow after a code change: run `scripts/coverage.sh`; for an ANCHOR or STALE failure re-check the item and move
or delete its entry; for UNEXPLAINED items write a test, or (only for genuinely unreachable code) add entries,
e.g. from `--emit-uncovered`, under a reason. Exclusion policy: only code that no input can reach (defensive checks
the code structure makes impossible, 64-bit-only overflow guards, configuration-specific paths, codec API calls
that cannot fail with the arguments given) may be excluded; "hard to trigger" is not unreachable. Entries whose
reason starts with `RESOURCE-BOUND` are reachable in principle but need more memory than a unit test can use.

Manifest size (phase 3): 1159 entries under 220 reasons: 574 lines, 207 regions, 287 true and 89 false branch
outcomes, 2 functions. Largest files: decoder_jxl.c 251, decoder_heif.c 130, decoder_png.c 103,
decoder_avif.c 84, decoder_gif.c 58, decoder_tiff.c 58, encoder_heif.c 58, core/image.c 57.

## Allocation failures

libimg2 has no allocator wrapper: the library calls `malloc`/`calloc`/`realloc`/`strdup` directly, and the
vendored codecs allocate on their own. Faults are injected by link-time interposition in the test executable
(GNU ld `--wrap`, Linux only); nothing is compiled into the library and there is no public API change.

* `src/tests/unit/alloc_fail.h` defines `__wrap_malloc/calloc/realloc/strdup`; the test is linked with
  `-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=strdup`. Every call from the test's own objects and from
  the statically linked img2 archive (libimg2 and the C vendors) goes through the wrappers; calls inside shared
  libraries (libc internals, libstdc++'s `operator new`) do not. `af_for_each_failure()` runs an operation once
  per allocation it makes, failing exactly that allocation; `af_count_allocs()` + `af_fail_range()` fail a range.
  Users: `test_alloc_fail` (phase 1), `test_cov_a_alloc`, `test_cov_b_alloc`, `test_cov_c_alloc`.
* Variants with their own wrappers: `test_cov_a_nostrdup` (`--wrap=strdup`, every strdup fails: empty font
  cache), `test_cov_p3_leak` (`--wrap=...,--wrap=free`: a live-block counter, so a leaked block is a non-zero
  difference without heap introspection), `test_cov_p3_font` (`--wrap=malloc,free`: font buffers of armed sizes
  end at a `PROT_NONE` guard page, so any read past the file data faults deterministically without a sanitizer).
* **Profiler warm-up caveat** (lane A): in the coverage build the profile runtime allocates value-profiling nodes
  through the wrapped `calloc` on the first call of an indirect call site. That shifts allocation indices between
  the first and later runs of an operation, so `af_count_allocs()` + `af_fail_range()` users run the operation
  once before counting (`test_cov_a_alloc` font operations). `af_for_each_failure()` is unaffected (it stops when
  a run completes without the injected failure).
* Leak checks of the lane tests use glibc `mallinfo2()` heap totals around repeated runs after a warm-up that
  saturates tcache (`cb_heap_in_use()`, `test_cov_c_alloc`); they pass vacuously on other C libraries.
* `test_cov_c_alloc` probes the kvazaar (HEIF encoder) allocation failures in forked children (most of them crash
  inside kvazaar) and requires at least one clean encoder error; the children `exit()` so their profile is written.
* `mallopt(M_PERTURB)` makes fresh memory non-zero, which turned the uninitialised-XMP free in the HEIF/AVIF stills
  into a deterministic crash (lane C red test).

### setjmp counting caveat

clang derives the counts of regions after `if (setjmp(...))` by subtraction (else = entry - then). Every `longjmp`
back adds to "then" without a new entry, so every later derived count in that function is too low by the number
of longjmps over the whole run and shows 0 once the error runs outnumber the success runs, although the code ran.
`decoder_png.c` (APNG, still, metadata readers) and `decoder_jpeg.c` are affected; `test_cov_b_jpeg` /
`test_cov_b_png` decode their success fixtures a few hundred times (2x2 / 8x8 images) so the derived counts stay
positive. Adding many libpng/libjpeg error or allocation-failure runs can zero them again: the checker then
reports success-path lines as UNEXPLAINED; the fix is more successful runs (or a restructured test), not an
exclusion. Phase 3 verified that no success-path line reads 0.

## Bugs found and fixed

Every fix has a test that failed (or crashed / was flagged by ASAN) on the unfixed code first.

Phase 1:

1. `decoders/decoder_ico.c`: `image_offset + bytes_in_res` computed in 32 bits wrapped around the bounds check
   (heap over-read).
2. `decoders/decoder_ico.c`: `palette_size * 4` in 32 bits wrapped for a huge `clr_used` (heap over-read).
3. `decoders/decoder_jpeg.c`: uninitialised XMP struct passed to `img2_xmp_clear()` when only the EXIF allocation
   failed.
4. `text/font_manager.c`: failed `strdup()`s left entries that freed static data, compared NULL names or opened
   NULL paths; entries are now skipped on allocation failure.
5. `metadata/xml_parser.c`: failed arena allocations stored as NULL and dereferenced later.

Phase 2, lane A (core, ICO/PNM/YUV, XML/EXIF, fonts):

6. `decoder_ico.c` `find_best_ico_entry()`: `(area << 16) | bit_count` wrapped for 256x256 entries, which never won.
7. `xml_parser.c`: unbounded recursion on deeply nested XML (stack overflow); `IMG2_XML_MAX_DEPTH` = 256.
8. `font_manager.c` `convert_utf16be_to_utf8()`: read one byte past an odd-length name.
9. `font_manager.c`: name-record offset/length used without a bounds check (read up to 64 KiB past the file).
10. `xml_parser.c`: `>` and other closing markers in character data failed the whole XMP packet.
11. `exif_reader.c`: ExifIFD/GPS pointers followed recursively from every IFD (stack overflow on cycles) and 32-bit
    offset arithmetic that wrapped near 4 GiB.

Phase 2, lane B (GIF, PNG, WebP, JPEG, TIFF, RAW, SVG):

12. `decoder_gif.c`: an extension without data sub-blocks swallowed the next record (valid GIFs failed).
13. `decoder_gif.c`: `GifErrorString(0)` (NULL) printed with `%s`.
14. `decoder_png.c`: libpng errors inside the APNG frame loop leaked every frame and buffer.
15. `decoder_png.c`: XMP in iTXt chunks was never read.
16. `decoder_png.c`: the hidden APNG default image was returned as frame 0.
17. `decoder_jpeg.c`: libjpeg errors after the image allocation leaked the image and row buffer.
18. `decoder_tiff.c`: heap overflow when `width * height` wrapped 32 bits.
19. `decoder_nanosvg.c`: 1-byte heap overflow in the CSS class inliner.
20. `decoder_nanosvg.c`: `</style>` before `<style>` made the CSS length negative.
21. `decoder_raw.c`: grey / short bitmap previews over-read (3 bytes per pixel assumed).
22. `decoder_raw.c`: monochrome full-processing output over-read.
23. `decoder_raw.c`: double rotation in the full processing path.
24. `decoder_raw.c`: the JPEG preview's metadata leaked.

Phase 2, lane C (HEIF/AVIF/JXL, encoders):

25. `encoder_png.c`: output buffer and row pointers leaked on every libpng error after output started (indeterminate
    automatic after longjmp).
26. `encoder_jpeg.c`: the `jpeg_mem_dest()` buffer and the row buffer leaked on every error (own destination
    manager now).
27. `decoder_jxl.c`: an unusable Exif box ended the metadata extraction before the XMP box was read.
28. `decoder_heif.c` / `decoder_avif.c`: uninitialised XMP struct freed when only the EXIF allocation failed.

Phase 3:

29. `metadata/exif_reader.c` `img2_xmp_parse_xml()`: a later `dc:subject` (same packet, or a second XMP item of a
    HEIF/AVIF still parsed into the same struct) overwrote `xmp->keywords` without freeing it (leak).
    Red: `test_cov_p3_leak` `xmp_keywords:*` -> "expected 0, got 1" live blocks.
30. `decoders/decoder_jxl.c` `jxl_find_box()`: scanned every byte for "Exif"/"xml ", so matching bytes inside the
    codestream, another box or after a bare codestream became bogus EXIF/XMP. Now a box walker (32-bit size,
    64-bit largesize, size 0 = to the end, stops at the first malformed header and after 4096 boxes; bare
    codestreams have no boxes; `brob` boxes are not decompressed). Red: `test_cov_p3_jxl`
    `bare_codestream_has_no_boxes`, `box_lookalikes_inside_another_box_are_ignored` -> "expected 0, got 1".
    Golden unchanged.
31. `text/font_manager.c`: stb_truetype does no range checking; a truncated / malformed font in a scanned font
    directory made `stbtt_InitFont()` read past the file buffer. `sfnt_is_valid()` now checks the sfnt version,
    the table directory and every table (64-bit offset + length) and the cmap/head/maxp minimum lengths before
    both `stbtt_InitFont()` calls. Red: `test_cov_p3_font` (guard-page buffers): 6 fixtures SIGSEGV in
    `stbtt__find_table` / `stbtt_InitFont_internal`, 3 invalid fonts (table past the end, 32-bit wrapping offset,
    WOFF / collection signature) were accepted.

## Known limitations

* Vendored code that crashes after an allocation failure (not ours to patch; these paths are not
  failure-injected): nanosvg `nsvg__addPoint` (realloc without a NULL check), LibRaw's memory manager (memsets a
  failed malloc in its constructor), jpeg-quantsmooth `upsample_row` (unchecked allocation), libtiff
  `TIFFRGBAImageBegin` (NULL ExtraSamples array after `TIFFFetchNormalTag` ran out of memory), kvazaar (most of
  its own allocation failures, e.g. `kvz_config_init(NULL)`).
* libheif 1.23.7: `heif_track_encode_end_of_sequence()` does not clear the track's active encoder, so releasing
  the encoder before `heif_context_write()` is a use-after-free (encoder side only; tests avoid it).
* Multi-page TIFF has no total-pixel cap (200 pages of up to `IMG2_MAX_PIXELS` each); the public contract lists
  the 2^30 total only for GIF/APNG/WebP/JXL/HEIF/AVIF.
* RAW decoding always decodes JPEG previews with quantsmooth deblocking (ignores `jpeg_deblock`).
* stb_truetype still trusts offsets inside tables (cmap subtables, loca/glyf, hmtx, kern, CFF) at glyph lookup
  time; only the sfnt header, table directory and the tables `stbtt_InitFont()` reads are validated.
* JXL: Brotli-compressed (`brob`) Exif/XMP boxes are not decompressed (no metadata from them).
* `metadata/xml_parser.c` does not decode entities (`&amp;` etc. stay literal in XMP strings).
* Excluded but reachable in principle (`RESOURCE-BOUND`): the running total-pixel check of HEIF/AVIF sequences
  (needs > 2^30 decoded pixels, >= 4 GiB of frames in memory). Not proven unreachable: `decoder_raw.c:319` false
  outcome (non-JPEG, non-bitmap previews exist only in Canon CR3 HEIF-capture files; no fixture can be built
  in-test).

## Tests added

| Test | Covers |
|---|---|
| `test_core_edges` | image/scale/validate/orient argument and limit paths, dispatcher edges, registries, log sanitising, MIME detection, stb dimension limit (phase 1) |
| `test_decoder_edges` | ICO/CUR, QOI, PNM/PAM, YUV edge cases (phase 1) |
| `test_alloc_fail` | allocation-failure injection (phase 1) |
| `test_cov_a_*` | lane A: ICO entry choice, XML (depth, character data, threads), EXIF/XMP (hostile IFDs, every tag), fonts (user font dirs, name tables), YUV, core, allocation failures, strdup failure |
| `test_cov_b_*` | lane B: GIF composition/disposal/errors, APNG and PNG metadata, WebP metadata/animation, JPEG errors (+ many clean decodes), multi-page/BigTIFF, SVG sizing/CSS, in-test DNGs, allocation failures |
| `test_cov_c_*` | lane C: HEIF/AVIF stills, lists and sequences built in-test with the 'unci' codec, JXL limits/timing/container boxes, encoder clamps and failures, allocation failures and leak checks |
| `test_cov_p3_leak` | phase 3: XMP keyword replacement without leaks (live-block counter) |
| `test_cov_p3_jxl` | phase 3: JXL container box walk (lookalikes, bare codestream, largesize, size 0, malformed headers, box cap, brob) |
| `test_cov_p3_font` | phase 3: sfnt validation (guard-page font buffers; truncated directory, tables past the end, short cmap/head/maxp, 32-bit wrap, versions) |
| `test_cov_p3_gaps` | phase 3: GIF with a 0x0 logical screen and two frames (decoder.h total check with a zero-area canvas) |
