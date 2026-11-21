# Feature Implementation Plan: libimg2 split (static library `img2::img2` + `imgcat2` CLI)

> Canonical synthesis. The base is the mvp-first draft (structure, step numbering S0-S11, scripts). Grafts come from the maintainability-first draft (cmake/ module layout, `img2_feature()` registry, `IMG2_LINK_ORDER`, full public header set, rename map, guard ctests, `img2_add_test()`, docs) and from the risk-first draft (strict golden harness, `dupsyms.sh`, PIC/PIE handling, explicit test linkage, tag-based rollback).
>
> Authoritative inputs: `.claude/tmp/img2-split-decisions.md` (locked decisions, these override everything else here) and `.claude/tmp/img2-split-recon.md`. Orchestrator resolutions of the open points are folded in and marked **[RESOLVED]**.
>
> All paths are relative to `/mnt/nvme/imaginarium/imgcat2`. Line anchors were checked against master `a50e93d` on 2026-11-21.
>
> Round-1 refinement (2026-11-21): validation findings from `.claude/tmp/img2-plan-round1-findings.md` are folded in and tagged inline as `[H1]`..`[H3]`, `[M1]`..`[M7]`, `[L1]`..`[L6]`, `[SM1]`..`[SM4]`, `[SL1]`..`[SL3]`, `[SI1]`/`[SI2]`, so a reviewer can grep for each one. Round-2 findings (`.claude/tmp/img2-plan-round2-findings.md`) are tagged `[R1]`..`[R18]` the same way, and round-3 findings (bottom of the same file) are tagged `[R19]` and `[Q1]`..`[Q12]`. Round-4 findings are tagged `[R20]` and `[P1]`..`[P9]`.

---

## 1. Requirements Summary

### Functional Requirements
- [FR-1] A static library target `img2` with alias `img2::img2`. Public headers live under `include/img2/` and are used as `#include <img2/img2.h>` (or a single `<img2/X.h>`).
- [FR-2] The CLI `imgcat2` links that library. From S7 on it uses **only** public headers. Its behaviour is unchanged until S10.
- [FR-3] Every non-static symbol (function or global object) defined in our library code starts with `img2_` (one underscore, internal symbols too; never `img2__`). Types are `img2_*_t`. Macros and enum constants are `IMG2_*`.
- [FR-4] Header-only vendors compiled into our translation units (TUs) are TU-local or img2-prefixed: stb_image, stb_image_resize2, stb_truetype, the local nanosvg fork and qoi. The nanosvg fork reaches glyph data only through internal `img2_font_face_*` wrappers.
- [FR-5] Errors are reported as `img2_status_t` codes (`IMG2_OK = 0`, negative errors) plus `img2_strerror()`. Results come back through out-params. A log callback with levels ERROR/WARN/INFO/DEBUG replaces every library `fprintf(stderr, ...)`.
- [FR-6] The library never terminates the process: no `exit`/`_exit`/`_Exit`/`quick_exit`/`abort`/`assert` in our objects. The `img2_symbols` ctest enforces this, together with the prefix rule.
- [FR-7] Process-global `img2_init()` / `img2_shutdown()`. Decoder and encoder registries are const tables with accessor functions. The font cache sits behind a mutex with lazy init. The log callback is swapped atomically. Per-call settings come from option structs.
- [FR-8] The public API is the approved set: `img2_decode`, `img2_detect_mime`, `img2_encode`, `img2_free`, `img2_scale` + `img2_filter_t`, `img2_apply_orientation`, `img2_frames_free`, `img2_can_decode`, `img2_can_encode`, metadata types and functions, `img2_font_*`, and the log API. **[RESOLVED]** It also includes these additive helpers the CLI needs: `img2_mime_name`, `img2_mime_string`, `img2_is_animated`, `img2_decoder_count`/`img2_decoder_info`, `img2_encoder_count`/`img2_encoder_info`, the `img2_yuv_*` helpers, `img2_font_foreach`, `img2_image_create`/`img2_image_destroy` (`img2_image_create` returns `img2_status_t` with an out-param, **[H1]**), `img2_fit_size`, `img2_scale_fit` (**[R6]**), `img2_version_string`, `IMG2_DECODE_OPTS_INIT`/`IMG2_ENCODE_OPTS_INIT`.
- [FR-9] Public headers contain no feature `#if`/`#ifdef`. Every function and struct always exists. An unbuilt codec returns `IMG2_E_UNSUPPORTED`. A generated `img2_config.h` carries informative `IMG2_HAS_*` and `IMG2_VERSION_*` values.
- [FR-10] The image model: `img2_image_t {width, height, stride, pixels, delay_ms, exif, xmp}` and `img2_frames_t {frames, count, loop_count}`. The exif/xmp fields are always present and always freed by the library.
- [FR-11] Source tree: `include/img2/`, `src/libimg2/{core,decoders,encoders,metadata,text,font}`, `src/imgcat2/` (CLI: `main.c`, `cli`, `pipeline`, `metadata`, `base64`, `ansi/`, `terminal/`), `src/tests/`.
- [FR-12] CMake: options `IMG2_ENABLE_<X>`, `IMG2_USE_SYSTEM_<X>`, `IMG2_BUILD_CLI`, `IMG2_BUILD_TESTS`, `IMG2_INSTALL` (defaults from `PROJECT_IS_TOP_LEVEL`). The old names are dropped. `cmake_minimum_required(VERSION 3.21)`. Modules live under `cmake/` (see S1). No `FORCE` cache writes leak to consumers. `-static` and the stack-size flag go on the `imgcat2` executable only.
- [FR-13] Install installs a fat `libimg2.a` (own objects plus every vendored archive; `ar -M` MRI script on Linux, `libtool -static` on macOS marked best effort/untested), `img2Config.cmake` and `img2ConfigVersion.cmake`. These define the imported target `img2::img2` with INTERFACE deps pthread, m, OpenMP/gomp and stdc++. Both `find_package(img2)` and `add_subdirectory`/FetchContent consumers work and are smoke-tested.
- [FR-14] The CLI `--interpolation` option maps to `img2_filter_t` (S10, late step).
- [FR-15] `delay_ms`/`loop_count` are filled from GIF, APNG, WebP, AVIF/HEIF and JXL (S11, last step).
- [FR-16] Documentation: `docs/libimg2.md` (API overview, ownership, errors and logging, threading, limits, known limitations, the "Adding a codec" checklist) plus a README library section.

### Non-Functional Requirements
- [NFR-1] Every step ends with a green build, a green full ctest, and a **byte-identical CLI output** check against master `a50e93d` on the golden set. The check compares exit code, stdout md5, raw stderr bytes, and the md5 of every output file. Only S10 and S11 may change output, and only for the listed cases.
- [NFR-2] Linux `imgcat2` stays a fully static, non-PIE binary, as today.
- [NFR-3] Guards are ctests, not conventions: `img2_symbols`, `img2_layering_lib`, `img2_layering_cli`, `img2_public_headers`, the consumer tests.
- [NFR-4] One commit per step (S2 is two commits: a content-free `git mv`, then fixes). Conventional commit messages on branch `feature/split`. Never push.
- [NFR-5] Every build and test runs with `export TMPDIR=/mnt/nvme/imaginarium/imgcat2/.claude/tmp`. Nothing writes to `/tmp`.
- [NFR-6] Thread safety: concurrent `img2_decode`/`img2_encode`/`img2_scale` calls on distinct data are safe. Log configuration may change while other threads log. `img2_shutdown` must not race with other calls (documented). **[SM3]** The claim is backed by an explicit audit (mutable-data allow-list in `img2_symbols`) and by `test_threads` (PNG/JPEG/GIF/SVG decode + scale + PNG encode in parallel). Any vendor that the audit cannot clear is listed in `docs/libimg2.md` as not thread-safe; the docs do not claim more than the audit and test prove.
- [NFR-8] **[SM1]/[SM2]** Untrusted input: `img2_decode`/`img2_is_animated` enforce `IMG2_MAX_FILE_SIZE` on `len`. Every frame allocation goes through the single dimension/pixel gate (`img2_image_calc_size`, old image.c:21-40). Every public entry that takes a caller-built `img2_image_t`/`img2_frames_t` validates it first. These new checks log only at `IMG2_LOG_DEBUG`, so CLI output stays byte-identical. Pre-existing ERROR lines on the same paths (e.g. `image_scale_resize: invalid target dimensions`, `image_create: invalid dimensions`) keep their level and text (**[R6]**).
- [NFR-9] **[SL2]** Hardening: our own sources build with `-fstack-protector-strong` and `_FORTIFY_SOURCE=2` (unless the toolchain already defines it), plus `-Wformat=2 -Wformat-security -Werror=format-security` (**[SM4]**). The `imgcat2` executable links with `-Wl,-z,relro,-z,now`. None of this may change CLI output; the golden gate proves it.
- [NFR-7] The work runs unattended overnight. Gates are fully scripted (`.claude/tmp/img2-split/gate.sh`). Each step fits one agent session.

### Success Criteria
- [SC-1] `gate.sh <step>` exits 0 after every step: configure, build, `ctest -LE expensive` green, `golden: 0 mismatches`, tree hygiene clean.
- [SC-2] The golden check reports 0 mismatches after S1-S9 and S11. After S10, only the allowed cases differ (listed in S10), and the reference is rebased.
- [SC-3] `img2_symbols` passes: only `img2_`-prefixed defined globals in `$<TARGET_FILE:img2>`, and no references to `exit _exit _Exit quick_exit abort __assert_fail __assert_perror_fail __assert`, also for the `-UNDEBUG` object variant.
- [SC-4] `img2_consumer_find_package` (default on) passes: install, then an external project uses only `find_package(img2 1.0 REQUIRED)` + `target_link_libraries(app PRIVATE img2::img2)`, builds as a normal PIE executable and runs. `img2_consumer_subdir` (label `expensive`, opt-in) passes once in the S9 gate.
- [SC-5] `grep -rn 'fprintf(stderr' src/libimg2` finds only commented-out lines. There is no `cli.h`/`cli_options_t` under `src/libimg2`. There is no feature conditional in `include/img2/*.h`. The CLI includes only `<img2/...>` plus its own headers.
- [SC-6] Adding a codec touches the fixed file set documented in `docs/libimg2.md` ("Adding a codec").

### Assumptions
- The host has `python3` 3.10, `cmake` 4.1.0, `gcc` 11.4, `git` 2.34 and binutils (`nm`, `ar -M`, `objdump`). There is no `project-forge.yaml`, so builds use Bash `cmake`/`ctest` (decision constraint).
- The default build type is `RelWithDebInfo` (CMakeLists.txt:5-7), which defines `NDEBUG`. Mapping vendor asserts to no-ops therefore does not change behaviour. The `-UNDEBUG` object variant in S6 proves the mapping explicitly.
- **[RESOLVED] Encode quality semantics are per format, preserved for byte identity.** Public `quality` is 0..100, and `-1` means the format default.
  - JPEG: 0..100 as today (default 90).
  - PNG: zlib level = `quality / 10`, clamped to 0..9 (default `-1` → level 6). The CLI passes `png_compression * 10` and iTerm2 passes `30`, so the mapping round-trips exactly (6 → 60 → 6, 3 → 30 → 3).
  - HEIF: 0..100 (default 80).
  - WebP: 0..99 lossy, 100 lossless (default 80).
  - JXL: 0..94 lossy (distance = (100 − q) / 10), **>= 95 lossless** as today (encoder_jxl.c:58) (default 80).

  This deviates from the decision's "100 = lossless where supported": WebP and JXL are lossless at 100 (JXL already from 95), PNG is always lossless, and JPEG/HEIF are never lossless. **[M1c] Accepted by the orchestrator:** JXL stays lossless at `quality >= 95` (encoder_jxl.c:58), because changing it would change `--jxl 95..99` output. The deviation is documented in `<img2/encode.h>` and in the "Deviations from locked decisions" list of `docs/libimg2.md` (S9).
- **[RESOLVED, M1b — orchestrator decision; overrides the earlier "keep vendor default handlers"] libpng and libtiff diagnostics go through `img2_log`, byte-identical.** Decision 5 asks for `png_set_error_fn` and libtiff handlers. The text and level must reproduce exactly what the vendor default handlers print today, so CLI stderr does not change:
  - libpng (the 5 `png_create_read_struct(..., NULL, NULL, NULL)` sites, decoder_png.c:89/:173/:261/:353/:498): static handlers pass to `png_create_read_struct`. Error: `img2_log(IMG2_LOG_ERROR, "libpng error: %s", msg ? msg : "undefined")`, then `png_longjmp(png_ptr, 1)`. Warning: `img2_log(IMG2_LOG_WARN, "libpng warning: %s", msg)`. These match `png_default_error`/`png_default_warning` (vendor/libpng/pngerror.c:658-668, :700-708: `"libpng error: %s"` / `"libpng warning: %s"` + newline). The static fast path (decoder_png.c:751-774, :820) already has its own silent handlers and stays unchanged. encoder_png.c:96-119 already uses custom handlers whose `fprintf` text becomes `img2_log` in S4.
  - libtiff (the 3 `TIFFClientOpen` sites, decoder_tiff.c:161/:198/:302): per-handle handlers through `TIFFOpenOptions` + `TIFFClientOpenExt` (vendored libtiff 4.7.2; API at vendor/libtiff/libtiff/tiffio.h:517-551). Each handler returns 1, so the process-global default is not called as well (tif_error.c:110-117). The text reproduces `unixErrorHandler`/`unixWarningHandler` (vendor/libtiff/libtiff/tif_unix.c:367-385): error = `"<module>: " + vformatted + "."`; warning = `"<module>: Warning, " + vformatted + "."`. The `"<module>: "` part is omitted when `module == NULL`. Error → `IMG2_LOG_ERROR`, warning → `IMG2_LOG_WARN`. Per-handle handlers keep no process-global state, which is a thread-safety gain over `TIFFSetErrorHandlerExt` (decision 5's wording). **[Q12]** Messages libtiff emits before the handle exists go through `_TIFFErrorEarly` (vendor/libtiff/libtiff/tif_error.c:80-92), which calls **our** `TIFFOpenOptions` error handler with `tif == NULL`. Our handler ignores `tif`, so the bytes are the same; it returns 1, so the default handler is not called as well. Only messages emitted with neither a handle nor the options (legacy `TIFFErrorExt`/`TIFFWarningExt` calls) reach the unchanged default handler, also with the same bytes. With a host libtiff older than 4.5 (`IMG2_USE_SYSTEM_TIFF=ON`), `#if` falls back to `TIFFClientOpen` and the default handlers (documented).
  - libjpeg's `error_exit` is already overridden (decoder_jpeg.c:50-61). giflib, LibRaw, libheif, libwebp and libjxl return codes. S6 audits that every vendor error path is caught (setjmp, handler or return code).
- **[RESOLVED, M1a — accepted by the orchestrator] Font cache:** a statically initialised mutex plus an `initialized` flag checked under it. This replaces the decision's `pthread_once`, because a literal `pthread_once` would block re-init after `img2_shutdown()`. Documented in `docs/libimg2.md` "Deviations from locked decisions".
- **[RESOLVED] `img2_apply_orientation()`** applies `(*img)->exif->orientation` and does **not** reset it. The CLI prints the original orientation in `--info` after applying it (pipeline.c:513-531 runs before main.c:175-199). This is documented as "call once".
- **[RESOLVED] Log levels.** The library default is `IMG2_LOG_ERROR`. Library messages map as follows: unconditional error text → ERROR; unconditional `Warning:`/`Note:`/`Info:` text → WARN; messages previously gated by `!opts->silent` → INFO. The CLI sets WARN at start-up and INFO with `-v`, so stderr stays byte-identical. The CLI has no `--silent` flag: `silent` is its default and `-v` clears it. Decision 4's "CLI --silent lowers level" is realised as "default WARN, `-v` raises to INFO".
- `img2_encode()` encodes `in->frames[0]` (multi-frame encoding is out of scope). The CLI passes a one-frame `img2_frames_t` view.
- **[RESOLVED, H1 — orchestrator decision] Public constructors return `img2_status_t`:**
  - `img2_status_t img2_image_create(uint32_t width, uint32_t height, img2_image_t **out)`;
  - `img2_status_t img2_font_load(const char *family, img2_font_weight_t weight, img2_font_style_t style, img2_font_t **out)`;
  - `img2_status_t img2_font_load_fallback(img2_font_t **out)`.

  Rename-once still holds. In S5 the old pointer-returning internals get their **final internal** names: `image_create` → `img2_image_alloc` (155 uses in 41 files: ~40 in the library, ~115 in tests; none in the CLI), `font_manager_load` → `img2_font_find` (one library caller, nanosvg.h:2972), `font_manager_load_fallback` → `img2_font_find_fallback`. S7 adds the public status-returning wrappers under the public names. No identifier is renamed twice.
- **[RESOLVED, M7 — orchestrator decision] Status codes in the internal chain.** Where the conversion is cheap, internal helpers return `img2_status_t` with out-params (S6): `img2_image_scale_fit`, `img2_image_scale_resize`, `img2_convert_rgb_to_rgba`, `img2_convert_gray_to_rgba`. Their callers are only image.c, pipeline.c:634/:638 (replaced by `img2_scale` in S7) and test_image_processing.c. Documented exceptions to decision 4 (listed in `docs/libimg2.md`):
  - allocator-style constructors and lookups that return a pointer, NULL meaning invalid input or OOM with a logged reason: `img2_image_alloc`, `img2_font_find`, `img2_font_find_fallback`, `img2_font_face_open`, `img2_xml_document_create` (xml_parser.h:52) and the `img2_xml_element_*` accessors, `img2_decoder_find`/`img2_encoder_find`, `img2_decoder_info`/`img2_encoder_info`;
  - predicates and size helpers that return `bool`: `img2_image_calc_size`, `img2_yuv_frame_size`, `img2_*_is_animated`, `img2_is_animated`, `img2_can_decode`/`img2_can_encode`;
  - `void` destructors.
- iTerm2/Kitty render paths cannot be golden-tested: without a TTY the pixel size is unknown and the CLI forces ANSI (main.c:106-111). The unit tests `test_terminal` and `test_file_render` cover them.

### Out of Scope
- Routing diagnostics of vendors other than libpng/libtiff through `img2_log` (they report through return codes; libjpeg already goes through `img2_log` from S4). libpng/libtiff routing is **in scope** since round 1 (**[M1b]**).
- A total-pixels-across-frames cap (**[SM1]**). The per-frame gate plus `max_frames` stay. A post-decode sum cannot bound peak memory, because decoders allocate frame by frame, and an in-decoder cap would change CLI behaviour for large animations (NFR-1). Documented in `docs/libimg2.md` "Limits".
- C++ vendor aborts (`std::terminate`, `JXL_ABORT`). This is a documented known limitation (decision 5).
- Symbol clashes between compiled vendor libraries in the fat archive (`png_*`, `jpeg_*`, ...) and a consumer's own copies. That is the consumer's problem (decision 3).
- The CLI using per-frame `delay_ms` for playback. The CLI keeps `--fps` (pipeline.c:746).
- Shared-library builds, Windows packaging, a pkg-config `.pc` file, and macOS fat-archive verification (the macOS path is written but untested).
- Any change inside `vendor/**`. The user's pre-existing `M vendor/nanosvg` stays as it is.
- Renaming internal CMake helpers and variables (`imgcat2_vendor_begin/end`, `IMGCAT2_VENDORED_*`, `IMGCAT2_WARNING_FLAGS`, `IMGCAT2_TEST_TMPDIR`). They are invisible to consumers, so renaming them is churn.

## 2. Architecture Analysis

### Affected Subsystems
- **Build system**: `CMakeLists.txt` (1919 lines; global `add_definitions` at :276, :381, :515, :559, :591, :611, :884, :1033, :1113, :1139, :1203, :1246, :1332, :1396, :1446, :1623; global `include_directories` :1498-1539; `-static` :52; `-O3 -g` :118). After the work: a thin top-level file plus `cmake/` modules, `src/libimg2/CMakeLists.txt`, `src/imgcat2/CMakeLists.txt` and `src/tests/CMakeLists.txt` (rewritten around `img2_add_test`).
- **Library core**: `src/imgcat2/core/image.{c,h}` → `src/libimg2/core/`. New files: `log.{c,h}`, `status.{c,h}`, `init.c`, `api.c`.
- **Decoders/MIME**: `src/imgcat2/decoders/*` (23 tracked files: `decoder.{c,h}`, 16 `decoder_*.c`, `magic.{c,h}`, `nanosvg.h`, `nanosvgrast.h`, `yuv.h`) → `src/libimg2/decoders/`.
- **Encoders**: `src/imgcat2/encoders/*` (7 files) → `src/libimg2/encoders/`.
- **Metadata**: `src/imgcat2/metadata/{exif_reader,xml_parser}.{c,h}` → `src/libimg2/metadata/`.
- **Text/font**: `src/imgcat2/text/font_manager.{c,h}` → `src/libimg2/text/`. The `src/imgcat2/font/*.ttf` files → `src/libimg2/font/`. The generated `embedded_fonts.{c,h}` are removed from git and generated in the build dir.
- **CLI**: `src/imgcat2/core/{cli,pipeline,metadata,base64}.{c,h}` are flattened to `src/imgcat2/`. `main.c`, `ansi/` and `terminal/` stay.
- **Tests**: `src/tests/**`. Tests stop recompiling library sources (src/tests/CMakeLists.txt:443, :460-461, :494, :517, :538, :558, :578, :598, :757-759). New tests: `test_log`, `test_threads`, `test_public_api`, the guard tests, and the consumer project.
- **Docs**: `README.md` (:274, :835 option names; new library section) and the new `docs/libimg2.md`.

### Integration Points
- Target graph (final): `imgcat2` (exe, `main.c`, `-static`) → `imgcat2_cli` (STATIC, CLI internals) → `img2_bundled` (IMPORTED, the fat `libimg2.a`; from S9) or `img2` (STATIC, own objects; PRIVATE links to the vendored targets in `IMG2_LINK_ORDER`). `img2_testing` (INTERFACE, never installed) gives white-box tests the internal include dirs and the `IMG2_HAVE_*` definitions.
- CLI → library calls today (all through internal headers):
  - `decoder_decode(cli_options_t*, ...)` (decoder.h:137) and `decoder_registry_init(cli_options_t*)` (decoder.h:80);
  - `encoder_encode` (encoder.h:127) from file.c:161, and `encode_png`/`encode_jpeg` called directly from iterm2.c:105/:113;
  - `image_scale_fit`/`image_scale_resize` (pipeline.c), `image_apply_orientation` (pipeline.c:526);
  - `detect_mime_type` (pipeline.c:479, kitty.c, iterm2.c:28) and `*_is_animated` (kitty.c:44-78, iterm2.c:35-69);
  - `font_manager_*` (main.c:94-96), `g_decoder_registry` (cli.c:129-131), `get_mime_string`/`mime_type_name`;
  - `yuv_parse_spec` (cli.c:249) and `yuv_frame_size` (pipeline.c:501).
- Library → CLI edges to cut in S4:
  - `decoders/decoder.h:16` and `encoders/encoder.h:16` include `../core/cli.h`;
  - `output_format_t` is defined in `core/cli.h:21-28`;
  - `decoder.c:186/244/262/310` and `encoder.c:90` read `opts->silent`;
  - `decoder.c:239-248` reads `opts->yuv_input` and `&opts->yuv`;
  - `get_mime_string` lives in CLI `core/metadata.c:20`.
- Library → vendor headers: `decoder.h:13` includes `<png.h>` (internal header only). `nanosvg.h:201-202` includes `../../src/imgcat2/text/font_manager.h` and `../stb/stb_truetype.h`.
- Data flow: buffer → `img2_detect_mime` → `img2_decode` → frames → (CLI) `img2_apply_orientation` → `img2_fit_size` + `img2_scale` → ANSI/iTerm2/Kitty render, or `img2_encode` → file.

### Constraints
- Byte identity covers stdout, stderr, exit codes and written files. Library log text must stay **verbatim**, including prefixes such as `Error: ` and `image_create: `. Callers drop only the trailing `\n`; the default sink re-adds it. String literals are never renamed.
- `-Werror` applies to project targets (CMakeLists.txt:119, applied at :1482). Making stb/nanosvg functions `static` triggers `-Wunused-function`, so those includes are wrapped in `#pragma GCC diagnostic push/ignored "-Wunused-function"/pop`.
- The static link order must stay exactly as today: PNG, ZLIB, jpegqs, JPEG, GIF, WEBP, HEIF, TIFF, RAW, JXL, RESVG, then `IMGCAT2_VENDORED_TRAILING_LIBS` (CMakeLists.txt:1455-1477), `m`, libc++ (CMakeLists.txt:1669-1774).
- Vendor block order: zlib, jpeg, jpegqs, png, gif, webp, lcms2, brotli, heif, tiff, raw, jxl, header-only, resvg, trailing. Project warnings and tidy come only after all vendors (CMakeLists.txt:1479-1496).
- Vendors must keep compiling with exactly today's flags: `-O3 -g`, `NDEBUG`, global `-static` and the stack-size flag visible to vendor configure checks. Otherwise FP contraction and inlining could change codec output bits.
- `src/imgcat2/compat/` is untracked and not ours. It must never be moved, deleted, formatted or globbed into a target. The format target globs sources (CMakeLists.txt:1781) and must exclude it.
- `compile_commands.json` is exported (CMakeLists.txt:10). Fan-out lanes use it for single-TU syntax checks without touching `build/`.

## 3. Captured Information

### Existing Patterns

**P1. Logging today (to be replaced).** Each call is unconditional, single-line and ends in `\n`. No library `fprintf(stderr` lacks its trailing `\n` and none is split across calls (mvp draft verification; S4 re-checks with a grep).
```c
// src/imgcat2/decoders/decoder.c (around :258-263)
fprintf(stderr, "Error: No decoder available for format: %s\n", mime_type_name(mime));
...
if (opts != NULL && !opts->silent) {
    fprintf(stderr, "Decoding %zu bytes with decoder: %s\n", len, decoder->name);
```
The gated sites are decoder.c:186-191, :244-246, :262-264, :310-312 and encoder.c:90-94; they become INFO. Unconditional `Warning:`/`Note:`/`Info:` sites become WARN. Every other unconditional site becomes ERROR. Commented-out `// fprintf(...)` lines (e.g. encoder_png.c:151, :155) are left alone.

**P2. Decoder entry signature today.** It returns NULL and gives no error code.
```c
// src/imgcat2/decoders/decoder.h:34
typedef image_t **(*decode_func_t)(const uint8_t *data, size_t len, int *frame_count);
// src/imgcat2/decoders/decoder_jpeg.c:78
image_t **decode_jpeg(const uint8_t *data, size_t len, int *frame_count)
```

**P3. Encoder entry signature today.** It returns 0 or -1, and callers test `!= 0`, so negative status codes stay compatible.
```c
// src/imgcat2/encoders/encoder.h:38
typedef int (*encode_func_t)(const image_t *img, int quality, uint8_t **out_data, size_t *out_size);
// src/imgcat2/terminal/file.c:161
if (encoder_encode(img, opts->output_format, quality, &encoded_data, &encoded_size) != 0) {
```
Quality selection happens in the CLI (file.c:144-155). PNG quality is the zlib level, clamped 0..9 in encoder_png.c:149-157. JXL is lossless at `quality >= 95` (encoder_jxl.c:58). CLI defaults are at main.c:53-57 (JPEG 90, PNG 6, HEIF/WebP/JXL 80).

**P4. Compile-time registry rows (made const + accessor-based in S6).**
```c
// src/imgcat2/decoders/decoder.c:102-113, 167-168
static const decoder_t s_decoder_registry[] = {
#ifdef HAVE_LIBPNG
	{ MIME_PNG,  "PNG (libpng)",         decode_png          },
#else
	{ MIME_PNG, "PNG (stb_image)", decode_stb },
#endif
...
const decoder_t *g_decoder_registry = NULL;
size_t g_decoder_count = 0;
```
The encoder equivalent is at encoder.c:46-72. The "not initialized" error paths are at decoder.c:202-205 and encoder.c:106-109.

**P5. setjmp error recovery for libjpeg/libpng (kept as is).**
```c
// src/imgcat2/decoders/decoder_jpeg.c:50-61
static void jpeg_error_exit(j_common_ptr cinfo)
{
	struct jpeg_error_mgr_ext *err = (struct jpeg_error_mgr_ext *)cinfo->err;
	char buffer[JMSG_LENGTH_MAX];
	(*cinfo->err->format_message)(cinfo, buffer);
	fprintf(stderr, "Error: libjpeg error: %s\n", buffer);
	longjmp(err->setjmp_buffer, 1);
}
```
Other setjmp sites: encoder_jpeg.c:38, encoder_png.c:96 and decoder_png.c (static and APNG paths). libpng's own default handlers (pngerror.c:658-668 error → `"libpng error: %s"` + longjmp; :700-708 warning → `"libpng warning: %s"`) are active at the 5 NULL-handler sites decoder_png.c:89/:173/:261/:353/:498. They are replaced by `img2_log` handlers with identical text in S6 (**[M1b]**). Pattern for the replacement (decoder_png.c:751-774 already does the longjmp part):
```c
// src/imgcat2/decoders/decoder_png.c:751-760
static void png_static_error_fn(png_structp png_ptr, png_const_charp message)
{
	png_static_error_ctx *ctx = (png_static_error_ctx *)png_get_error_ptr(png_ptr);
	if (ctx != NULL && message != NULL) {
		snprintf(ctx->message, sizeof(ctx->message), "%s", message);
	}
	png_longjmp(png_ptr, 1);
}
```

**P6. Memory ownership and the NULL-safe destroy.** `image_create` (image.c:42) calloc's the pixels and returns NULL on failure. `image_destroy` (image.c:75-105) is NULL-safe but frees exif/xmp **only under `HAVE_EXIF_READER`** (image.c:87-101). That makes the ABI depend on the flag; S6 removes the dependence.
```c
// src/imgcat2/core/image.c:87-93
#ifdef HAVE_EXIF_READER
	if (img->exif != NULL) {
		exif_info_free(img->exif);
		free(img->exif);
```
Frame arrays come from malloc and are freed by `decoder_free_frames` (decoder.c:323). The CLI frees them by hand at main.c:275-289.

**P7. Mutable process globals in the library** (each must become const, per-call or locked):
- `g_decoder_registry`/`g_decoder_count` (decoder.c:167-168);
- `g_encoder_registry`/`g_encoder_count` (encoder.c:71-72);
- `g_font_cache`/`g_fallback_font`/`g_initialized` (font_manager.c:40-42);
- `s_jpegqs_opts` (decoder_jpeg.c:24, used at :120);
- the function-local `font_mgr_initialized` (decoder_nanosvg.c:265-270).
- Header-only vendor statics compiled into our TUs (**[SM3]**): stb_image's process-wide flags (vertical flip, unpremultiply, iPhone-PNG conversion, HDR gamma/scale) are only written by the `stbi_set_*`/`stbi_*_gamma`/`stbi_*_scale` setters. `src/` has no call to any of them today (verified: no `stbi_set_`, `stbi_convert_iphone`, `stbi_ldr_to_hdr`, `stbi_hdr_to_ldr` match), so they are effectively read-only and must stay untouched.
- Font discovery (**[SL1]**): `expand_path()` reads `getenv("HOME")` (font_manager.c:81) to expand `~` in the search lists at font_manager.c:56 (Linux: `/usr/share/fonts`, `/usr/local/share/fonts`, `~/.local/share/fonts`, `~/.fonts`), :60 (macOS) and :64 (Windows).

**P8. Header-only vendor instantiation sites:**
- `STB_IMAGE_RESIZE_IMPLEMENTATION` image.c:12 (include at :14). The only stbir callers are `stbir_resize_uint8_srgb(...)` at image.c:158 and :187.
- `STB_TRUETYPE_IMPLEMENTATION` font_manager.c:47 + `#include "../../vendor/stb/stb_truetype.h"` at :48. `embedded_fonts.h` is included at :45.
- `STB_IMAGE_IMPLEMENTATION` decoder_stb.c:15 (include at :18).
- `QOI_IMPLEMENTATION` decoder_qoi.c:20 + `#include "../../vendor/qoi/qoi.h"` at :22. qoi.h has no `QOIDEF`. `qoi_encode`/`qoi_decode` are extern (qoi.h:278/:289); `qoi_read`/`qoi_write` sit under `#ifndef QOI_NO_STDIO`.
- `NANOSVG_IMPLEMENTATION`/`NANOSVGRAST_IMPLEMENTATION` decoder_nanosvg.c:22-25. The fork's API is at nanosvg.h:177-187 and nanosvgrast.h:52/:63/:68.
- Upstream static switches: `STB_IMAGE_STATIC` (stb_image.h:395-398), `STB_IMAGE_RESIZE_STATIC` (stb_image_resize2.h:402), `STBTT_STATIC` (stb_truetype.h:502).
- Assert hooks: `STBI_ASSERT` (stb_image.h:603), `STBIR_ASSERT` (stb_image_resize2.h:783, default `assert(x)`), `STBTT_assert` (stb_truetype.h:477).

**P9. nanosvg text uses stb_truetype directly, and must go through wrappers.**
```c
// src/imgcat2/decoders/nanosvg.h:201-202
#include "../../src/imgcat2/text/font_manager.h"
#include "../stb/stb_truetype.h"
// nanosvg.h:2880 (measureText, order prev_cp, cp)
int kern = stbtt_GetCodepointKernAdvance(font, prev_cp, cp);
// nanosvg.h:3036-3037 (render loop, REVERSED order codepoint, prev_codepoint)
int kern = stbtt_GetCodepointKernAdvance(&stbtt_font,
                                          codepoint, prev_codepoint);
```
**Do-not-fix note:** the reversed kerning arguments at nanosvg.h:3036-3037 look like a bug. Fixing them would change SVG text output bytes and break the golden gate. Keep the argument order of **both** calls verbatim when switching to `img2_font_face_kern`, and add the comment `/* argument order kept verbatim for byte identity; see docs/libimg2.md */`. Other stbtt users are `nsvg__measureText` (nanosvg.h:2865-2887), `nsvg__addGlyphToPaths` (nanosvg.h:2892-2961; `STBTT_vmove/vline/vcurve/vcubic` = 1/2/3/4) and the render loop (nanosvg.h:2972-3046).

**P10. Fit math (copied verbatim into `img2_fit_size`).** It uses float aspect math with `roundf`; the output must stay identical.
```c
// src/imgcat2/core/image.c:119-128
float src_aspect = (float)src->width / (float)src->height;
float target_aspect = (float)target_width / (float)target_height;
uint32_t new_width, new_height;
if (src_aspect > target_aspect) {
	new_width = target_width;
	new_height = (uint32_t)roundf((float)new_width / src_aspect);
```
The full block is image.c:118-142, followed by validation. `IMG2_FILTER_DEFAULT` must keep calling exactly `stbir_resize_uint8_srgb(src, w, h, 0, dst, nw, nh, 0, STBIR_RGBA)` (image.c:158/:187).

**P11. `--fonts` listing (reproduced by the CLI on top of `img2_font_foreach`)** (font_manager.c:683-764):
- Pass 1 walks the cache. Each new family is **prepended** to a list. Totals and embedded counts (`path == "[embedded]"`) are accumulated.
- It prints `"Available fonts (%d total, %d embedded):\n\n"`.
- For each family in list order it prints `"%s: "`, then every cache entry of that family in cache order as `"%s/%s"` (weight `regular`/`bold`/`unknown`, style `normal`/`italic`/`unknown`, font_manager.c:659-678) joined by `", "`, then `"\n"`.

**P12. Naming conventions.** Types use a `_t` suffix. Enum constants are upper-case with a domain prefix (`MIME_PNG`, `FORMAT_JPEG`, `YUV_FMT_I420`). Guards are `IMGCAT2_<NAME>_H`, except `EXIF_READER_H` and `XML_PARSER_H`. File-local data is `static` with an `s_`/`g_` prefix. Code style: tabs, K&R braces, `/** @brief */` Doxygen.

**P13. Test registration today** (src/tests/CMakeLists.txt:93-107 style; `imgcat2_lib` is used 44 times):
```cmake
add_executable(test_decoder_png unit/main.c unit/test_decoder_png.c)
target_link_libraries(test_decoder_png imgcat2_lib)
add_test(NAME test_decoder_png COMMAND test_decoder_png)
set_tests_properties(test_decoder_png PROPERTIES TIMEOUT 10 WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
```
TMPDIR for every test comes from the loop at src/tests/CMakeLists.txt:773-778, which must stay last. Tests link `-static` today because the global `CMAKE_EXE_LINKER_FLAGS` (CMakeLists.txt:52) applies to them too.

**P14. Vendor helper macros** (CMakeLists.txt:169-181). `BUILD_TESTING` here is vendor-facing and is not renamed.
```cmake
macro(imgcat2_vendor_begin)
	set(_IMGCAT2_SAVED_BUILD_TESTING ${BUILD_TESTING})
	set(BUILD_TESTING OFF)
```

### Type Definitions (current; final names in the S5 rename map)
- `image_t` (image.h:44-52): `uint32_t width, height; uint8_t *pixels; exif_info_t *exif; xmp_info_t *xmp;`. Forward typedefs are at image.h:33-34 (`typedef struct exif_info_t exif_info_t;`). Limits (image.h:22-28): `IMAGE_MAX_DIMENSION 65536`, `IMAGE_MAX_PIXELS 200000000UL`, `IMAGE_MAX_FILE_SIZE 1073741824UL`. Inline `image_get_pixel` is at :91 and `image_set_pixel` at :113.
- `mime_type_t` (magic.h:20-41): `MIME_UNKNOWN=0, PNG, JPEG, GIF, BMP, TGA, PSD, HDR, PNM, WEBP, HEIF, TIFF, RAW, QOI, ICO, CUR, JXL, SVG, AVIF, YUV`. Detection never returns `MIME_YUV`. The `MAGIC_*` arrays are used only in magic.c.
- `output_format_t` (cli.h:21-28): `FORMAT_NONE=0, JPEG, PNG, HEIF, WEBP, JXL`.
- `yuv_format_t` (yuv.h:35-46): `YUV_FMT_NONE=0, I420, YV12, NV12, NV21, I422, I444, YUYV, UYVY, GRAY`. `yuv_params_t` (yuv.h:54-60): `{format; uint32_t width, height; size_t y_stride, uv_stride;}`.
- `decoder_t` (decoder.h:42): `{mime_type, name, decode}`. `encoder_t` (encoder.h:46): `{format, name, extension, encode}`.
- `font_weight_t` (font_manager.h:23-26: `FONT_WEIGHT_NORMAL=400, FONT_WEIGHT_BOLD=700`), `font_style_t` (:31-34: `FONT_STYLE_NORMAL=0, FONT_STYLE_ITALIC=1`), opaque `font_t` (:39; `struct font_t {family, weight, style, data, data_size}` at font_manager.c:31-37).
- `exif_info_t` (exif_reader.h:22-89) and `xmp_info_t` (:97-120). The struct tags equal the typedef names.
- `xml_node_type_t` (xml_parser.h:18), `xml_node_t`, `xml_document_t` (:26-27).
- `embedded_font_t` and `embedded_dejavu_{regular,bold,italic,bold_italic}{,_data,_size}`, generated by tools/embed_font.py:17-22, :64-101.

### Build System
- `cmake_minimum_required(VERSION 3.15)` (:1); `project(imgcat2 VERSION 1.0.0 LANGUAGES C)` (:2); build type default with FORCE (:5-7); `CMAKE_EXPORT_COMPILE_COMMANDS` (:10); C11 with GNU extensions (:12-14).
- Options `ENABLE_*` (:17-29, including `option(BUILD_TESTING)` :25) and `USE_SYSTEM_*` (:33-41).
- `BUILD_SHARED_LIBS ... FORCE` (:44); a second `BUILD_SHARED_LIBS` cache set at :146 (zlib-ng block) is **non-FORCE** (**[L1]**); `-static` (:52); `add_compile_options(-O3 -g)` (:118); `IMGCAT2_WARNING_FLAGS` (:119); stack size (:121-133).
- FORCE leaks (**[H3]**, complete list): `ZLIB_INCLUDE_DIR`/`ZLIB_LIBRARY` (:159-160), `JPEG_INCLUDE_DIR`/`JPEG_LIBRARY` (:272-273), `WITH_LIBSHARPYUV` (:837/:839), `WebP_INCLUDE_DIR`/`WebP_LIBRARY` (:1069-1070, tiff block), and `CACHE INTERNAL` (which implies FORCE) for `HAVE_JPEGTURBO_DUAL_MODE_8`/`_12` (:1094-1095, tiff block). `HWY_FORCE_STATIC_LIBS` (:1266), `JPEGXL_FORCE_SYSTEM_*` (:1299-1301) and the comment at :1260 only contain the word FORCE in a variable name; they are not FORCE writes. `JPEG::JPEG` (:260-266) has a guard, but its properties are still overwritten. `AOM::aom` (:802) has no guard. `unset(EXECUTABLE_OUTPUT_PATH CACHE)` is at :823.
- Non-FORCE vendor knob cache sets (**[L3]**) that still leak into an `add_subdirectory` consumer's cache: zlib-ng :139-146; libpng :492-497; libwebp :572-583; brotli :651-653; libde265 :680-684 (`ENABLE_SDL`, `ENABLE_DECODER`, `ENABLE_ENCODER`, `ENABLE_SHERLOCK265`, `ENABLE_INTERNAL_DEVELOPMENT_TOOLS`); openjpeg :812-817; libheif :847-867 (incl. `ENABLE_PLUGIN_LOADING` :847, `ENABLE_EXPERIMENTAL_FEATURES` :867); libtiff :1047-1075; highway :1266-1270; libjxl :1299-1311.
- jpegqs OpenMP: `target_link_options(jpegqs INTERFACE ${_jpegqs_omp_flags})` (:358) carries `-fopenmp` to the final link. Once jpegqs is linked PRIVATE by the STATIC `img2`, this relies on policy CMP0099 NEW (implied by `cmake_minimum_required(VERSION 3.21)`) (**[L5]**).
- PIC today: libjpeg-turbo and aom ExternalProjects pass `-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON` (:235, :767). `POSITION_INDEPENDENT_CODE ON` is set on some vendor targets (:551, :638, :747, :1176). Our own code and the other vendors are non-PIC.
- `IMGCAT2_VENDORED_TRAILING_LIBS` is defined at :1461-1477 (libjpeg-turbo, lcms2, ..., zlib last) and linked at :1754-1755.
- Section map: sources :1541-1638 (EXIF :1620-1624); embed_fonts :1640-1658 (writes into the source tree); targets :1660-1666; links :1668-1774; install :1776-1777; format :1779-1793 (glob at :1781); tests :1795-1799; summary :1801-1918.
- `src/tests/CMakeLists.txt`: `IMGCAT2_TEST_TMPDIR` (:6-8); `WORKING_DIRECTORY ${CMAKE_BINARY_DIR}` (:106, :506, :529, :609); `if(ENABLE_WEBP|HEIF|JXL|RAW)` (:645, :663, :681, :699, :717); extra link libs (:499, :522, :543, :563, :583, :725).
- `cmake/vendored-find/Find*.cmake` read `IMGCAT2_VENDORED_*`. `cmake/FindCompressionLib.cmake` is unused.
- README mentions old options at README.md:274 (`-DENABLE_RESVG=OFF`) and :835 (`-DENABLE_WEBP=ON`). No other repo script uses them.

## 4. Alternative Approaches

### Selected: Linear, gate-per-step refactor; scripted mechanical passes; ctest-enforced boundaries; file-disjoint fan-out only
**Rationale**: Every step ends byte-identical, so a regression is pinned to one small diff. The large edit classes are reviewed scripts with dry-run output: file move/include rewrite, `fprintf` → `img2_log` (~370 sites), symbol/type rename (~130 symbols + ~80 types/constants), option rename. Structural rules (prefix, no-terminate, layering, header purity) become ctests in the step that introduces them, so later steps cannot regress them. Fan-out is used only where files are disjoint and integration is a single build (S6 decoders, S9 docs, S11 delay fill).
**Trade-offs**: Some signatures change twice. S4 removes `cli_options_t`; S6 introduces status codes and out-params. There are transitional states: the CLI sees internal headers through `img2_testing` until S7, and `decoder.h` includes the CLI header through a relative shim between S2 and S4.

### Rejected: Big-bang rewrite (new API first, port everything at once)
**Reason**: There are no intermediate green states and golden diffs cannot be bisected. It conflicts with decision 12.

### Rejected: Parallel long-lived tracks in git worktrees (C sources vs CMake)
**Reason**: Each worktree needs a full vendored rebuild (aom, libjxl, libheif, LibRaw). It also conflicts with the rule "each step committed before the next starts".

### Rejected: `ld -r` + `objcopy --localize-hidden`
**Reason**: Ruled out by decision 3 (manual prefix only).

### Rejected: `install(EXPORT)` of the in-tree `img2` target
**Reason**: The installed artifact is the merged fat archive (decision 9), which is not a CMake target. A hand-written `img2Config.cmake.in` with an IMPORTED target is simpler and explicit.

### Rejected: X-macro codec list (`decoders.def`) generating registry, prototypes and CMake sources
**Reason**: The fallback rows (e.g. `PNG (stb_image)` when libpng is absent, decoder.c:103-113) and the per-row probe column read worse as macros. An explicit const table plus the documented "Adding a codec" checklist was chosen instead.

### Rejected: One public header only
**Reason**: Per-concern headers plus an umbrella keep ownership boundaries visible and let consumers include only what they use.

### Rejected: The fat archive replaces the `img2` target for `add_subdirectory` consumers
**Reason**: `add_subdirectory` consumers get the real `img2` target with normal usage requirements, which is idiomatic. Only the CLI, the tests and the installed package use the fat archive. That is enough to satisfy "the CLI links the same library" (decision 9) for the installed artifact.

## 5. Implementation Strategy

### Overview
1. Freeze a strict golden reference of master CLI behaviour.
2. Restructure the build and the tree without changing code semantics (S1-S3).
3. Cut and normalise the library's seams: logging and edges (S4), symbols (S5), status/state/no-terminate (S6).
4. Publish the public headers and move the CLI onto them (S7).
5. Make the library consumable: hygiene and PIC (S8), then fat archive, install, consumers and docs (S9).
6. Do the two behaviour-changing steps last (S10-S11).

### Step map vs. the approved outline
| This plan | Content | Approved outline |
|---|---|---|
| S0 | golden harness, gate/tu_check/dupsyms scripts | S0 |
| S1 | cmake/ modules, 3.21, `PROJECT_*`, `IMG2_*` option rename, `project(img2)` | S1 + option part of S9 |
| S2 | tree move (2 commits) + generated fonts | S2 |
| S3 | targets, `img2_feature()`, `IMG2_LINK_ORDER`, `img2_add_test()`, `img2_testing` | S3 |
| S4 | log module + cut lib→CLI edges + `img2_layering_lib` + `test_log` | S4 + S5 merged |
| S5 | static pass, TU-local vendors, nanosvg font wrappers, `img2_` rename, `img2_symbols` (prefix) | S6 |
| S6 | status codes, image model, init/shutdown, const registries, font lock, per-call opts, no-terminate, `test_threads` | S7 |
| S7 | public headers, `img2_config.h`, facade, CLI on public API, layering/header guards, `test_public_api` | S8 |
| S8 | CMake consumer hygiene, PIC, tests dynamic PIE | S9 rest |
| S9 | fat archive + dupsyms, CLI/tests link it, install/export, consumer tests, docs | S10 |
| S10 | `--interpolation` → `img2_filter_t` | S11 |
| S11 | delay/loop fill | S12 |

### Key Design Decisions
- **Rename once, to the final name.** New identifiers get their final names when introduced (`img2_log`, `img2_decode_opts_t`, `img2_status_t`, `img2_frames_t`, `img2_font_face_*`, `img2_decoder_info_t`). Old identifiers are renamed exactly once, in S5, by the map in S5. Later steps change signatures only, never names. **[H1]** Where the public API needs a status-returning constructor under a name an internal function would otherwise take (`img2_image_create`, `img2_font_load`, `img2_font_load_fallback`), S5 gives the internal pointer-returning function a different final name (`img2_image_alloc`, `img2_font_find`, `img2_font_find_fallback`), and S7 introduces the public name fresh. This keeps `git log -S` history readable and the rename map stable.
- **The golden check is the oracle; ctest is the secondary net.** `golden.py` runs the CLI with a fixed `env -i`-style environment, a pinned `argv[0] = "imgcat2"`, no controlling TTY (new session) and a fixed cwd. Master is captured twice plus once with `OMP_NUM_THREADS=1`. Unstable cases are quarantined (exit code only) and reported, never dropped silently.
- **The CLI owns the log level.** `img2_set_log_level(IMG2_LOG_WARN)` is the first statement of `main()`. After `parse_arguments`/`validate_options` succeed, `if (!opts.silent) img2_set_log_level(IMG2_LOG_INFO);`. The library default is ERROR.
- **Status codes are negative and `IMG2_OK == 0`.** Existing internal callers that test `== 0`, `!= 0` or `< 0` on encoders and EXIF parsers keep working unchanged.
- **Internal decode function type from S6:** `img2_status_t (*)(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out)`. It is uniform with the public API, so S11 only sets fields.
- **CMake feature contract (S3).** Each `cmake/vendor/Img2Vendor<X>.cmake` ends with `img2_feature(<X> LIBS ... INCLUDE_DIRS ... ARCHIVES ... ARCHIVE_TARGETS ... SYSTEM_LIBS ...)`. Nothing else reads vendor-specific variables. `src/libimg2/CMakeLists.txt` consumes the generic lists in `IMG2_LINK_ORDER`, which reproduces today's order exactly.
- **The vendor configure environment is kept byte-for-byte.** During the vendor section, the old global `-static`, stack-size, `-O3 -g` and `NDEBUG` stay in effect (`img2_vendor_section_begin/end`). They are restored before our targets are declared. This removes the leak without changing what vendors see.
- **PIC (from S8).** `IMG2_POSITION_INDEPENDENT_CODE` (default ON) applies to `img2` and to every vendored target and ExternalProject merged into the fat archive, because Ubuntu consumers default to PIE. The CLI executable stays `-static` non-PIE. If PIC changes golden bits, the documented fallback in S8 applies.
- **From S9 the CLI and tests link the same fat archive that is installed** (IMPORTED target `img2_bundled`). Every later golden check therefore exercises the consumer artifact.
- **Header-only vendors are TU-local** (`STB_*_STATIC`, `NSVG_DEF static`, qoi renamed by macro). Their asserts map to `((void)0)`. **[SI2]** This is identical to today's behaviour, because the default `RelWithDebInfo` build defines `NDEBUG` (CMakeLists.txt:5-7) and the asserts already compile to nothing (documented).
- **Untrusted-input boundary (S6/S7, [SM1]/[SM2]).** The library treats every `buf`/`len` and every caller-built `img2_image_t`/`img2_frames_t` as untrusted. Checks run at the public entry. They return `IMG2_E_ARG`/`IMG2_E_LIMIT` and log only at `IMG2_LOG_DEBUG`, so CLI stderr does not change even if a check ever fires on a CLI path.
- **Log formats are literals ([SM4]).** `img2_log` keeps `__attribute__((format(printf, 2, 3)))`. Our own sources build with `-Wformat=2 -Wformat-security -Werror=format-security`. Decoder-supplied strings (EXIF/XMP/SVG text, libpng/libtiff messages, `stbi_failure_reason()`) are only ever passed as `"%s"` arguments. The default stderr sink does not sanitise (byte identity). `docs/libimg2.md` warns that messages may contain terminal control bytes from untrusted files, so consumers that print them to a terminal should install a callback.

### Risk Mitigation
- Link-order regressions when links move to `img2` → explicit `IMG2_LINK_ORDER` reproducing CMakeLists.txt:1669-1774, including the trailing raw archive paths. S3 cross-checks `build/CMakeFiles/imgcat2.dir/link.txt` against the master copy saved in S0.
- The rename touches string literals and changes stderr → `apply_rename.py` tokenises C and never edits string or char literals. The golden check catches strays.
- Silent loss of a codec through a macro rename or scoping → golden decode cases for every codec, the `--version` decoder list case, and a post-step grep for old macro names.
- Fan-out agents breaking each other's builds → lanes never run `cmake --build`. They use `tu_check.py`. Only the integrator builds.
- Nondeterministic encoders (kvazaar/HEIF, JXL threads, OpenMP in jpegqs/LibRaw) → triple capture and `unstable.txt`.
- Assert references appear only in non-NDEBUG builds → the `-UNDEBUG` object variant is checked by `img2_symbols`.
- PIC changes codegen → `-fno-semantic-interposition`; fallback option and `-no-pie` interface flag (S8).
- Duplicate-symbol winners change when archives are merged → `dupsyms.sh` plus a link-map attribution check in S9. MRI `ADDLIB` order equals today's link order.
- Missing transitive deps (OpenMP runtime, Threads, stdc++) once tests link dynamically → fixed in the library's usage requirements and config, never in tests.
- Removing global `add_definitions(-DHAVE_*)` (S3) or moving `-O3 -g`/`NDEBUG`/PIC into the vendor section (S8) silently changes what vendor TUs see (**[M2]**). For example, `-DHAVE_JPEGQS` (CMakeLists.txt:381) is directory-scoped and precedes the tiff/heif/raw/jxl/openjpeg blocks. → `cc_vendor_diff.py` compares every vendor entry of `compile_commands.json` against the master copy saved in S0. It runs as a gate in S3 and S8 (ExternalProjects such as libjpeg-turbo/aom are not in `compile_commands.json`; their flags come only from their explicit `-D` arguments, which S8 checks by reading the module).
- An overnight agent stops mid-step → the commit plus `git tag -f img2/sNN` is the last action of a step. Resume by checking tags. S2 must never end a session between its two commits.

## 6. Step-by-Step Plan

### Global rules (apply to every step)
1. **Branch**: `git switch -c feature/split a50e93d` (S0). Every commit goes on `feature/split`. **Never push.** Local tags `img2/sNN` mark green steps.
2. **One commit per step** (S2 = S2a + S2b; S2a is the content-free `git mv` commit). Commit only after the step's gate passes. Then run `git tag -f img2/s<NN>` (S2 tags only after S2b).
3. **Every build** runs with `export TMPDIR=/mnt/nvme/imaginarium/imgcat2/.claude/tmp`. **Never write to `/tmp`.** Scratch scripts, logs and probes go under `.claude/tmp/img2-split/` and `.claude/tmp/img2-golden/`. Reusable guards are committed under `cmake/scripts/`.
4. **Never modify `vendor/`** (no submodule commands, no `--recurse-submodules`). Leave the pre-existing `M vendor/nanosvg`, the untracked `src/imgcat2/compat/` and `sbre-structure-visual.svg` alone. Never `git clean`. Never `git add -A`/`git add .`.
5. **Staging**: `git add -u -- CMakeLists.txt cmake src/libimg2 src/imgcat2 src/tests include tools docs README.md .gitignore`, then `git add <new files>` explicitly. Then confirm that `git diff --cached --name-only | grep -E '^(vendor/|src/imgcat2/compat/|sbre-)'` prints nothing.
6. **Gate** = `gate.sh <step>` (build + full ctest without the `expensive` label + golden check + hygiene). Per-step extra checks are listed under each step.
7. **Rollback** (every step): `git reset --hard img2/s<previous>` (untracked compat/ and svg survive), then `rm -rf build` only if the step touched CMake cache semantics, and re-run `gate.sh`. Never use `git clean`, never `git checkout -- vendor`.
8. Progress journal: `.claude/tmp/img2-split/progress.md` records, per step, the gate log path, ctest count, golden result, commit hash and tag.

### Execution notes for agents
- **Strictly sequential step order.** A step starts only after the previous step is committed and tagged. To resume, read `git tag -l 'img2/s*'` and `git log -1 --format=%s` and compare them with the commit messages below.
- **Only one agent (the integrator) runs `cmake`, `cmake --build`, `ctest`, `gate.sh` and `git commit`** in `build/`. Lanes check single TUs with `tu_check.py` and never write into `build/`.
- Parallel lanes, each owning a disjoint file set:

| Step | Lane | Files owned |
|---|---|---|
| S0 | A: master build, `golden.py`, `gate.sh`, `tu_check.py`, `dupsyms.sh`, `cc_vendor_diff.py` | `.claude/tmp/img2-golden/{golden.py,imgcat2.master,master-link.txt,master-compile_commands.json,master-configure.log}`, `.claude/tmp/img2-split/*` |
| S0 | B: `gen_inputs.py`, `probe_anim.py` and the input set | `.claude/tmp/img2-golden/{gen_inputs.py,probe_anim.py,anim-facts.tsv,inputs/**}` (A runs `record` after B finishes) |
| S6 (after the integrator wrote the core part) | L1 | `src/libimg2/decoders/decoder_{png,jpeg,gif,stb,pnm,qoi,ico}.c`, `src/tests/unit/test_decoder_{png,jpeg,stb,pnm}.c`, `src/tests/decoder_internal.h` |
| S6 | L2 | `src/libimg2/decoders/decoder_{webp,heif,avif,tiff,raw}.c`, `src/tests/integration/test_{png,webp,heif,jxl,raw,tiff,jpeg}_exif_integration.c` (L1 also owns the libpng handler routing in `decoder_png.c`; L2 owns the libtiff handler routing in `decoder_tiff.c` and the `decoder_raw.c:26` extern cleanup, **[M1b]/[L6]**) |
| S6 | L3 | `src/libimg2/decoders/decoder_{jxl,nanosvg,resvg,yuv}.c`, `src/libimg2/encoders/encoder_{jpeg,png,heif,webp,jxl}.c`, `src/tests/unit/test_encoder_{jpeg,png,webp,heif,jxl}.c` |
| S9 | A (integrator): CMake | `cmake/Img2FatArchive.cmake`, `cmake/Img2Install.cmake`, `cmake/img2Config.cmake.in`, `cmake/scripts/img2_fat_archive.cmake`, `cmake/scripts/img2_consumer_smoke.cmake`, `cmake/vendor/*.cmake`, `src/libimg2/CMakeLists.txt`, `src/imgcat2/CMakeLists.txt`, `src/tests/CMakeLists.txt`, `src/tests/consumer/*` |
| S9 | B: docs | `docs/libimg2.md`, `README.md` (no builds) |
| S11 | L1 / L2 / L3 | L1: `decoder_gif.c`, `decoder_png.c`; L2: `decoder_webp.c`; L3: `decoder_heif.c`, `decoder_avif.c`, `decoder_jxl.c`; the integrator owns `src/tests/**` |

- Lanes never touch `decoder.{c,h}`, `encoder.{c,h}`, `image.{c,h}`, `status.*`, `log.*`, `init.c`, `api.c`, `src/imgcat2/**`, or any `CMakeLists.txt`/`cmake/**` unless the table assigns it. A lane that needs such a change reports it to the integrator.
- S1, S2, S3, S4, S5, S7, S8 and S10 are single-agent steps.

### Common gate `.claude/tmp/img2-split/gate.sh` (created in S0)
```bash
#!/usr/bin/env bash
# usage: gate.sh <step-id> [extra cmake -D args...]
# env: GOLDEN_ARGS_FILE=<file> (S10 only): one golden.py argument per line, e.g. "--allow" then "^meta:--help$".
#      An args file plus a bash array avoids word splitting and globbing of regexes such as '^meta:-i bogus ' [M5].
set -euo pipefail
R=/mnt/nvme/imaginarium/imgcat2
cd "$R"
export TMPDIR="$R/.claude/tmp"
step="${1:?step id}"; shift || true
L="$R/.claude/tmp/img2-split/logs"; mkdir -p "$L"
log="$L/${step}.log"
golden_args=()
if [ -n "${GOLDEN_ARGS_FILE:-}" ]; then
	mapfile -t golden_args < "$GOLDEN_ARGS_FILE"
fi
hygiene() {
	# vendor/ must be unchanged except the user's pre-existing nanosvg modification
	local v; v=$(git status --porcelain -- vendor | grep -v '^ M vendor/nanosvg$' || true)
	[ -z "$v" ] || { echo "HYGIENE: vendor changed: $v"; return 1; }
	git status --porcelain --untracked-files=all -- src/imgcat2/compat | grep -q '^??' \
		|| { echo "HYGIENE: src/imgcat2/compat no longer untracked"; return 1; }
	echo "hygiene: ok"
}
{
	cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo "$@"   # [Q1] explicit; never inherit a stale cache value
	grep -qx 'CMAKE_BUILD_TYPE:STRING=RelWithDebInfo' build/CMakeCache.txt \
		|| { echo "GATE: CMAKE_BUILD_TYPE is not RelWithDebInfo"; exit 1; }
	cmake --build build -j32
	ctest --test-dir build -j8 --output-on-failure -LE expensive
	python3 -I .claude/tmp/img2-golden/golden.py check build/imgcat2 "${golden_args[@]}"
	hygiene
} 2>&1 | tee "$log"
```
The gate passes when the script exits 0 and the log contains `golden: 0 mismatches` (or, in S10, only `ALLOWED` lines) and `hygiene: ok`.

`"${golden_args[@]}"` with an empty array is safe under `set -u` in bash >= 4.4 (the host has 5.x).

`.claude/tmp/img2-split/cc_vendor_diff.py <ref.json> <new.json> [--allow-drop-define <MACRO>...] [--allow-add-flag <flag>...] [--archive-members <file>]` (**[M2]**, created in S0): loads both `compile_commands.json` files and keeps the entries whose `file` lies under `vendor/` (our sources moved, vendor paths did not) **or under `<build>/_deps/*-src/`** (**[P5]**: the patched libpng copy is compiled from `${CMAKE_BINARY_DIR}/_deps/libpng-src`, CMakeLists.txt:447/:499; such paths are normalised to `_deps/...` relative to each database's build dir, so S0 and later builds compare like for like). With `--archive-members <file>` (**[R1]**, used from S8; matching rule per **[Q4]**), only entries whose object path `normpath(join(entry.directory, -o value))` appears in the file are compared; the rest are counted as `SKIPPED`. Basenames are never used for matching, because many vendors have same-named objects. The file lists absolute object paths, collected from the archive command in each vendored archive target's `CMakeFiles/<tgt>.dir/link.txt` (the `ar qc <lib> <objs...>` line, objects resolved against that target's build directory). `--allow-drop-include <dir>... --only-targets <regex>` (**[Q2]**) permits removal of exactly the listed `-I` directories, and only for entries whose object path matches `CMakeFiles/(<regex>)\.dir/`; anywhere else, a dropped `-I` is still a failure. Per file it tokenises `command` (or `arguments`) with `shlex`, drops `-o <obj>` and the source path, and compares the remaining token sequence in order. It prints `VENDOR-FLAGS <file>: -<removed> +<added>` for every difference and exits 1 if a difference is not covered by an `--allow-*` option. It also reports files present in only one of the two databases. It writes nothing.

`.claude/tmp/img2-split/tu_check.py <src-file>`: loads `build/compile_commands.json` and finds the entry whose `file` resolves to the argument. It removes the `-o <obj>` pair, appends `-fsyntax-only`, runs the command in the entry's `directory`, and exits with the compiler's status. It writes nothing to `build/`.

---

### Step S0: Branch, golden reference, gate and helper scripts
**Files**: create `.claude/tmp/img2-golden/{golden.py,gen_inputs.py,probe_anim.py,imgcat2.master,master-link.txt,master-compile_commands.json,master-configure.log,golden.tsv,anim-facts.tsv,unstable.txt,omp-sensitive.txt}`, `.claude/tmp/img2-golden/{inputs,ref,home,tmp,run}/`, `.claude/tmp/img2-split/{gate.sh,tu_check.py,dupsyms.sh,cc_vendor_diff.py,progress.md}`. No tracked files change. Anchors: `src/imgcat2/core/cli.c:158-190` (option table used for cases), `src/imgcat2/terminal/terminal_unix.c:27-127` (TTY probing; reason for the new session), `src/imgcat2/main.c:106-111` (forced ANSI without pixel size).
**Dependencies**: none
**Description**:
1. `git switch -c feature/split a50e93d`; then `git rev-parse HEAD` must print `a50e93d...`. Untracked files and `M vendor/nanosvg` stay as they are. `git tag img2/s00`.
2. Fresh master build. **[Q1]** The existing `build/` holds a cached `CMAKE_BUILD_TYPE:STRING=Release` (build/CMakeCache.txt:91), and CMakeLists.txt:5-7 only sets the default when the variable is empty, so reusing it would record a Release reference. Therefore first `rm -rf build`, then `export TMPDIR=...; cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo 2>&1 | tee .claude/tmp/img2-golden/master-configure.log`, then assert `grep -qx 'CMAKE_BUILD_TYPE:STRING=RelWithDebInfo' build/CMakeCache.txt` (stop on failure), then `cmake --build build -j32 && ctest --test-dir build -j8 --output-on-failure`. `gate.sh` passes the same `-DCMAKE_BUILD_TYPE=RelWithDebInfo` and asserts the cache on every step. Record the ctest count in `progress.md`. Copy `build/imgcat2` → `.claude/tmp/img2-golden/imgcat2.master`, `build/CMakeFiles/imgcat2.dir/link.txt` → `master-link.txt`, and `build/compile_commands.json` → `master-compile_commands.json` (**[M2]** reference for `cc_vendor_diff.py`). `master-configure.log` is the **[L5]** reference for configure warnings. **[Q2]** Also copy the object directories `build/CMakeFiles/{jpegqs_base,jpegqs_sse2,jpegqs_avx2,jpegqs_avx512,jpegqs_dispatch,jpegqs,vendor_giflib,vendor_lcms2,vendor_kvazaar,vendor_libraw}.dir/` (those that exist in this configuration) to `.claude/tmp/img2-golden/master-vendor-objs/`. They are the reference for the S3 object-identity proof.
3. `gen_inputs.py` (run with `python3 -I`; stdlib only; deterministic; writes only under `.claude/tmp/img2-golden/inputs/`):
   - generated into `inputs/gen/`:
     - `rgb.ppm` (P6 64x48 gradient), `gray16.pgm` (P5 maxval 65535), `bits.pbm` (P4), `rgba.pam` (P7 RGB_ALPHA);
     - `rgb24.bmp` (BI_RGB 24-bit bottom-up, 4-byte row padding), `rgba.tga` (type 2, 32-bit);
     - `rgba.qoi` (37x23; every pixel `QOI_OP_RGBA` + end marker);
     - `icon.ico` (1 entry, 32x32 32bpp BMP DIB + AND mask) and `icon_png.ico` (1 entry, PNG payload via zlib + struct);
     - `flat.hdr` (`#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 4 +X 4\n` + 16 flat RGBE pixels);
     - `shapes.svg` (rect, circle, linear gradient, a `<style>` class; **no text**) and `text.svg` (`<text font-family="DejaVu Sans Mono" font-size="24" text-anchor="middle">Hello img2</text>` plus bold and italic variants);
     - `i420_64x48.yuv` (gradient planes);
     - `garbage.bin` (512 bytes from an LCG with seed 12345), `empty.bin` (0 bytes);
     - `truncated.png` (first 200 bytes of `src/tests/data/png/goldhill.png`), `truncated.jpg` (first 300 bytes of `src/tests/data/jpg/DSCN0010.jpg`).
   - copies (`shutil.copy2`, keeping relative names) into `inputs/`: all of `src/tests/data/**`, `src/tests/fixtures/svg/*.svg`, `.claude/tmp/vendor-smoke/{smoke.jpg,smoke.png,smoke.webp,smoke_anim.gif,smoke_anim.png,smoke_anim.webp,smoke_tiff_jpeg.tif,smoke_tiff_deflate.tif,smoke_tiff_lzw.tif}`, `docs/example.gif`.
4. `golden.py` modes: `record <binary>`, `check <binary> [--allow <id-regex>]...`, `rebase <binary>`.
   - **Environment (env -i style):** the subprocess `env` is exactly `{PATH:/usr/bin:/bin, HOME:<G>/home (empty dir), LANG:C, LC_ALL:C, TERM:xterm-256color, TMPDIR:<G>/tmp}`. Nothing is inherited.
   - **Process:** `subprocess.run(["imgcat2", *args], executable=<binary>, stdin=<file or DEVNULL>, capture_output=True, start_new_session=True, timeout=300, cwd=<G>/run)`. Pinning `argv[0]` matters because `"Try '%s --help'"` (cli.c) prints it. `<G>/run/out/` is wiped before each case.
   - **Case ids (**[M5]**, normative; S10's `--allow` regexes depend on them).** Every case id is `<kind>:<args>`, where `<args>` is the exact argv after `imgcat2`, joined by single spaces. Input paths in it are relative to `inputs/`, and output paths are written as `out/o.<ext>`. Per-file kinds use only the path: `info:<S>`, `json:<S>`, `ansi:<S>`, `png:<S>`, `full:<S>`. The fixed kinds, with examples:

     | kind | example id |
     |---|---|
     | `enc` | `enc:-j 90 -o out/o.jpg png/goldhill.png` |
     | `verbose` | `verbose:-v -w 64 png/goldhill.png` |
     | `frames` | `frames:-n 1 -p 1 -o out/o.png smoke_anim.gif` |
     | `yuv` | `yuv:--yuv i420:64:48 -p 1 -o out/o.png gen/i420_64x48.yuv` |
     | `size` | `size:-r -w 40 -H 20 png/hell.png` |
     | `interp` | `interp:-i nearest -w 64 png/goldhill.png` |
     | `stdin` | `stdin:- -w 32` |
     | `meta` | `meta:--help`, `meta:--version`, `meta:--fonts`, `meta:-i bogus png/gray4.png` |

     `golden.py` writes the id list to `golden.tsv`. S0 verification greps it for `^meta:--help$`, `^meta:-i bogus png/gray4.png$` and `^interp:-i (nearest|cubic|bilinear|lanczos) `, and fails if one is missing.
   - **Case matrix** (S = every file under `inputs/`, sorted by relative path):
     - `info:<S>` → `--info S`; `json:<S>` → `--info --json --exif-detailed S` (files < 5 MB).
     - `ansi:<S>` → `-w 64 S` (files < 5 MB).
     - `png:<S>` → `-w 256 -p 1 -o out/o.png S`; `full:<S>` → `-p 1 -o out/o.png S` (files < 2 MB).
     - encoders on `inputs/png/goldhill.png`: `-j 90 -o out/o.jpg`, `-j 85` (stdout), `-p 0 -o out/o.png`, `-p 9 -o out/o.png`, `--webp 80 -o out/o.webp`, `--webp 100 -o ...`, `--heif 80 -o out/o.heif`, `--jxl 80 -o out/o.jxl`, `--jxl 100 -o ...`; plus `--webp 80`/`--jxl 80` on `png/pal4_trns_interlaced.png` (alpha).
     - verbose: `-v -w 64 png/goldhill.png`, `-v -p 6 -o out/o.png jpg/Canon_40D.jpg`, `-v -w 32 gen/i420_64x48.yuv --yuv i420:64:48`, `-v -p 1 -o out/o.png gen/text.svg`, `-v --info --json heif/sample_exif.heif`, `-v --jxl 80 -o out/o.jxl png/goldhill.png`.
     - frames: `-n 1 -p 1 -o out/o.png` on `smoke_anim.{gif,png,webp}`; `-n 9 -p 1 -o out/o.png smoke_anim.gif` (error path).
     - YUV: `--yuv i420:64:48 -p 1 -o out/o.png gen/i420_64x48.yuv`; `--yuv i420:0:0 -p 1 -o out/o.png gen/i420_64x48.yuv` (error).
     - sizing: `-r -w 40 -H 20 png/hell.png`, `-f -w 48 -H 40 jpg/Canon_40D.jpg`.
     - interpolation (identical until S10): `-i nearest -w 64 png/goldhill.png`, `-i cubic -p 1 -w 128 -o out/o.png jpg/DSCN0010.jpg`, `-i bilinear -w 64 png/goldhill.png`, `-i lanczos -w 64 png/goldhill.png`.
     - stdin: `- -w 32` with stdin = `png/rgb8_srgb.png`.
     - meta: `--version`, `--help`, `--fonts`, `nonexistent.png`, `-i bogus png/gray4.png`, `--yuv bad:1:1 x`, `--json` without `--info`.
   - **Per case**, store under `ref/<case-hash>/`: `rc`, raw `stderr` bytes, `stdout.md5` (raw stdout kept only if < 1 MiB), and `files.md5` (sorted md5 of every file in `out/`). Write a manifest `golden.tsv` (`id\trc\tstdout_md5\tstderr_md5\tfiles_md5`).
   - **record**: run the matrix three times (pass 1 → `ref/`, pass 2 → `ref2/`, pass 3 → `ref-omp1/` with `OMP_NUM_THREADS=1` added). Ids that differ between `ref` and `ref2` go to `unstable.txt` (compared by `rc` only). Ids that differ only vs `ref-omp1` go to `omp-sensitive.txt` (informational). Always record with `imgcat2.master`.
   - **check**: run once into `last-check/`. Compare `rc`, `stdout.md5`, stderr **bytes** and `files.md5` against `ref/`. For each mismatch print `MISMATCH <id> <field>` and a `difflib` unified diff of stderr (first 20 lines). `--allow` turns matching mismatches into `ALLOWED <id>`. An `--allow` regex that matches no id in the matrix is itself an error (`STALE-ALLOW <regex>`, counted as a mismatch) (**[M5]**). A case id present in the current matrix but absent from `ref/` prints `NEW <id>` and is not a mismatch (used by S10). The last line is `golden: N mismatches`; exit 1 if N > 0.
   - **rebase `<binary>`**: re-records `ref/` from the given binary (used only after S10). The old reference is kept as `ref-v1/`.
4a. **[M6] Animation fixture facts.** `probe_anim.py` (stdlib only, `python3 -I`, read-only) parses `.claude/tmp/vendor-smoke/smoke_anim.{gif,png,webp}` and writes `.claude/tmp/img2-golden/anim-facts.tsv` (`file\tframes\tdelays_ms\tloop_count`):
   - GIF: count image descriptors (`0x2C`); delay = Graphic Control Extension `DelayTime * 10`; loop = the `NETSCAPE2.0` application extension's 16-bit count, or `1` when the extension is absent (the S11 convention);
   - APNG: `acTL.num_frames`/`num_plays`; per `fcTL`: `delay_num`/`delay_den` (den 0 → 100) converted to ms with the S11 formula;
   - WebP: `ANIM.loop_count`, and the count and `duration` of the `ANMF` chunks.

   The S11 test asserts exactly these recorded values. The plan's expectation is `frames=3`, every delay `100`, `loop_count=0`. If the facts differ, S11 uses the recorded facts, and the S0 report flags the difference.
5. `dupsyms.sh <archive>...`: `nm -A --defined-only -g` on each archive. It collects strong definitions (`T D B R`), prints symbols defined in more than one archive as `symbol archiveA(member) archiveB(member)`, and exits 0 (report only).
6. `python3 -I .claude/tmp/img2-golden/golden.py record .claude/tmp/img2-golden/imgcat2.master`, then `gate.sh S0` (which runs `check build/imgcat2`). It must report 0 mismatches, which proves the harness is deterministic.

**Pattern to follow**: P13 (TMPDIR discipline); global rule 3.
**Verification**: `golden: 0 mismatches`; `wc -l golden.tsv` > 200; the case-id greps from item 4 pass (**[M5]**); `anim-facts.tsv` has 3 rows (**[M6]**); `cc_vendor_diff.py master-compile_commands.json build/compile_commands.json` exits 0 (self-check); `unstable.txt` and `omp-sensitive.txt` are listed in the step report (expected: empty, or only HEIF/JXL encode cases).
**Commit**: none (scratch only). The tag `img2/s00` points at a50e93d.
**Rollback**: `git switch master && git branch -D feature/split && git tag -d img2/s00`.

---

### Step S1: CMake modularisation, 3.21, `PROJECT_*` paths, option rename, `project(img2)` (pure refactor)
**Files**:
- modify `CMakeLists.txt:1-1919` (it becomes the top-level orchestrator);
- create `cmake/Img2Options.cmake` (old :16-41), `cmake/Img2Platform.cmake` (old :43-85, :121-133), `cmake/Img2Tooling.cmake` (old :87-112 tidy/format detection and :1779-1793 format target), `cmake/Img2VendorHelpers.cmake` (old :163-204), `cmake/Img2LinkOrder.cmake` (old :1455-1477 trailing libs), `cmake/Img2ProjectSettings.cmake` (old :1479-1496 warnings/tidy/gcc_s), `cmake/Img2Summary.cmake` (old :1801-1918);
- create `cmake/vendor/Img2Vendor{Zlib,Jpeg,Jpegqs,Png,Gif,Webp,Lcms2,Brotli,Heif,Tiff,Raw,Jxl,HeaderOnly,Resvg}.cmake`;
- create the directory `cmake/scripts/` (filled later); `git rm cmake/FindCompressionLib.cmake` (unused);
- modify `src/tests/CMakeLists.txt:6,106,506,529,609,645,663,681,699,717`, `README.md:274,835` and, doc-only, `cmake/vendored-find/README.md:14` (`USE_SYSTEM_JPEG=ON` → `IMG2_USE_SYSTEM_JPEG=ON`, **[L2]**; the `Find*.cmake` files there stay untouched).

**Dependencies**: S0
**Description**:
1. The top-level `CMakeLists.txt` after S1:
```cmake
cmake_minimum_required(VERSION 3.21)
project(img2 VERSION 1.0.0 LANGUAGES C)
# old :4-14 kept here verbatim (build type default, compile_commands, C11)
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")   # never add cmake/vendored-find here
include(Img2Options)          # old :16-41 (renamed options)
include(Img2Platform)         # old :43-85 + :121-133 (static/libc++ flags, stack size)
include(Img2Tooling)          # old :87-112 (tidy/format detection; format target function defined, created at the end)
add_compile_options(-O3 -g)   # old :118, same position (moved into vendor section/targets in S8)
set(IMGCAT2_WARNING_FLAGS ...) # old :119, verbatim
find_package(PkgConfig REQUIRED)  # old :136
include(Img2VendorHelpers)    # old :163-204; [Q3] moved above zlib: only macro/function definitions, no statements
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorZlib.cmake")    # old :138-161
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorJpeg.cmake")    # old :206-276
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorJpegqs.cmake")  # old :278-384
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorPng.cmake")     # old :386-524
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorGif.cmake")     # old :526-567
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorWebp.cmake")    # old :569-617
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorLcms2.cmake")   # old :619-646
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorBrotli.cmake")  # old :648-660
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorHeif.cmake")    # old :662-1039 (vendored + host)
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorTiff.cmake")    # old :1041-1144
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorRaw.cmake")     # old :1146-1252
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorJxl.cmake")     # old :1254-1403
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorHeaderOnly.cmake") # old :1405-1422
include("${PROJECT_SOURCE_DIR}/cmake/vendor/Img2VendorResvg.cmake")   # old :1424-1453
include(Img2LinkOrder)        # old :1455-1477
include(Img2ProjectSettings)  # old :1479-1496
# old :1498-1777 stay here verbatim in S1 (global includes, sources, embed_fonts, targets, install)
img2_add_format_target()      # old :1779-1793 body, now a function in Img2Tooling
if(IMG2_BUILD_TESTS)          # old :1795-1799
	enable_testing()
	add_subdirectory(src/tests)
endif()
include(Img2Summary)          # old :1801-1918
if(NOT PROJECT_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	message(FATAL_ERROR "img2: PROJECT_SOURCE_DIR clobbered by an include()d module")
endif()
```
   Vendor modules are included by explicit path, so modules in `cmake/vendored-find/` can never be picked up by name. Move each line range **verbatim** (cut and paste in order). Each new module starts with a 3-line header comment: purpose, inputs read, outputs defined. In `Img2Tooling.cmake` the format glob gains `list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX "/src/imgcat2/compat/")`. That changes only the developer `format` target, never the build.
2. In every moved line and in `src/tests/CMakeLists.txt`, replace `${CMAKE_SOURCE_DIR}` → `${PROJECT_SOURCE_DIR}` and `${CMAKE_BINARY_DIR}` → `${PROJECT_BINARY_DIR}`. That is all 58 sites listed in the recon digest, including `_deps` and ExternalProject prefixes; the vendored-find module path setup (old :868-872, :1317-1321) becomes `${PROJECT_SOURCE_DIR}/cmake/vendored-find`. Modules are `include()`d at top-level scope, so `CMAKE_CURRENT_*` equal the top-level values and relative `add_subdirectory(vendor/...)` calls stay valid.
3. Option rename (word-boundary regex, exact token list) in `CMakeLists.txt`, `cmake/*.cmake`, `cmake/vendor/*.cmake`, `src/tests/CMakeLists.txt`, `README.md`:
   - `ENABLE_{GIF,WEBP,HEIF,TIFF,RAW,JXL,RESVG,EXIF_READER,JPEGQS,CLANG_TIDY,CLANG_FORMAT,STATIC_LIBCXX}` → `IMG2_ENABLE_*`;
   - `USE_SYSTEM_{JPEG,PNG,GIF,WEBP,TIFF,LCMS2,RAW,HEIF,JXL}` → `IMG2_USE_SYSTEM_*`;
   - `option(BUILD_TESTING "Build tests" ON)` (:25) → `option(IMG2_BUILD_TESTS "Build img2 tests" ${PROJECT_IS_TOP_LEVEL})`; the summary line (:1908) prints `IMG2_BUILD_TESTS`.

   **Do not touch** vendor-facing names: `ENABLE_SHARED`, `ENABLE_STATIC` (:236-237), `ENABLE_APPS|DOCS|EXAMPLES|TESTS|TESTDATA|TOOLS|NASM` (:769-775), `ENABLE_PLUGIN_LOADING`, `ENABLE_EXPERIMENTAL_FEATURES`, `BUILD_TESTING` inside `imgcat2_vendor_begin/end` (:170-179), the libde265 knobs `ENABLE_SDL|DECODER|ENCODER|SHERLOCK265|INTERNAL_DEVELOPMENT_TOOLS` (:680-684), or anything in `cmake/vendored-find/` except the doc-only README line above.
4. README: `-DENABLE_RESVG=OFF` (:274) → `-DIMG2_ENABLE_RESVG=OFF`; `-DENABLE_WEBP=ON` (:835) → `-DIMG2_ENABLE_WEBP=ON`.
5. `project()` is renamed to `img2`. No code uses `${PROJECT_NAME}` (verified). The executable name stays `imgcat2`.

**Pattern to follow**: P14 (helper macros unchanged); verbatim block moves only.
**Verification**:
- `rm -rf build` (the cache holds old option names), then `gate.sh S1`.
- **[H2]** Check only the exact renamed tokens. Vendor cache entries such as `ENABLE_SDL`, `ENABLE_PLUGIN_LOADING`, `ENABLE_EXPERIMENTAL_FEATURES` and `ENABLE_AVX2`/`ENABLE_SIMD`/`ENABLE_SHERLOCK265`/`ENABLE_COVERAGE` legitimately remain:
  `cmake -S . -B build -LA | grep -E '^(ENABLE_(GIF|WEBP|HEIF|TIFF|RAW|JXL|RESVG|EXIF_READER|JPEGQS|CLANG_TIDY|CLANG_FORMAT|STATIC_LIBCXX)|USE_SYSTEM_(JPEG|PNG|GIF|WEBP|TIFF|LCMS2|RAW|HEIF|JXL)):'` → empty;
  `grep -rnE 'option\(BUILD_TESTING' CMakeLists.txt cmake/*.cmake cmake/vendor/*.cmake` → empty (the vendor-facing `BUILD_TESTING` normal variable inside `imgcat2_vendor_begin/end` stays);
  `cmake -S . -B build -LA | grep -E '^IMG2_(ENABLE_|USE_SYSTEM_|BUILD_TESTS)'` lists all 22 renamed options.
- **[L5]** Raising the minimum from 3.15 to 3.21 flips about 20 policies (e.g. CMP0114, CMP0111, CMP0099). Tee the S1 configure output to `.claude/tmp/img2-split/logs/S1-configure.log`; `grep -E 'CMake (Deprecation )?Warning|Policy CMP' .claude/tmp/img2-split/logs/S1-configure.log` must show no warning that is absent from `master-configure.log`. Any new one is fixed or explained in `progress.md`. CMP0099 must be NEW (it is, with 3.21), so the jpegqs `INTERFACE` `-fopenmp` link option (CMakeLists.txt:358) keeps propagating through the PRIVATE link from S3 on.
- `diff <(sed 's/[[:space:]]\+/ /g' .claude/tmp/img2-golden/master-link.txt) <(sed 's/[[:space:]]\+/ /g' build/CMakeFiles/imgcat2.dir/link.txt)` → identical (same link order).
- Golden: 0 mismatches.

**Commit**: `build(cmake): split CMakeLists into cmake/ modules, require 3.21, rename options to IMG2_*`
**Rollback**: global rule 7 to `img2/s00`, then `rm -rf build`.

---

### Step S2: Source tree move (two commits) + generated fonts in the build dir
**Files**:
- `git mv` per the move map (also saved as `.claude/tmp/img2-split/move-map.tsv`):

| From | To |
|---|---|
| `src/imgcat2/core/image.{c,h}` | `src/libimg2/core/image.{c,h}` |
| `src/imgcat2/decoders/*` (23 tracked files) | `src/libimg2/decoders/` |
| `src/imgcat2/encoders/*` (7 files) | `src/libimg2/encoders/` |
| `src/imgcat2/metadata/{exif_reader,xml_parser}.{c,h}` | `src/libimg2/metadata/` |
| `src/imgcat2/text/font_manager.{c,h}` | `src/libimg2/text/` |
| `src/imgcat2/font/*` (4 `.ttf` + `embedded_fonts.{c,h}`) | `src/libimg2/font/` |
| `src/imgcat2/core/{cli,pipeline,metadata,base64}.{c,h}` | `src/imgcat2/{cli,pipeline,metadata,base64}.{c,h}` |

- `main.c`, `ansi/`, `terminal/`, `src/tests/` stay. `src/imgcat2/compat/` is untracked; `git mv` never touches it and nobody moves it by hand.
- S2b modifies: includes in every `.c`/`.h` under `src/libimg2`, `src/imgcat2` (not `compat/`) and `src/tests`; the top-level `CMakeLists.txt` source list (old :1541-1638), include list (old :1498-1539) and embed_fonts block (old :1640-1658); `src/tests/CMakeLists.txt:443,460-461,494,517,538,558,578,598,757-759`; `tools/embed_font.py:29` (usage text). It deletes `src/libimg2/font/embedded_fonts.{c,h}` from git.

**Dependencies**: S1
**Description**:
- **Commit S2a (content-free).** Run `mkdir -p src/libimg2/{core,decoders,encoders,metadata,text,font}` and the `git mv`s. `git status` must show only renames. Commit. The tree does not build at this commit; continue immediately and never end a session here.
- **Commit S2b (fixes).**
  1. `.claude/tmp/img2-split/fix_includes.py` takes `move-map.tsv` plus `git ls-files src`. For each `.c`/`.h` file F (new path; old path from the reverse map) and each line matching `^\s*#\s*include\s+"([^"]+)"`, it resolves `old_target = normpath(dirname(old(F)) / X)`. If `old_target` is in the map or exists on master (`git cat-file -e a50e93d:<path>`), it rewrites X to `relpath(map.get(old_target, old_target), dirname(new(F)))`. Otherwise it leaves the line (it resolves via `-I`). The script prints a dry-run diff first, then applies it.
  2. Manual edits:
     - `src/libimg2/text/font_manager.c:45` → `#include "embedded_fonts.h"`; `:48` → `#include "stb_truetype.h"`.
     - `src/libimg2/decoders/decoder_qoi.c:22` → `#include "qoi.h"`.
     - `src/libimg2/decoders/nanosvg.h:201` → `#include "../text/font_manager.h"`; `:202` → `#include "stb_truetype.h"`.
     - The temporary shim `#include "../../imgcat2/cli.h"` in `src/libimg2/decoders/decoder.h:16` and `src/libimg2/encoders/encoder.h:16` (produced by the script) is removed in S4.
  3. Apply the move map to path strings in the top-level `CMakeLists.txt` and `src/tests/CMakeLists.txt` (`../imgcat2/metadata/xml_parser.c` → `../libimg2/metadata/xml_parser.c`; `../imgcat2/core/metadata.c` → `../imgcat2/metadata.c`).
  4. Fonts into the binary dir. First run `python3 -I tools/embed_font.py src/libimg2/font .claude/tmp/img2-split/fontcheck/embedded_fonts` and `cmp` both outputs against the committed files. They must be identical; otherwise STOP and report. Then replace the old custom command with:
```cmake
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(IMG2_GENERATED_DIR "${PROJECT_BINARY_DIR}/generated")
set(_img2_font_dir "${PROJECT_SOURCE_DIR}/src/libimg2/font")
add_custom_command(
	OUTPUT "${IMG2_GENERATED_DIR}/embedded_fonts.c" "${IMG2_GENERATED_DIR}/embedded_fonts.h"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${IMG2_GENERATED_DIR}"
	COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/embed_font.py"
	        "${_img2_font_dir}" "${IMG2_GENERATED_DIR}/embedded_fonts"
	DEPENDS "${_img2_font_dir}/dejavu-sans-mono.book.ttf" "${_img2_font_dir}/dejavu-sans-mono.bold.ttf"
	        "${_img2_font_dir}/dejavu-sans-mono.oblique.ttf" "${_img2_font_dir}/dejavu-sans-mono.bold-oblique.ttf"
	        "${PROJECT_SOURCE_DIR}/tools/embed_font.py"
	COMMENT "Embedding DejaVu Sans Mono font family" VERBATIM)
add_custom_target(embed_fonts DEPENDS "${IMG2_GENERATED_DIR}/embedded_fonts.c" "${IMG2_GENERATED_DIR}/embedded_fonts.h")
```
     The source list uses `${IMG2_GENERATED_DIR}/embedded_fonts.c`. Add `${IMG2_GENERATED_DIR}` and `${PROJECT_SOURCE_DIR}/src/libimg2` to the global include list (made target-private in S3). Run `git rm src/libimg2/font/embedded_fonts.{c,h}`.

**Pattern to follow**: none new; path changes only.
**Verification**:
- `gate.sh S2`.
- **[R18]/[Q7]** S2a prints only `R100<TAB>old<TAB>new` lines. Run the check **right after the S2a commit, before S2b is committed**, against `HEAD`: `git diff --name-status -M100% img2/s01 HEAD | grep -v '^R100'` → empty. If it is run after the S2b commit, use `HEAD~1` instead of `HEAD`. (`git show --stat` over a range does not show rename similarity and is not used.)
- `grep -rn '"\.\./\.\./src/imgcat2' src/libimg2` → empty.
- `git ls-files | grep embedded_fonts` → empty; `build/generated/embedded_fonts.c` exists and `cmp`s equal to `git show a50e93d:src/imgcat2/font/embedded_fonts.c`.
- `git status --short src/imgcat2/compat` still shows `??`.
- Golden: 0 mismatches.

**Commit**: S2a `refactor(tree): move library sources to src/libimg2 and flatten CLI core (no content change)` (body: "build fixed in the next commit; git bisect skip"); S2b `refactor(tree): fix include paths after move; generate embedded fonts into build dir`
**Rollback**: `git reset --hard img2/s01` (both commits together).

---

### Step S3: Targets, feature contract, link order, test helper; per-target flags; explicit test linkage
**Files**:
- create `cmake/Img2Features.cmake` (`img2_feature()`, `IMG2_ALL_FEATURES`, `IMG2_LINK_ORDER`);
- create `cmake/Img2Testing.cmake` (`img2_add_test()`, `img2_static_executable()`);
- create `src/libimg2/CMakeLists.txt` (`img2`, `img2::img2`, `img2_testing`, font generation);
- create `src/imgcat2/CMakeLists.txt` (`imgcat2_cli`, `imgcat2`);
- modify every `cmake/vendor/Img2Vendor*.cmake` that has `add_definitions(-DHAVE_*)` (old :276, :381, :515, :559, :591, :611, :884, :1033, :1113, :1139, :1203, :1246, :1332, :1396, :1446), plus the EXIF block (old :1620-1624);
- modify `cmake/Img2Platform.cmake` (no `-static`/stack flags in `CMAKE_EXE_LINKER_FLAGS`; compute `IMG2_EXE_LINK_FLAGS`), `cmake/Img2VendorHelpers.cmake` (`img2_vendor_section_begin/end`), `cmake/Img2ProjectSettings.cmake` (`img2_apply_project_settings(<tgt>)`), `cmake/Img2Options.cmake` (`IMG2_BUILD_CLI`);
- modify the top-level `CMakeLists.txt` (remove global includes, sources, targets and install; add the subdirectories);
- rewrite `src/tests/CMakeLists.txt` (all 779 lines) using `img2_add_test`.

**Dependencies**: S2
**Description**:
1. `cmake/Img2Features.cmake`:
```cmake
# Feature names == today's macro suffix, so the S5 macro rename is one place (IMG2_FEATURE_MACRO_PREFIX).
set(IMG2_ALL_FEATURES ZLIB LIBPNG JPEGQS LIBJPEG GIFLIB WEBP HEIF TIFF RAW JXL RESVG EXIF_READER)
# [R17] Only features that had an add_definitions(-DHAVE_*) on master get a macro (CMakeLists.txt:276, :381, :515,
# :559, :591/:611, :884/:1033, :1113/:1139, :1203/:1246, :1332/:1396, :1446, :1623). ZLIB never had one.
set(IMG2_MACRO_FEATURES LIBJPEG JPEGQS LIBPNG GIFLIB WEBP HEIF TIFF RAW JXL RESVG EXIF_READER)
set(IMG2_LINK_ORDER   LIBPNG ZLIB JPEGQS LIBJPEG GIFLIB WEBP HEIF TIFF RAW JXL RESVG)   # == CMakeLists.txt:1677-1751
# [R7] Include order is NOT the link order: old :1504-1538 = PNG, ZLIB, JPEG (unconditional), then
# GIF, WEBP, HEIF, TIFF, RAW, JXL, RESVG, and jpeg-quantsmooth LAST (its directory has its own jconfig.h,
# which must not shadow libjpeg-turbo's).
set(IMG2_INCLUDE_ORDER LIBPNG ZLIB LIBJPEG GIFLIB WEBP HEIF TIFF RAW JXL RESVG JPEGQS)
set(IMG2_FEATURE_MACRO_PREFIX "HAVE_")   # S5 changes this single line to "IMG2_HAVE_"
function(img2_feature name)
	cmake_parse_arguments(F "" "" "LIBS;INCLUDE_DIRS;ARCHIVES;ARCHIVE_TARGETS;SYSTEM_LIBS" ${ARGN})
	set(IMG2_HAVE_${name} ON PARENT_SCOPE)
	set(IMG2_${name}_LIBS "${F_LIBS}" PARENT_SCOPE)
	set(IMG2_${name}_INCLUDE_DIRS "${F_INCLUDE_DIRS}" PARENT_SCOPE)
	set(IMG2_${name}_ARCHIVES "${F_ARCHIVES}" PARENT_SCOPE)   # [R8] per feature; ordered in S9 by IMG2_LINK_ORDER
	set(IMG2_VENDORED_ARCHIVE_TARGETS ${IMG2_VENDORED_ARCHIVE_TARGETS} ${F_ARCHIVE_TARGETS} PARENT_SCOPE)
	set(IMG2_SYSTEM_LINK_LIBS ${IMG2_SYSTEM_LINK_LIBS} ${F_SYSTEM_LIBS} PARENT_SCOPE)
endfunction()
# [R8] Archives of vendors that are not features (lcms2, brotli, ... from IMGCAT2_VENDORED_TRAILING_LIBS,
# CMakeLists.txt:1461-1477) are registered in trailing-lib order; S9 appends them after the feature archives.
function(img2_register_archives)
	cmake_parse_arguments(A "" "" "ARCHIVES;ARCHIVE_TARGETS" ${ARGN})
	set(IMG2_TRAILING_ARCHIVES ${IMG2_TRAILING_ARCHIVES} ${A_ARCHIVES} PARENT_SCOPE)
	set(IMG2_VENDORED_ARCHIVE_TARGETS ${IMG2_VENDORED_ARCHIVE_TARGETS} ${A_ARCHIVE_TARGETS} PARENT_SCOPE)
endfunction()
```
   Each vendor module replaces `add_definitions(-DHAVE_X)` with `img2_feature(X LIBS <exactly what old :1677-1751 linked for X, same 3-way choice> INCLUDE_DIRS <what old :1498-1539 added for X, same if() conditions>)`:
   - TIFF/RAW/JXL pick the LIBS variant exactly as old :1700-1746 (vendored target / `*_CUSTOM_LIBS` on APPLE / `*_STATIC_LDFLAGS`); HEIF uses `HEIF_CUSTOM_LIBS`.
   - zlib registers `ZLIB` with `LIBS ${ZLIB_LIBRARIES}`; png registers `LIBPNG` with `LIBS ${PNG_LIBRARIES}`.
   - EXIF registers `EXIF_READER` (no libs).
   - jpegqs registers `JPEGQS` with `LIBS jpegqs` and `INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/vendor/jpeg-quantsmooth` (old :1537-1538).
   - **[R8]** Non-feature vendors in `IMGCAT2_VENDORED_TRAILING_LIBS` (lcms2, brotli, ...) call `img2_register_archives()` in S9 (the archive lists stay empty until then).
2. `img2_vendor_section_begin/end` (in `Img2VendorHelpers.cmake`) keep vendor configure flags identical:
```cmake
macro(img2_vendor_section_begin)
	set(_IMG2_SAVED_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
	string(JOIN " " _img2_lf ${IMG2_EXE_LINK_FLAGS})
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${_img2_lf}")   # vendors see today's -static/stack flags
endmacro()
macro(img2_vendor_section_end)
	set(CMAKE_EXE_LINKER_FLAGS "${_IMG2_SAVED_EXE_LINKER_FLAGS}")
endmacro()
```
   `Img2Platform.cmake` computes `IMG2_EXE_LINK_FLAGS` instead of appending to `CMAKE_EXE_LINKER_FLAGS`: Linux `-static;-Wl,-z,stack-size=8388608`; APPLE `-Wl,-stack_size,0x800000` (+ `-nostdlib++` when static libc++ is found, as old :70); WIN32 `/STACK:8388608`. The top level calls `img2_vendor_section_begin()` right before the first vendor include (`Img2VendorZlib.cmake`; this works because S1 already moved `include(Img2VendorHelpers)` above it, **[Q3]**) and `img2_vendor_section_end()` after `Img2LinkOrder`. Vendor subdirectories capture the flags at `add_subdirectory` time; top-level targets see the restored value.
3. `cmake/Img2Testing.cmake`:
```cmake
function(img2_static_executable tgt)
	target_link_options(${tgt} PRIVATE ${IMG2_EXE_LINK_FLAGS})
	if(APPLE AND STATIC_LIBCXX_LIBS)
		target_link_libraries(${tgt} PRIVATE ${STATIC_LIBCXX_LIBS})
	elseif(APPLE AND DYNAMIC_LIBCXX_LIBS)
		target_link_libraries(${tgt} PRIVATE ${DYNAMIC_LIBCXX_LIBS})
	endif()
endfunction()
# img2_add_test(<name> SOURCES <files...> [KIND unit|integration] [LINK lib|cli]
#               [REQUIRES <FEATURE>...] [TIMEOUT <s>] [WORKDIR <dir>] [LABELS ...])
function(img2_add_test name)
	cmake_parse_arguments(T "" "KIND;LINK;TIMEOUT;WORKDIR" "SOURCES;REQUIRES;LABELS" ${ARGN})
	if(NOT T_KIND)    set(T_KIND unit)    endif()
	if(NOT T_LINK)    set(T_LINK lib)     endif()
	if(NOT T_TIMEOUT) set(T_TIMEOUT 10)   endif()
	foreach(_f IN LISTS T_REQUIRES)
		if(NOT IMG2_HAVE_${_f})
			return()
		endif()
	endforeach()
	if(T_LINK STREQUAL "cli" AND NOT TARGET imgcat2_cli)
		return()
	endif()
	add_executable(${name} ${T_KIND}/main.c ${T_SOURCES})
	if(T_LINK STREQUAL "cli")
		target_link_libraries(${name} PRIVATE imgcat2_cli img2_testing)
	else()
		target_link_libraries(${name} PRIVATE img2_testing)
	endif()
	target_compile_definitions(${name} PRIVATE IMGCAT2_TEST_TMPDIR="${PROJECT_BINARY_DIR}/test-tmp")
	img2_apply_project_settings(${name})
	img2_static_executable(${name})          # explicit: tests link -static exactly as today (switched in S8)
	add_test(NAME ${name} COMMAND ${name})
	if(NOT T_LABELS)
		set(T_LABELS ${T_KIND} ${T_LINK})
	endif()
	set_tests_properties(${name} PROPERTIES TIMEOUT ${T_TIMEOUT} LABELS "${T_LABELS}"
		ENVIRONMENT "TMPDIR=${PROJECT_BINARY_DIR}/test-tmp")
	if(T_WORKDIR)
		set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY ${T_WORKDIR})
	endif()
endfunction()
```
   Use the `if()/endif()` block form; one-line `if()` is shown only for brevity. Keep the old `test-tmp` directory creation (src/tests/CMakeLists.txt:6) and any other per-test `ENVIRONMENT` from the old loop (:773-778). The loop itself is replaced by the helper.
4. `src/libimg2/CMakeLists.txt`:
```cmake
set(IMG2_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")   # font custom command moves here from S2
# ... add_custom_command / embed_fonts exactly as in S2, with this IMG2_GENERATED_DIR ...
add_library(img2 STATIC)
add_library(img2::img2 ALIAS img2)
target_sources(img2 PRIVATE
	core/image.c
	decoders/decoder.c decoders/magic.c decoders/decoder_stb.c decoders/decoder_png.c decoders/decoder_jpeg.c
	decoders/decoder_qoi.c decoders/decoder_ico.c decoders/decoder_pnm.c decoders/decoder_yuv.c decoders/decoder_nanosvg.c
	encoders/encoder.c encoders/encoder_jpeg.c encoders/encoder_png.c
	text/font_manager.c "${IMG2_GENERATED_DIR}/embedded_fonts.c")
# then one if(IMG2_HAVE_<X>) block per optional codec, with exactly the file lists of old :1541-1638
# (gif, webp + encoder_webp, heif/avif + encoder_heif, tiff, raw, jxl + encoder_jxl, resvg, EXIF: metadata/*.c)
add_dependencies(img2 embed_fonts)
set(_img2_defs "")
set(_img2_incs ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR} ${IMG2_GENERATED_DIR}   # old :1500 first, then the two S2 additions
	${NANOSVG_INCLUDE_DIR} ${STB_INCLUDE_DIR} ${QOI_INCLUDE_DIR})                          # old :1501-1503
foreach(_f IN LISTS IMG2_MACRO_FEATURES)          # [R17] no HAVE_ZLIB
	if(IMG2_HAVE_${_f})
		list(APPEND _img2_defs ${IMG2_FEATURE_MACRO_PREFIX}${_f})
	endif()
endforeach()
foreach(_f IN LISTS IMG2_INCLUDE_ORDER)           # [R7] old :1504-1538 order, jpegqs last
	if(IMG2_HAVE_${_f})
		list(APPEND _img2_incs ${IMG2_${_f}_INCLUDE_DIRS})
	endif()
endforeach()
target_compile_definitions(img2 PRIVATE ${_img2_defs})
target_include_directories(img2 PRIVATE ${_img2_incs})
foreach(_f IN LISTS IMG2_LINK_ORDER)
	if(IMG2_HAVE_${_f})
		target_link_libraries(img2 PRIVATE ${IMG2_${_f}_LIBS})
	endif()
endforeach()
target_link_libraries(img2 PRIVATE ${IMGCAT2_VENDORED_TRAILING_LIBS})
if(UNIX AND NOT APPLE)
	target_link_libraries(img2 PRIVATE m)
endif()
if(APPLE AND STATIC_LIBCXX_LIBS)
	target_link_libraries(img2 PRIVATE ${STATIC_LIBCXX_LIBS})
elseif(APPLE AND DYNAMIC_LIBCXX_LIBS)
	target_link_libraries(img2 PRIVATE ${DYNAMIC_LIBCXX_LIBS})
endif()
img2_apply_project_settings(img2)
add_library(img2_testing INTERFACE)          # never installed/exported; white-box tests + transitional CLI
target_link_libraries(img2_testing INTERFACE img2)
target_include_directories(img2_testing INTERFACE ${_img2_incs})
target_compile_definitions(img2_testing INTERFACE ${_img2_defs})
```
   Keep the global include list order inside `_img2_incs` exactly as old :1498-1539, including PNG/ZLIB/JPEG dirs (old :1504-1506) and the `if(GIF_FOUND)` conditions. **[R7]** `IMG2_INCLUDE_ORDER` reproduces that order (it differs from `IMG2_LINK_ORDER`: JPEGQS comes last, at old :1537-1538). LIBPNG/ZLIB/LIBJPEG always register (unconditional in old :1504-1506). Each other module passes `INCLUDE_DIRS` only under the old `if(<X>_FOUND)` condition (old :1509-1538). The `-I` order of `src/libimg2` entries in `compile_commands.json` must equal the master entries' order for the same file (manual diff of one decoder TU, e.g. decoder_jpeg.c). The only insertion is the two S2 directories, placed directly after `${PROJECT_SOURCE_DIR}/src` (the old first entry, :1500), exactly where S2 put them in the global list (**[L4]**). `PRIVATE` link items on a STATIC library still propagate as `$<LINK_ONLY:...>`, which preserves the link closure.
5. `src/imgcat2/CMakeLists.txt`:
```cmake
set(_cli_src cli.c pipeline.c metadata.c base64.c ansi/ansi.c ansi/escape.c terminal/file.c)
if(WIN32)
	list(APPEND _cli_src terminal/terminal_windows.c)
else()
	list(APPEND _cli_src terminal/terminal_unix.c terminal/iterm2.c terminal/kitty.c)
endif()
add_library(imgcat2_cli STATIC ${_cli_src})
target_include_directories(imgcat2_cli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(imgcat2_cli PUBLIC img2_testing)   # TRANSITIONAL: replaced by img2 in S7 (public API only)
img2_apply_project_settings(imgcat2_cli)
if(IMG2_BUILD_CLI)
	add_executable(imgcat2 main.c)
	target_link_libraries(imgcat2 PRIVATE imgcat2_cli)
	set_target_properties(imgcat2 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")   # keeps build/imgcat2
	img2_apply_project_settings(imgcat2)
	img2_static_executable(imgcat2)
	install(TARGETS imgcat2 RUNTIME DESTINATION bin)   # replaced in S9
endif()
```
   The top level adds `add_subdirectory(src/libimg2)`, then `if(IMG2_BUILD_CLI OR IMG2_BUILD_TESTS) add_subdirectory(src/imgcat2) endif()`, then tests. Add `option(IMG2_BUILD_CLI "Build the imgcat2 command line tool" ${PROJECT_IS_TOP_LEVEL})` to `Img2Options.cmake`.
6. `img2_apply_project_settings(<tgt>)` (`Img2ProjectSettings.cmake`) does `target_compile_options(<tgt> PRIVATE ${IMGCAT2_WARNING_FLAGS})` and sets the `C_CLANG_TIDY` property when tidy is enabled. It replaces the directory-wide `add_compile_options(${IMGCAT2_WARNING_FLAGS})` (old :1482) and the global `CMAKE_C_CLANG_TIDY`. The gcc_s removal (old :1490-1496) stays as a directory-scoped variable. The global `add_compile_options(-O3 -g)` is left for S8.
7. `src/tests/CMakeLists.txt` rewrite. Each old 4-line block becomes one `img2_add_test(...)` line with the same name, sources, timeout and working directory (`${PROJECT_BINARY_DIR}`).
   - Drop the recompiled lib/CLI sources (old :443, :460-461, :494, :517, :538, :558, :578, :598, :757-759) and the extra link libs (old :499, :522, :543, :563, :583, :725).
   - `LINK cli` for tests that use CLI symbols: `test_cli_parsing`, `test_stdin_pipe`, `test_pipeline_end_to_end`, `test_convert`, `test_decoder_yuv`, `test_animation`, `test_cli_arguments*`, `test_terminal`, `test_cross_platform`, `test_file_render`, `test_ansi`, `test_metadata_output`, `test_security` (uses `read_file_secure`), and `test_raw_exif_integration` (calls the CLI's `read_file_secure` at src/tests/integration/test_raw_exif_integration.c:37, **[M4]**; it keeps `REQUIRES RAW`). All others use `LINK lib`. Before registering, run `grep -rlnE '\b(read_file_secure|read_stdin_secure|pipeline_[a-z_]+|parse_arguments|validate_options)\(' src/tests` and confirm that every hit is in the `LINK cli` list.
   - `REQUIRES` replaces the old `if(ENABLE_*)` blocks (:645-751): `test_encoder_webp WEBP`, `test_encoder_heif HEIF`, `test_encoder_jxl JXL`, `test_{webp,heif,jxl,raw,tiff}_exif_integration` with the matching feature, and the orientation tests with WEBP/HEIF/JXL/RAW/TIFF.
   - Tests keep their own `extern decode_X` prototypes; the symbols are global in `img2`.
   - The orphan `unit/test_xml_simple.c` stays unregistered.

**Pattern to follow**: P13 (replaced by the helper), P14; link order CMakeLists.txt:1669-1774.
**Verification**:
- `rm -rf build && gate.sh S3`.
- `ctest --test-dir build -N | tail -1` count == the S0 count.
- `diff` of normalised `build/src/imgcat2/CMakeFiles/imgcat2.dir/link.txt` (the exe moved into the `src/imgcat2` subdirectory; it was `build/CMakeFiles/imgcat2.dir/link.txt` through S2) vs `master-link.txt`. **[R15]** On master the exe compiles `IMGCAT2_SOURCES` directly (`add_executable(imgcat2 ${IMGCAT2_SOURCES} src/imgcat2/main.c)`, CMakeLists.txt:1661), so master's link line lists our `.o` files where S3 lists `src/imgcat2/libimgcat2_cli.a` + `src/libimg2/libimg2.a`. A plain diff therefore always differs. Compare only the token sequence **after** our objects/archives: strip every `*.o` token and the two new archives from both, normalise build-relative paths, then diff. The vendored archives, `-l` items and link flags must appear in the same order. Symbol-resolution equivalence itself is proven by the golden gate, not by link.txt.
- `file build/imgcat2` → "statically linked"; `file build/src/tests/test_magic` → "statically linked".
- `grep -rn 'add_definitions\|^include_directories' CMakeLists.txt cmake` → empty.
- `grep -n '\.\./imgcat2/\|\.\./libimg2/' src/tests/CMakeLists.txt` → empty.
- `-fopenmp` still appears in `build/src/imgcat2/CMakeFiles/imgcat2.dir/link.txt` when OpenMP was found (CMP0099 propagation through the PRIVATE link, **[L5]**).
- **[M2] Vendor compile-flag gate.** `python3 -I .claude/tmp/img2-split/cc_vendor_diff.py .claude/tmp/img2-golden/master-compile_commands.json build/compile_commands.json --allow-drop-define HAVE_LIBJPEG --allow-drop-define HAVE_JPEGQS ...` (one option per feature macro that the old global `add_definitions` set, i.e. the `IMG2_MACRO_FEATURES` list; there is no `HAVE_ZLIB`, **[R17]**; also check that no `src/libimg2` entry gained a `-DHAVE_ZLIB`) prints the per-file diff. For every dropped `HAVE_<X>`, run `grep -rlw HAVE_<X> <that vendor's source dir>` restricted to files that appear in `build/compile_commands.json` and to the headers they include from the same vendor tree.
  - No hit: the drop is harmless; record it in `progress.md`.
  - A hit (candidates named by the review: `vendor/libheif/heifio/stubs.cc`, `vendor/libwebp/imageio/*.c`, `vendor/openjpeg/src/bin/jp2/*`; they are expected not to be built): re-add exactly that definition to exactly that vendor target with `target_compile_definitions(<vendor-target> PRIVATE HAVE_<X>)` in its `cmake/vendor/` module (comment: "kept for byte identity; was a global add_definitions"), then re-run until `cc_vendor_diff.py` reports only allowed, grep-cleared drops.
  - **[Q2] Dropped global includes on top-level vendor targets.** On master, `include_directories()` (CMakeLists.txt:1499-1539) also appends to the `INCLUDE_DIRECTORIES` of every target already defined in the top-level directory. Those are the vendor targets created there: `jpegqs_base/_sse2/_avx2/_avx512/_dispatch` and `jpegqs` (:288-338), `vendor_giflib` (:537), `vendor_lcms2` (:633), `vendor_kvazaar` (:722), `vendor_libraw` (:1162). They currently compile with ~18 extra `-I` (our `src/` plus every codec include dir), which S3 removes. Allowed only as: `--allow-drop-include <each dir of the old :1499-1539 list, as expanded on master> --only-targets 'jpegqs[a-z0-9_]*|vendor_giflib|vendor_lcms2|vendor_kvazaar|vendor_libraw'`. Any other include change still fails. **Object identity proof** for those targets: for every object of these targets, `objdump -d --no-show-raw-insn` of the master object (kept from S0: copy `build/CMakeFiles/{jpegqs*,vendor_giflib,vendor_lcms2,vendor_kvazaar,vendor_libraw}.dir/` to `.claude/tmp/img2-golden/master-vendor-objs/` before S1) vs the S3 object, with the file-name header lines stripped, must be identical; equally `objdump -s -j .rodata -j .data`. A mismatch means a header was resolved from a different directory: restore exactly the needed `-I` on that target (`target_include_directories(<tgt> PRIVATE ...)`, commented "kept for byte identity") and re-run. Record the allowance and the proof result in `progress.md`.
- Golden: 0 mismatches.

**Commit**: `build: add img2 static library, imgcat2_cli and imgcat2 targets; img2_feature registry, IMG2_LINK_ORDER, img2_add_test`
**Rollback**: `git reset --hard img2/s02`; `rm -rf build`.

---

### Step S4: Log module + `fprintf(stderr)` → `img2_log` (scripted) + cut lib→CLI edges + layering guard
**Files**:
- create `src/libimg2/core/log.h`, `src/libimg2/core/log.c`, `cmake/scripts/img2_check_layering.cmake`, `src/tests/unit/test_log.c`;
- modify every lib `.c` with `fprintf(stderr` (decoders incl. the 2 sites in `nanosvg.h`, encoders, `image.c` 33, `font_manager.c` 6);
- modify `src/libimg2/decoders/decoder.h:16,80,137`, `decoder.c:176-192,224-315`;
- modify `src/libimg2/encoders/encoder.h:16,78` (+ the moved enum), `encoder.c:80-95`;
- modify `src/imgcat2/cli.h:15-28`, `src/imgcat2/main.c:27-117`, `src/imgcat2/cli.c:126`, `src/imgcat2/pipeline.c:485-511`, `src/imgcat2/metadata.{c,h}` (lose `get_mime_string`), `src/libimg2/decoders/magic.{c,h}` (gain it);
- modify tests calling `decoder_registry_init(...)`, `encoder_registry_init(...)` or `decoder_decode(...)` (test_convert.c, test_pipeline_end_to_end.c, test_decoder_{png,jpeg,stb,pnm,yuv}.c, test_encoder.c, test_file_render.c);
- modify `src/libimg2/CMakeLists.txt` (add `core/log.c`; `find_package(Threads REQUIRED)` once, at top level **after `img2_vendor_section_end()`** (**[Q10]**: not in `Img2Platform.cmake`, which is included before the vendor section, where the `-static` vendor link flags would bias the `Threads` try-compile checks), immediately before `add_subdirectory(src/libimg2)`; link `Threads::Threads` PRIVATE) and `src/tests/CMakeLists.txt` (new tests).

**Dependencies**: S3
**Description**:
1. `src/libimg2/core/log.h`. Final names; the public part moves verbatim to `<img2/log.h>` in S7.
```c
#ifndef IMG2_INTERNAL_CORE_LOG_H
#define IMG2_INTERNAL_CORE_LOG_H
typedef enum img2_log_level { IMG2_LOG_ERROR = 0, IMG2_LOG_WARN = 1, IMG2_LOG_INFO = 2, IMG2_LOG_DEBUG = 3 } img2_log_level_t;
/* [R20] fn must be thread-safe, must not call any img2_* function, and may run with internal locks held. */
typedef void (*img2_log_fn)(img2_log_level_t level, const char *msg, void *user);
void img2_set_log_callback(img2_log_fn fn, void *user);   /* fn == NULL restores the default stderr sink */
void img2_set_log_level(img2_log_level_t max_level);       /* emit messages with level <= max_level; default IMG2_LOG_ERROR */
img2_log_level_t img2_get_log_level(void);
/* internal */
void img2_log(img2_log_level_t level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#endif
```
   `log.c`:
   - state: `static _Atomic int s_level = IMG2_LOG_ERROR;` and `static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER; static img2_log_fn s_fn; static void *s_user;`;
   - `img2_set_log_callback` writes the pair under `s_lock`, so a reader never sees a torn `fn`/`user` pair ("swapped atomically");
   - `img2_log`: return if `level > atomic_load(&s_level)`. Format into `char buf[512]` with `va_copy`; if `n >= 512`, malloc `n+1` and format again (truncated `buf` on malloc failure). Lock, copy fn/user, unlock. If `fn` is set, call `fn(level, msg, user)` **outside** the lock; else `fputs(msg, stderr); fputc('\n', stderr);`. Free any heap buffer.
   - **[SM4]** The `format(printf, 2, 3)` attribute is mandatory. Never call `img2_log(lvl, s)` with a non-literal `s`; untrusted text (EXIF/XMP/SVG strings, vendor messages, `stbi_failure_reason()`) is always an argument of a literal `"%s"`. The default sink deliberately does not sanitise control bytes (byte identity); S9 documents this.
   - **[SM4] Compiler enforcement**, added in this step to `src/libimg2/CMakeLists.txt`: `target_compile_options(img2 PRIVATE -Wformat=2 -Wformat-security -Werror=format-security)` (GCC/Clang only; guard with `if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")`). Only `img2`'s own sources get this, not vendors or the CLI. If `-Wformat-nonliteral` (part of `-Wformat=2`) fires inside a header-only vendor compiled into one of our TUs (stb, nanosvg, qoi), add `"-Wformat-nonliteral"` to that include's existing `#pragma GCC diagnostic` push/ignored block, not to the target. Our own sites are fixed. The flags change diagnostics only, not code generation; the golden gate confirms this.
2. `.claude/tmp/img2-split/log_convert.py` runs over `src/libimg2/**/*.c` and `src/libimg2/decoders/nanosvg.h`:
   - First it asserts that every `fprintf(stderr, "...` format literal ends in `\n` and is a single literal. On any exception it refuses to write and lists the site for a manual merge.
   - It skips `//` lines and `/* ... */` regions (tracks state).
   - Regex `fprintf\(stderr,\s*"((?:[^"\\]|\\.)*)\\n"` → `img2_log(<LVL>, "\1"`.
   - `<LVL>`: `IMG2_LOG_WARN` if the text starts with `Warning:`, `Note:` or `Info:`; otherwise `IMG2_LOG_ERROR`. **[RESOLVED]** No unconditional message is mapped to INFO, because that would hide it at the CLI's WARN level.
   - It inserts `#include "<rel>/core/log.h"` after the last **unconditional** `#include "` of each modified `.c` (**[R11]**). It tracks `#if`/`#ifdef`/`#ifndef` … `#endif` depth and only considers includes at depth 0, so the new line never lands inside a feature block such as `#ifdef HAVE_EXIF_READER` / `#include "../metadata/exif_reader.h"` / `#endif` (image.c:17-19, decoder_tiff.c:20-22). In image.c it therefore goes after `#include "stb_image_resize2.h"` (:14); in decoder_tiff.c after `#include "decoder.h"` (:18). A file whose only quoted includes are conditional gets the line after its last unconditional `#include <...>`. The dry run lists the chosen anchor line per file for review. For `nanosvg.h`, `decoder_nanosvg.c` must include `../core/log.h` **before** the nanosvg includes.
   - It prints per-file counts (dry-run first).
3. Manual: in `decoder.c` and `encoder.c`, the 5 `opts->silent`-gated blocks (decoder.c:186-191, :244-246, :262-264, :310-312; encoder.c:90-94) become unconditional `img2_log(IMG2_LOG_INFO, ...)` with the same text.
4. Cut the edges:
   - `decoder.h`: delete the `cli.h` include. Add (final name, extended in S6/S7):
```c
typedef struct img2_decode_opts {
	mime_type_t force_mime;          /* MIME_UNKNOWN = detect */
	const yuv_params_t *yuv;         /* required for MIME_YUV */
	int max_frames;                  /* <= 0: no extra limit (implemented in S6) */
	int jpeg_deblock;                /* -1 default (jpegqs if built), 0 off, 1 on (implemented in S6) */
} img2_decode_opts_t;
void decoder_registry_init(void);   /* logs the registered decoders at IMG2_LOG_INFO */
image_t **decoder_decode(const img2_decode_opts_t *opts, const uint8_t *data, size_t len, mime_type_t mime, int *frame_count);
```
     In the YUV branch of `decoder.c`: `if (opts == NULL || opts->yuv == NULL) { img2_log(IMG2_LOG_ERROR, "Error: Raw YUV input requires --yuv format:width:height"); return NULL; }`. Copy the text verbatim from the existing message. Then the INFO log with `yuv_format_name(opts->yuv->format)`, width and height, then `decode_yuv(data, len, opts->yuv, frame_count)`.
   - `encoder.h`: delete the `cli.h` include. Move the `output_format_t` enum (cli.h:21-28) here verbatim. `void encoder_registry_init(void);`.
   - `cli.h`: include `"../libimg2/encoders/encoder.h"` (which pulls in image.h) and keep `"../libimg2/decoders/yuv.h"`. Delete the enum.
   - `main.c`: the first statement in `main` is `img2_set_log_level(IMG2_LOG_WARN);`. After the `parse_arguments`/`validate_options`/`--fonts` chain (main.c:84-98) add `if (!opts.silent) { img2_set_log_level(IMG2_LOG_INFO); }` before the verbose print at :100. :114/:117 become `encoder_registry_init(); decoder_registry_init();` (same order).
   - `cli.c:126` → `decoder_registry_init();`. It runs while the level is WARN, so it prints nothing, exactly like today's `NULL` opts.
   - `pipeline.c` `pipeline_decode` builds `img2_decode_opts_t dopts = { .force_mime = MIME_UNKNOWN, .yuv = opts->yuv_input ? &opts->yuv : NULL, .max_frames = 0, .jpeg_deblock = -1 };` and calls `decoder_decode(&dopts, buffer, size, mime, out_frame_count)`.
   - Move `get_mime_string` (src/imgcat2/metadata.c:20-49 and its prototype) verbatim into `src/libimg2/decoders/magic.{c,h}`.
   - Tests: `sed -E 's/\b(decoder|encoder)_registry_init\([^)]*\)/\1_registry_init()/g'`; hand-fix each `decoder_decode(&opts, ...)` call to build an `img2_decode_opts_t`.
5. Layering guard `cmake/scripts/img2_check_layering.cmake` (`cmake -P`, args `-DMODE=lib|cli -DROOT=<repo>`):
   - `MODE=lib`: scans `src/libimg2/**/*.{c,h}` (and `include/img2/*.h` once it exists). It fails on any quoted include whose path contains `imgcat2/`, `cli.h`, `pipeline.h`, `base64.h`, `terminal/` or `ansi/`, and on any token `cli_options_t`.
   - `MODE=cli` (enabled in S7): scans `src/imgcat2/*.{c,h}` and `src/imgcat2/{ansi,terminal}/**`, never `compat/`. It fails on quoted includes that resolve outside `src/imgcat2`, on `libimg2/`, and on the token `IMG2_HAVE_`.

   Registration: `add_test(NAME img2_layering_lib COMMAND ${CMAKE_COMMAND} -DMODE=lib -DROOT=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/scripts/img2_check_layering.cmake)`, `LABELS guard`.
6. `test_log.c` (`img2_add_test(test_log SOURCES unit/test_log.c)`) checks:
   - the callback receives level + message without a trailing newline;
   - level filtering (DEBUG is dropped at ERROR level);
   - a `NULL` callback restores the default sink;
   - a 2000-char message is delivered intact.

   It restores the level and callback at the end.

**Pattern to follow**: P1 (message text kept byte-for-byte).
**Verification**:
- `gate.sh S4`.
- `grep -rn 'fprintf(stderr' src/libimg2 | grep -v ':\s*//'` → empty.
- `grep -rn 'cli\.h\|cli_options_t' src/libimg2` → empty.
- `grep -n 'Wformat=2' build/compile_commands.json | grep -c src/libimg2` > 0, and no vendor entry has it (**[SM4]**).
- `img2_layering_lib` and `test_log` pass. Manual sanity check: temporarily add `#include "../../imgcat2/cli.h"` to `decoder.c` → `img2_layering_lib` fails; revert.
- Golden: 0 mismatches. This is the key check: the verbose cases prove INFO, and the error/warning cases prove ERROR/WARN.

**Commit**: `refactor(lib): add img2 log module, route library diagnostics through it, remove CLI types from the library`
**Rollback**: `git reset --hard img2/s03`.

---

### Step S5: Symbol hygiene: static pass, TU-local header-only vendors, nanosvg font wrappers, `img2_` rename, `img2_symbols` ctest
**Files**:
- `src/libimg2/decoders/magic.{c,h}` (`MAGIC_*` → `static const` in magic.c; delete the `extern`s at magic.h:50-159);
- `src/libimg2/decoders/decoder_stb.c:15-18`, `src/libimg2/core/image.c:11-14`, `src/libimg2/text/font_manager.{c,h}` (:47-48 + new wrappers), `src/libimg2/decoders/decoder_qoi.c:19-22`, `src/libimg2/decoders/nanosvg.h:177-202,2865-3046`, `src/libimg2/decoders/nanosvgrast.h:52-68` + definitions, `src/libimg2/decoders/decoder_nanosvg.c:18-25`;
- `tools/embed_font.py:17-22,64-101`;
- all files touched by the rename (lib, CLI except `compat/`, tests incl. `decoder_internal.h`, `cmake/Img2Features.cmake` macro prefix);
- create `cmake/scripts/img2_check_symbols.cmake`; modify `src/tests/CMakeLists.txt`.

**Dependencies**: S4
**Description**:
**Order inside S5 ([M3]): do item 2 (header-only vendors TU-local) first, rebuild, then run item 1 (static pass), then items 3-5.** Otherwise `nm_unused.py` sees the `stbi_*`/`stbir_*`/`stbtt_*`/`qoi_*`/`nsvg*` definitions that live in `vendor/` headers and proposes to edit vendor code. As a second safety net, `nm_unused.py` ignores any symbol whose `nm -l` source location (or, failing that, whose name prefix `stbi`, `stbir`, `stbtt`, `qoi_`, `nsvg`) points into a header-only vendor, and lists such symbols separately as "vendor leftovers, fix via item 2". The pass never proposes an edit to `vendor/**`.
1. **Static pass (nm-driven).** `.claude/tmp/img2-split/nm_unused.py` takes every global defined symbol (`nm -g --defined-only -P`) in `$<TARGET_FILE:img2>` (`build/src/libimg2/libimg2.a`) and counts `U` references in all other project objects (`build/src/**/*.o`, excluding vendor dirs). Each symbol with zero external references is handled as follows:
   - used inside its own TU → `static`, and its prototype is removed from headers;
   - unused anywhere → deleted (`static` would trip `-Wunused-function`).
   - **[P2] Exemption:** a symbol whose S5 rename-map row is marked `public` (the API surface published in S7) is never made static or deleted, even with zero references. Examples: `font_get_family` (font_manager.c:651, no caller today), `font_get_data`, and the metadata/yuv/mime functions. `nm_unused.py` reads `rename-map.tsv` and skips every `public` row. Before S5 is committed, cross-check that every public function named in the S7 item 1 headers still exists as a global in `libimg2.a`.

   Expected candidates: the 35 `MAGIC_*` arrays and the stb leftovers `decode_bmp/tga/psd/hdr/pnm` (decoder_stb.c:99-257). **[R14]** `decode_tga` is **not** called by any test. Its only occurrence in tests is the CTest name token in `CTEST(decoder_stb, decode_tga)` (test_decoder_stb.c:199), so `nm_unused.py` handles it like the other leftovers.
2. **Header-only vendors TU-local** (each implementation include wrapped in `#pragma GCC diagnostic push` / `ignored "-Wunused-function"` / `pop`):
   - `decoder_stb.c`: `#define STB_IMAGE_STATIC` and `#define STBI_ASSERT(x) ((void)0)` before `#include "stb_image.h"`.
   - `image.c`: `#define STB_IMAGE_RESIZE_STATIC` and `#define STBIR_ASSERT(x) ((void)0)` (after `STB_IMAGE_RESIZE_IMPLEMENTATION`).
   - `font_manager.c`: `#define STBTT_STATIC` and `#define STBTT_assert(x) ((void)0)`.
   - `decoder_qoi.c` before the include: `#define QOI_NO_STDIO`, `#define qoi_encode img2_qoi_encode`, `#define qoi_decode img2_qoi_decode`. qoi.h declares extern prototypes without a `QOIDEF`, so renaming satisfies decision 3.
   - nanosvg fork: after the include guard of `nanosvg.h` add `#ifndef NSVG_DEF\n#define NSVG_DEF\n#endif` and prefix the declarations/definitions of `nsvgParseFromFile` (:177/:3475), `nsvgParse` (:181/:3448), `nsvgDuplicatePath` (:184/:3504), `nsvgDelete` (:187/:3534), **and** (**[M3]**) the non-static internal function `nsvg__parseXML` (definition at nanosvg.h:338) and the non-static global data `NSVGNamedColor nsvg__colors[]` (nanosvg.h:1321 → `NSVG_DEF NSVGNamedColor nsvg__colors[] = {`). Do the same in `nanosvgrast.h` with `NSVGRAST_DEF` for `nsvgCreateRasterizer` (:52/:153), `nsvgRasterize` (:63/:1375), `nsvgDeleteRasterizer` (:68/:169). This list covers every non-static, non-indented `nsvg*` definition in both files (verified by regex on the fork); still check `nm decoder_nanosvg.o` for any other global and add `NSVG_DEF` to it. In `decoder_nanosvg.c`: `#define NSVG_DEF static` and `#define NSVGRAST_DEF static` before the includes, inside the pragma push/pop.
3. **nanosvg → font wrappers** (internal, in `src/libimg2/text/font_manager.h`, final names):
```c
typedef struct img2_font_face img2_font_face_t;
typedef struct img2_font_vertex { short x, y, cx, cy, cx1, cy1; unsigned char type; } img2_font_vertex_t;
enum { IMG2_FONT_VMOVE = 1, IMG2_FONT_VLINE = 2, IMG2_FONT_VCURVE = 3, IMG2_FONT_VCUBIC = 4 };   /* == STBTT_v* */
img2_font_face_t *img2_font_face_open(const img2_font_t *font);   /* NULL if stbtt_InitFont fails / OOM */
void  img2_font_face_close(img2_font_face_t *face);
float img2_font_face_scale_for_pixel_height(const img2_font_face_t *face, float pixels);
void  img2_font_face_vmetrics(const img2_font_face_t *face, int *ascent, int *descent, int *line_gap);
void  img2_font_face_hmetrics(const img2_font_face_t *face, int codepoint, int *advance, int *lsb);
int   img2_font_face_kern(const img2_font_face_t *face, int cp1, int cp2);
int   img2_font_face_glyph_shape(const img2_font_face_t *face, int codepoint, img2_font_vertex_t **out); /* count; *out malloc'd */
void  img2_font_face_free_shape(img2_font_vertex_t *v);
```
   Implementation in font_manager.c: `struct img2_font_face { stbtt_fontinfo info; };`.
   - `open`: malloc, `img2_font_get_data`, `stbtt_InitFont(&f->info, data, 0)`.
   - `glyph_shape`: calls `stbtt_GetCodepointShape`, copies each `stbtt_vertex` field into a malloc'd array (no layout punning), then `stbtt_FreeShape`.
   - the other wrappers are one-line forwards.

   In `nanosvg.h`:
   - remove the `stb_truetype.h` include;
   - change `nsvg__measureText` to take `img2_font_face_t*` and `nsvg__addGlyphToPaths` to take `img2_font_vertex_t*`;
   - replace `STBTT_v*` with `IMG2_FONT_V*`;
   - in the render function: `img2_font_face_t *face = img2_font_face_open(font); if (!face) { img2_log(IMG2_LOG_ERROR, "Error: stbtt_InitFont failed"); return; }`, then replace the stbtt calls one-to-one and call `img2_font_face_close(face)` on every return path and after the loop.

   **Keep both kerning calls' argument order verbatim** (P9 do-not-fix note). The arithmetic (int advance × float scale) is unchanged, so output is byte-identical.
4. **Rename map** (`.claude/tmp/img2-split/rename-map.tsv`, columns `old new [new_struct_tag]`). `gen_rename_map.py` lists every non-`img2_` global defined in `$<TARGET_FILE:img2>` plus the curated entries below. Any symbol it finds that is not in the table gets the default `img2_` + name and is flagged for review. Hard rules: no new name contains `img2__`; no CLI symbol is renamed (`pipeline_*`, `cli_*`, `terminal_*`, `ansi_*`, `escape_*`, `iterm2_*`, `kitty_*`, `base64_*`, `file_render`, `output_*`, `parse_arguments`, `print_*`, `validate_options`, `read_*_secure`, `generate_*_ansi`, `free_frame_lines`).

| Old | New | Notes |
|---|---|---|
| `image_t` (tag `image_t`/anon) | `img2_image_t` (tag `img2_image`) | public in S7 |
| `image_create` | `img2_image_alloc` | internal, pointer-returning (**[H1]**); ~155 uses in 41 files (lib + tests, none in the CLI). The public `img2_status_t img2_image_create(w, h, **out)` is new in S7 |
| `image_destroy` | `img2_image_destroy` | public |
| `image_calculate_size` | `img2_image_calc_size` | internal |
| `image_get_pixel` / `image_set_pixel` | `img2_image_get_pixel` / `img2_image_set_pixel` | public inline |
| `image_scale_fit` / `image_scale_resize` | `img2_image_scale_fit` / `img2_image_scale_resize` | internal (tests) |
| `image_apply_orientation` | `img2_image_orient` | internal; public wrapper `img2_apply_orientation` in S7 |
| `convert_rgb_to_rgba` / `convert_grayscale_to_rgba` | `img2_convert_rgb_to_rgba` / `img2_convert_gray_to_rgba` | internal |
| `IMAGE_MAX_DIMENSION/PIXELS/FILE_SIZE` | `IMG2_MAX_DIMENSION/PIXELS/FILE_SIZE` | public |
| `exif_info_t` (tag `exif_info_t`) | `img2_exif_t` (tag `img2_exif`) | `struct exif_info_t` → `struct img2_exif` |
| `xmp_info_t` (tag `xmp_info_t`) | `img2_xmp_t` (tag `img2_xmp`) | likewise |
| `exif_info_init` / `xmp_info_init` | `img2_exif_init` / `img2_xmp_init` | public |
| `exif_info_free` / `xmp_info_free` | `img2_exif_clear` / `img2_xmp_clear` | public (frees contents, not the struct) |
| `parse_exif` / `parse_xmp` | `img2_exif_parse` / `img2_xmp_parse` | internal aliases |
| `parse_exif_from_jpeg` / `parse_exif_from_tiff` | `img2_exif_parse_jpeg` / `img2_exif_parse_tiff` | public |
| `parse_xmp_from_jpeg` / `parse_xmp_from_xml` | `img2_xmp_parse_jpeg` / `img2_xmp_parse_xml` | public |
| `xml_node_type_t`, `XML_NODE_<X>` | `img2_xml_node_type_t`, `IMG2_XML_NODE_<X>` | internal |
| `xml_node_t` / `xml_document_t` (tags) | `img2_xml_node_t` / `img2_xml_document_t` (tags `img2_xml_node`/`img2_xml_document`) | internal |
| `xml_document_{create,destroy,parse,root_element}` | `img2_xml_document_*` | internal |
| `xml_element_{first_child,attribute,text,name,next_sibling}` | `img2_xml_element_*` | internal |
| `mime_type_t`, `MIME_<X>` (20) | `img2_mime_t`, `IMG2_MIME_<X>` | public |
| `detect_mime_type` / `mime_type_name` / `get_mime_string` | `img2_detect_mime` / `img2_mime_name` / `img2_mime_string` | public |
| `decoder_t` / `decode_func_t` | `img2_decoder_entry_t` / `img2_decode_fn` | internal |
| `decoder_registry_init` / `decoder_find_by_mime` | `img2_decoder_registry_init` / `img2_decoder_find` | internal |
| `decoder_decode` | `img2_decoder_dispatch` | internal dispatcher (mime given) |
| `decoder_free_frames` | `img2_decoder_free_frames` | deleted in S6 (→ `img2_frames_free`) |
| `g_decoder_registry` / `g_decoder_count` | `img2_decoder_registry` / `img2_decoder_registry_size` | removed in S6 (static + accessors) |
| `decode_{png,jpeg,gif,gif_animated,webp,heif,avif,tiff,raw,jxl,qoi,ico,stb,yuv,svg_nanosvg,svg_resvg}` | `img2_decode_<same>` | internal |
| `decode_netpbm` | `img2_decode_pnm` | internal |
| `decode_{bmp,tga,psd,hdr,pnm}` (decoder_stb.c leftovers, if still global) | `img2_decode_stb_{bmp,tga,psd,hdr,pnm}` | avoids a clash with `img2_decode_pnm` |
| `{png,gif,heif,avif,webp}_is_animated` | `img2_{png,gif,heif,avif,webp}_is_animated` | internal |
| `yuv_format_t`, `YUV_FMT_<X>` (10), `yuv_params_t` | `img2_yuv_format_t`, `IMG2_YUV_FMT_<X>`, `img2_yuv_params_t` | public |
| `yuv_format_from_name` / `yuv_format_name` / `yuv_parse_spec` / `yuv_frame_size` | `img2_yuv_*` | public |
| `output_format_t`, `FORMAT_{NONE,JPEG,PNG,HEIF,WEBP,JXL}` | `img2_format_t`, `IMG2_FORMAT_*` | public |
| `encoder_t` / `encode_func_t` | `img2_encoder_entry_t` / `img2_encode_fn` | internal |
| `encoder_registry_init` / `encoder_find_by_format` / `encoder_encode` | `img2_encoder_registry_init` / `img2_encoder_find` / `img2_encoder_dispatch` | internal |
| `g_encoder_registry` / `g_encoder_count` | `img2_encoder_registry` / `img2_encoder_registry_size` | removed in S6 |
| `encode_{jpeg,png,webp,heif,jxl}` | `img2_encode_<same>` | internal |
| `font_t` (tag `font_t`) | `img2_font_t` (tag `img2_font`) | public opaque |
| `font_weight_t`, `FONT_WEIGHT_*` / `font_style_t`, `FONT_STYLE_*` | `img2_font_weight_t`, `IMG2_FONT_WEIGHT_*` / `img2_font_style_t`, `IMG2_FONT_STYLE_*` | public |
| `font_manager_init` / `font_manager_cleanup` | `img2_font_manager_init` / `img2_font_manager_cleanup` | internal |
| `font_manager_load` / `font_manager_load_fallback` | `img2_font_find` / `img2_font_find_fallback` | internal, pointer-returning (**[H1]**); the only library caller is nanosvg.h:2972. The public status-returning `img2_font_load(..., **out)` / `img2_font_load_fallback(**out)` are new in S7 |
| `font_get_data` / `font_get_family` | `img2_font_get_data` / `img2_font_get_family` | public |
| `font_manager_list_fonts` | `img2_font_manager_list_fonts` | deleted in S7 (→ `img2_font_foreach`) |
| `embedded_font_t`, `embedded_dejavu_*` | `img2_embedded_font_t`, `img2_embedded_dejavu_*` | generated (embed_font.py) |
| `HAVE_{LIBJPEG,JPEGQS,LIBPNG,GIFLIB,WEBP,HEIF,TIFF,RAW,JXL,RESVG,EXIF_READER}` | `IMG2_HAVE_*` | not `PNG_APNG_SUPPORTED` |
| lib guards `IMGCAT2_<NAME>_H`, `EXIF_READER_H`, `XML_PARSER_H` | `IMG2_INTERNAL_<MODULE>_<NAME>_H` | CLI guards unchanged |

   - `.claude/tmp/img2-split/apply_rename.py` is a C tokenizer with states: code, `//` comment, `/* */` comment, string literal, char literal, preprocessor continuation. It replaces whole identifier tokens in code and comments, never inside string/char literals, so `#include "..."` lines and log text stay untouched. When the previous code token is `struct` and the row has a `new_struct_tag`, it emits the tag. Scope: `src/libimg2/**`, `src/imgcat2/**` (not `compat/`), `src/tests/**` (`*.c`, `*.h`). **[R14]** The arguments of the CTest case macros (`CTEST(suite, name)`, `CTEST2(suite, name)`, `CTEST_DATA(suite)`, `CTEST_SETUP(suite)`, `CTEST_TEARDOWN(suite)`) are suite/case names, not symbols, and are never renamed. Example: `CTEST(decoder_stb, decode_tga)` at test_decoder_stb.c:199 stays as it is. Dry-run first, then write.
   - CMake: change `IMG2_FEATURE_MACRO_PREFIX` to `"IMG2_HAVE_"` (one line in `cmake/Img2Features.cmake`).
   - `tools/embed_font.py`: `var_name = f'img2_embedded_dejavu_{variant}'`, the typedef becomes `img2_embedded_font_t`, the header guard becomes `IMG2_EMBEDDED_FONTS_H`.
5. **`img2_symbols` ctest.** `cmake/scripts/img2_check_symbols.cmake` (run with `-P`) takes `-DNM=<nm> -DPREFIX=img2_ [-DARCHIVE=<a>] [-DOBJ_LIST=<file;file>] [-DFORBIDDEN=<sym;...>]`.
   - It runs `${NM} -A -P -g` on the archive and/or each listed object.
   - Defined globals (types `T D B R C V W G S`) must match `^${PREFIX}`.
   - When `FORBIDDEN` is set (from S6), any `U` symbol in that list fails.
   - On a hit it calls `message(FATAL_ERROR ...)` listing `member: symbol`.
   - `IMG2_SYMBOL_ALLOWLIST` exists as a hook but must stay empty.

   Registration (Linux only):
```cmake
if(CMAKE_NM AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
	add_test(NAME img2_symbols COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} -DPREFIX=img2_
		-DARCHIVE=$<TARGET_FILE:img2> -P ${PROJECT_SOURCE_DIR}/cmake/scripts/img2_check_symbols.cmake)
	set_tests_properties(img2_symbols PROPERTIES LABELS guard)
endif()
```
   `$<TARGET_FILE:img2>` holds only our objects: jpegqs is its own STATIC target, and vendors are separate archives.

**Pattern to follow**: P8, P9, P12.
**Verification**:
- `gate.sh S5`.
- `ctest --test-dir build -R img2_symbols -V` passes.
- `nm -A -P -g --defined-only build/src/libimg2/libimg2.a | awk '$2 !~ /^img2_/'` → empty.
- `grep -rn 'stbtt_' src/libimg2 --include=*.h` → empty.
- `grep -rnE '\bHAVE_(LIBJPEG|JPEGQS|LIBPNG|GIFLIB|WEBP|HEIF|TIFF|RAW|JXL|RESVG|EXIF_READER)\b' src cmake | grep -v IMG2_HAVE_ | grep -v 'kept for byte identity'` → empty. **[P8]** The excluded lines are the vendor-target re-adds from S3's **[M2]** gate (`target_compile_definitions(<vendor-target> PRIVATE HAVE_<X>)  # kept for byte identity; was a global add_definitions` in `cmake/vendor/*.cmake`). They must keep the vendor-facing `HAVE_<X>` spelling, because vendor code tests that name; the S5 rename never touches `cmake/vendor/` (only the one `IMG2_FEATURE_MACRO_PREFIX` line in `cmake/Img2Features.cmake`). The comment must stay on the same line as the definition so that this grep works.
- Golden: 0 mismatches. The SVG text fixtures prove the wrapper path and kerning order; `--version` proves no decoder was lost.

**Commit**: `refactor(lib): prefix all library symbols with img2_, make header-only vendors TU-local`
**Rollback**: `git reset --hard img2/s04`.

---

### Step S6: Status codes, image model, init/shutdown, const registries, per-call options, font lock, no-terminate guarantee
**Files**:
- **S6a core (integrator, written first, not committed alone):**
  - create `src/libimg2/core/status.{h,c}`, `src/libimg2/core/init.c`;
  - modify `src/libimg2/core/image.{h,c}` (stride, delay_ms, `img2_frames_t`, `img2_frames_free`, unconditional exif/xmp free at old image.c:87-101, `img2_fit_size`, and the `img2_filter_t` enum that `scale_impl` needs, defined here with the exact text of the S7 `<img2/image.h>` block, **[P3]**);
  - modify `src/libimg2/decoders/decoder.{h,c}`, `src/libimg2/encoders/encoder.{h,c}`;
  - modify `src/libimg2/text/font_manager.c:40-42` and every public function (lock), and `font_manager.c:81` (`secure_getenv`, **[SL1]**);
  - modify `src/libimg2/decoders/decoder_jpeg.c:20-25,119-121`, `decoder_nanosvg.c:264-270`, `src/libimg2/metadata/exif_reader.c` (status returns);
  - modify `src/libimg2/decoders/decoder.c:25` (drop the local `extern decode_jpeg`; use the decoder.h prototype, **[L6]**);
  - modify `src/tests/unit/test_image_processing.c` (status-returning scale/convert helpers, **[M7]**);
  - modify the CLI call sites `src/imgcat2/{main.c,pipeline.c,cli.c,terminal/file.c,terminal/iterm2.c,terminal/kitty.c}`;
  - modify `src/libimg2/CMakeLists.txt` (EXIF sources always compiled; `check_symbol_exists(secure_getenv ...)`).
- **S6b fan-out (lanes L1-L3, see Execution notes):** each `decoder_*.c`/`encoder_*.c` and its tests, including the libpng handler routing in `decoder_png.c` (L1), the libtiff handler routing in `decoder_tiff.c` (L2) and the `decoder_raw.c:26` extern removal (L2) (**[M1b]/[L6]**).
- **S6c (integrator):** `src/tests/CMakeLists.txt` (forbidden-symbol mode, `-UNDEBUG` object variant, mutable-data audit, `test_threads`), create `src/tests/unit/test_threads.c`, `cmake/scripts/img2_mutable_allowlist.txt` and `src/tests/data/anim/smoke_anim.gif` (copied from `.claude/tmp/vendor-smoke/`; moved forward from S11 so the thread test has a GIF, **[SM3]**), and the vendor error-path audit.

**Dependencies**: S5
**Description**:
1. `status.h` (final; moves into public `<img2/common.h>` in S7):
```c
typedef enum img2_status {
	IMG2_OK = 0, IMG2_E_NOMEM = -1, IMG2_E_ARG = -2, IMG2_E_FORMAT = -3, IMG2_E_UNSUPPORTED = -4,
	IMG2_E_LIMIT = -5, IMG2_E_IO = -6, IMG2_E_CODEC = -7, IMG2_E_INTERNAL = -8,
	IMG2_E_NOT_FOUND = -9                        /* [H1] img2_font_load*: no matching font */
} img2_status_t;                                 /* values stable; append only */
const char *img2_strerror(img2_status_t st);     /* never NULL; "unknown status" fallback */
```
2. Image model in `core/image.h`:
```c
typedef struct img2_image {
	uint32_t width, height;
	uint32_t stride;          /* bytes per row; == width * 4 today */
	uint8_t *pixels;          /* RGBA8888 */
	uint32_t delay_ms;        /* frame duration, 0 = unknown/not animated (filled in S11) */
	img2_exif_t *exif;
	img2_xmp_t *xmp;
} img2_image_t;
typedef struct img2_frames { img2_image_t **frames; int count; int loop_count; } img2_frames_t;
void img2_frames_free(img2_frames_t *f);   /* destroys frames, frees array, zeroes *f; NULL-safe */
img2_status_t img2_fit_size(uint32_t src_w, uint32_t src_h, uint32_t max_w, uint32_t max_h,
                            uint32_t *out_w, uint32_t *out_h);   /* P10 math verbatim + old validation text */
```
   - `img2_image_alloc` (the S5 name of the old `image_create`; still pointer-returning, **[H1]**) sets `stride = width * 4` and `delay_ms = 0`. It stays the **single allocation gate** for every frame: it calls `img2_image_calc_size` (old image.c:21-40: zero, `> IMG2_MAX_DIMENSION`, `> IMG2_MAX_PIXELS` and `size_t` overflow checks) before any allocation (**[SM1]**). Grep that no decoder allocates pixel buffers for an `img2_image_t` by another route: `grep -rn 'pixels *= *\(malloc\|calloc\|realloc\)' src/libimg2` must hit only image.c. `img2_image_orient` updates `stride` when it swaps width/height. Grep every `->width =` assignment in lib/CLI/tests and update `stride` next to it.
   - `img2_image_destroy` frees exif/xmp unconditionally (`img2_exif_clear` + `free`).
   - `metadata/exif_reader.c` and `xml_parser.c` are now **always compiled**. `IMG2_HAVE_EXIF_READER` only gates metadata extraction inside decoders, so the struct and the ABI no longer depend on the flag.
   - `img2_image_orient` returns `img2_status_t` (the `-1` paths become `IMG2_E_ARG`/`IMG2_E_NOMEM` according to their log message; callers' `< 0` test stays valid).
   - **[R6] One internal scale implementation that takes a message tag**, so every legacy ERROR line keeps its exact prefix:
```c
/* image.c, static. tag is "image_scale_fit" or "image_scale_resize"; all texts are today's (image.c:107-194). */
/* [P3] img2_filter_t is defined in S6a in src/libimg2/core/image.h, with exactly the enum text of the S7
 * <img2/image.h> block (IMG2_FILTER_DEFAULT = 0, POINT, BOX, TRIANGLE, CUBIC, MITCHELL); S7 moves it verbatim
 * into the public header and image.h then just includes <img2/image.h>. */
static img2_status_t scale_impl(const char *tag, const img2_image_t *src, uint32_t w, uint32_t h,
                                img2_filter_t filter, img2_image_t **out);
```
     It logs at ERROR, in today's order: `"%s: invalid source image"` (src/pixels NULL), `"%s: invalid target dimensions %u×%u"` (w or h 0), then `img2_image_alloc` (which itself logs `image_create: invalid dimensions %u×%u` for over-limit sizes, image.c:47), then `"%s: failed to create output image"`, and `"%s: stbir_resize failed"`. `img2_image_scale_resize(src, w, h, out)` = `scale_impl("image_scale_resize", ..., IMG2_FILTER_DEFAULT, out)`. `img2_image_scale_fit(src, w, h, out)` keeps its own legacy pre-checks (`image_scale_fit: invalid source image` / `invalid target dimensions`, image.c:109-116), calls `img2_fit_size`, then `scale_impl("image_scale_fit", ...)`. These lines are reachable from the CLI: with iTerm2/Kitty, `-w`/`-H` are not range-checked (cli.c:344, :352), so e.g. `-w 70000` reaches `img2_image_alloc` → `image_create: invalid dimensions ...` → `image_scale_resize: failed to create output image` (image.c:180-183) → `pipeline_scale: failed to scale frame 0`. They must stay ERROR and byte-identical. DEBUG-only logging (**[SM2]**) applies **only** to the extra validation of caller-built source images in the public `img2_scale`/`img2_scale_fit`/`img2_encode`/`img2_apply_orientation` (stride, limits), never to these legacy lines.
   - **[M7]** The cheap internal helpers switch to `img2_status_t` + out-param (names unchanged from S5; old prototypes at image.h:154/:171/:213/:229):
```c
img2_status_t img2_image_scale_fit(const img2_image_t *src, uint32_t target_width, uint32_t target_height, img2_image_t **out);
img2_status_t img2_image_scale_resize(const img2_image_t *src, uint32_t target_width, uint32_t target_height, img2_image_t **out);
img2_status_t img2_convert_rgb_to_rgba(const uint8_t *rgb, uint32_t width, uint32_t height, img2_image_t **out);
img2_status_t img2_convert_gray_to_rgba(const uint8_t *gray, uint32_t width, uint32_t height, img2_image_t **out);
```
     `*out` is set to NULL first. Codes: NULL args / zero target → `IMG2_E_ARG`; `img2_image_calc_size` failure → `IMG2_E_LIMIT`; allocation failure → `IMG2_E_NOMEM`; stbir failure → `IMG2_E_CODEC`. Every existing log line stays verbatim. Callers: pipeline.c:634/:638 (`if (img2_image_scale_fit(..., &scaled) != IMG2_OK)` with the same failure message; replaced by `img2_scale` in S7) and test_image_processing.c (26 call sites; `ASSERT(img2_image_scale_fit(src, w, h, &dst) == IMG2_OK)` / `!= IMG2_OK` for the former NULL expectations). No decoder calls the convert helpers.
   - **[SM2] `img2_fit_size` hardening.** `src_w`, `src_h`, `max_w` or `max_h` equal to 0 → `IMG2_E_ARG`, before the division in the P10 math (today a zero `src->height` would divide by zero). `img2_fit_size` itself logs nothing for zero arguments: the legacy `image_scale_fit: invalid target dimensions %u×%u` line is printed by the fit wrappers' pre-checks (**[R6]**), so it appears exactly once, as today. Post-condition: `1 <= *out_w <= max_w` and `1 <= *out_h <= max_h`. A value above max (reachable only through `roundf`) is clamped to max; a result of 0 keeps today's error text `image_scale_fit: calculated dimensions are invalid %u×%u` and returns `IMG2_E_ARG`. The golden sizing cases prove that the clamp never fires on today's inputs.
3. Decoder contract: `typedef img2_status_t (*img2_decode_fn)(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out);`. Every decoder becomes `img2_status_t img2_decode_X(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out)`. The old `image_t **` + `*frame_count` become `out->frames`/`out->count`, with `out->loop_count = 0`. On error `*out` is zeroed and partial frames are freed. Every `return NULL;` gets a code from this table, judged by the log message right before it:

   | Message pattern | Code |
   |---|---|
   | `Invalid parameters`, NULL/0 argument checks | `IMG2_E_ARG` |
   | `Failed to allocate`, `failed to allocate`, `Failed to create image` (after dims validated) | `IMG2_E_NOMEM` |
   | `exceed`, `too large`, `limit` (hard failure), dimension/pixel limit checks (P6 limits) | `IMG2_E_LIMIT` |
   | error from a third-party call (libpng/libjpeg longjmp, `Gif*`, `WebP*`, `heif_*`, `TIFF*`, `libraw_*`, `Jxl*`, resvg, `stbi_failure_reason`) | `IMG2_E_CODEC` |
   | our own parser rejects input (ico, pnm, qoi, yuv, svg parse, `too small`, `Invalid ... header/field`) | `IMG2_E_FORMAT` |
   | impossible state | `IMG2_E_INTERNAL` |

   - Static helpers may keep NULL/bool returns; the entry function maps them.
   - `img2_decode_yuv` takes `opts->yuv` and returns `IMG2_E_ARG` when it is NULL.
   - `img2_decode_jpeg`: replace the global `s_jpegqs_opts` (decoder_jpeg.c:24) with a local `jpegqs_control_t qs = { ...same initialiser... };`. If `opts && opts->jpeg_deblock == 0`, use plain `jpeg_start_decompress`/`jpeg_finish_decompress`. Otherwise (-1 or 1) take the `#ifdef IMG2_HAVE_JPEGQS` path as today; 1 without jpegqs behaves like the default.
   - `*_is_animated` keep returning `bool`.
   - **[L6]** Local `extern` decoder prototypes go away: decoder.c:25 (integrator) and decoder_raw.c:26 (lane L2) both declare `extern image_t **decode_jpeg(...)` today. They are deleted, and the files use the unconditional prototype `img2_status_t img2_decode_jpeg(const uint8_t *, size_t, const img2_decode_opts_t *, img2_frames_t *)` from `decoder.h`. S6a adds **every** decoder prototype to decoder.h unconditionally. Tests use `decoder_internal.h` (lane L1). **[P7]** Its five `extern image_t **decode_*` prototypes (decoder_internal.h:18-22: `decode_png`, `decode_jpeg`, `decode_gif`, `decode_stb`, `decode_netpbm`) are deleted. The header instead includes the library's `decoders/decoder.h`, which declares the S6 signatures (`img2_decode_png/jpeg/gif/stb/pnm`) without `extern`, replacing its `../imgcat2/core/image.h` include (:15) as rewritten by S2. The verification grep below matches `decoder_internal.h` too, because it lives under `src/tests`.
     **[R10] Complete list of local `extern` codec prototypes to delete** (all replaced by the unconditional prototypes in decoder.h/encoder.h):
     - integrator: decoder.c:20-70 (the whole `#ifdef HAVE_*`-guarded `extern image_t **decode_*` block); encoder.c:20-38 (the `extern int encode_*` block); encoder.h:147-199 (the five `extern int encode_*` declarations become plain unconditional `img2_status_t img2_encode_<x>(...)` prototypes);
     - lane L1: decoder_ico.c:54 (`extern ... decode_png`);
     - lane L2: decoder_raw.c:26;
     - lane L3: `test_encoder_{heif,jpeg,webp,png}.c:19` and `test_encoder_jxl.c:20` (they include encoder.h instead).

     Verification: `grep -rn '^extern .*\b\(img2_\)\?\(decode\|encode\)_' src/libimg2 src/tests` → empty. **[P7]** This also covers decoder_internal.h. Use the ERE form `grep -rnE '^\s*extern\s.*\b(img2_)?(decode|encode)_' src/libimg2 src/tests`.
   - **[M1b] libpng handlers (lane L1, decoder_png.c).** Add, next to the existing static-path handlers (decoder_png.c:751-774):
```c
/* Same text and newline as libpng's png_default_error/png_default_warning (vendor/libpng/pngerror.c:658-708),
 * routed through img2_log so CLI stderr is byte-identical. */
static void log_png_error(png_structp png_ptr, png_const_charp msg)
{
	img2_log(IMG2_LOG_ERROR, "libpng error: %s", msg != NULL ? msg : "undefined");
	png_longjmp(png_ptr, 1);
}
static void log_png_warning(png_structp png_ptr, png_const_charp msg)
{
	(void)png_ptr;
	img2_log(IMG2_LOG_WARN, "libpng warning: %s", msg != NULL ? msg : "(null)");
}
```
     The 5 sites decoder_png.c:89/:173/:261/:353/:498 become `png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, log_png_error, log_png_warning)`. `error_ptr` stays NULL: `png_longjmp` uses `png_ptr->jmp_buf_ptr` set by the existing `setjmp(png_jmpbuf(png_ptr))`. Do not touch :820 (static path, silent handlers). The names are `static`, so no `img2_` prefix is needed, and they are deliberately not `png_*` (libpng's namespace).
   - **[M1b] libtiff handlers (lane L2, decoder_tiff.c).** One static helper replaces the 3 `TIFFClientOpen` calls (:161, :198, :302):
```c
#if defined(TIFFLIB_AT_LEAST)
#if TIFFLIB_AT_LEAST(4, 5, 0)
#define IMG2_TIFF_HANDLE_LOG 1
#endif
#endif
#ifdef IMG2_TIFF_HANDLE_LOG
/* Reproduces unixErrorHandler/unixWarningHandler (vendor/libtiff/libtiff/tif_unix.c:367-385) byte for byte:
 * "<module>: " (if module) + [ "Warning, " ] + vformatted + "." ; the default sink adds "\n". */
static void tiff_log(img2_log_level_t lvl, const char *module, const char *prefix, const char *fmt, va_list ap)
	__attribute__((format(printf, 4, 0)));
static int log_tiff_error(TIFF *tif, void *user, const char *module, const char *fmt, va_list ap)
	__attribute__((format(printf, 4, 0)));   /* calls tiff_log(IMG2_LOG_ERROR, module, "", fmt, ap); returns 1 */
static int log_tiff_warning(TIFF *tif, void *user, const char *module, const char *fmt, va_list ap)
	__attribute__((format(printf, 4, 0)));   /* tiff_log(IMG2_LOG_WARN, module, "Warning, ", fmt, ap); returns 1 */
#endif
static TIFF *tiff_open_memory(tiff_memory_t *mem)
{
#ifdef IMG2_TIFF_HANDLE_LOG
	TIFFOpenOptions *o = TIFFOpenOptionsAlloc();
	if (o == NULL) {
		return NULL;
	}
	TIFFOpenOptionsSetErrorHandlerExtR(o, log_tiff_error, NULL);
	TIFFOpenOptionsSetWarningHandlerExtR(o, log_tiff_warning, NULL);
	TIFF *tif = TIFFClientOpenExt("memory", "r", (thandle_t)mem, tiff_read_proc, tiff_write_proc, tiff_seek_proc,
	                              tiff_close_proc, tiff_size_proc, NULL, NULL, o);
	TIFFOpenOptionsFree(o);
	return tif;
#else
	return TIFFClientOpen("memory", "r", (thandle_t)mem, tiff_read_proc, tiff_write_proc, tiff_seek_proc,
	                      tiff_close_proc, tiff_size_proc, NULL, NULL);   /* host libtiff < 4.5: default handlers */
#endif
}
```
     `tiff_log` formats `fmt`/`ap` into a 512-byte stack buffer with `va_copy`. If `n >= 512` it mallocs `n + 1` and formats again (the same strategy as `img2_log`), so messages are never truncated. It then calls `img2_log(lvl, "%s: %s%s.", module, prefix, text)`, or `img2_log(lvl, "%s%s.", prefix, text)` when `module == NULL`. The `format(printf, 4, 0)` attributes keep `-Wformat=2` (`-Wformat-nonliteral`) quiet without a pragma. Returning 1 stops `TIFFErrorExtR`/`TIFFWarningExtR` from also calling the process-global default handler (tif_error.c:110-117), and likewise `_TIFFErrorEarly`, which calls the handler with `tif == NULL` during `TIFFClientOpenExt` (tif_error.c:80-92; the handlers must not dereference `tif`, **[Q12]**). The error paths of the 3 call sites keep their current messages (e.g. `Error: Failed to open TIFF`). Before writing, check `TIFFClientOpenExt`'s exact parameter list at vendor/libtiff/libtiff/tiffio.h:551.
   - EXIF/XMP parsers return `img2_status_t`: `IMG2_OK`, or `IMG2_E_FORMAT` for malformed data, `IMG2_E_NOMEM`, `IMG2_E_ARG`. First check that every current `return` there is `0` or `-1`. Callers' `== 0`/`!= 0` tests stay valid. `img2_yuv_parse_spec` returns `img2_status_t` (`IMG2_OK`/`IMG2_E_ARG`); cli.c:249's `< 0` stays valid.
4. Const registries with accessors. `decoder.c:102-192` and `encoder.c:46-95` keep their row lists, with one column added:
```c
typedef bool (*img2_probe_fn)(const uint8_t *data, size_t len);
typedef struct img2_decoder_info { img2_mime_t mime; const char *name; } img2_decoder_info_t;   /* public in S7 */
typedef struct img2_decoder_entry { img2_decoder_info_t info; img2_decode_fn decode; img2_probe_fn is_animated; } img2_decoder_entry_t;
static const img2_decoder_entry_t s_decoders[] = { /* same rows/order/#ifdefs as today + probe column */ };
size_t img2_decoder_count(void);
const img2_decoder_info_t *img2_decoder_info(size_t index);        /* NULL if out of range */
const img2_decoder_entry_t *img2_decoder_find(img2_mime_t mime);
bool img2_can_decode(img2_mime_t mime);                             /* IMG2_MIME_YUV -> true */
bool img2_is_animated(const uint8_t *data, size_t len);            /* NULL/0/len > IMG2_MAX_FILE_SIZE -> false [SM1]; detect, find, probe, else false */
void img2_decoder_registry_init(void);                              /* now only logs the list at INFO, same text */
```
   - Probe column, matching kitty.c:44-78 / iterm2.c:35-69: GIF → `img2_gif_is_animated`; WEBP → `img2_webp_is_animated`; HEIF **and** AVIF → `img2_heif_is_animated`; PNG → `img2_png_is_animated` only under `#ifdef PNG_APNG_SUPPORTED`, else NULL; all others NULL.
   - Encoders: `typedef struct img2_encoder_info { img2_format_t format; const char *name; const char *extension; } img2_encoder_info_t;`, `img2_encoder_entry_t { img2_encoder_info_t info; img2_encode_fn encode; int default_quality; }`, `img2_encoder_count/info/find`, `img2_can_encode`.
   - Remove `img2_decoder_registry`/`_size`, `img2_encoder_registry`/`_size`, and the "not initialized" branches (decoder.c:202-205, encoder.c:106-109).
   - **[R2] Finders are silent.** Today `decoder_find_by_mime` prints `Error: No decoder found for MIME type: %s` (decoder.c:215) and `encoder_find_by_format` prints `Error: No encoder found for format: %s` (encoder.c:119-120). `img2_decoder_find`/`img2_encoder_find` are reused by `img2_can_decode`, `img2_can_encode`, `img2_is_animated` and `img2_encode`, where a miss is a normal answer, so the finders log nothing. The two legacy lines move verbatim into the dispatchers, at the same position, directly before the dispatchers' own existing lines:
     - `img2_decoder_dispatch` on a miss: `Error: No decoder found for MIME type: %s` then `Error: No decoder available for format: %s` (decoder.c:256-259 order);
     - `img2_encoder_dispatch` on a miss: `Error: No encoder found for format: %s` (with the same JPEG/PNG/HEIF/WebP/JXL/UNKNOWN name ternary, encoder.c:119) then `Error: No encoder available for format` (encoder.c:148-151 order). Both lines come from `img2_encoder_log_missing(fmt)`, introduced here and reused by `img2_encode` in S7 (**[P9]**). `img2_encode` returns before calling the dispatcher, so the lines are never printed twice.

     CLI stderr is therefore unchanged.
5. Dispatcher: `img2_status_t img2_decoder_dispatch(const img2_decode_opts_t *opts, const uint8_t *data, size_t len, img2_mime_t mime, img2_frames_t *out)`. Every log line stays verbatim. Codes:
   - bad args → `ARG`;
   - `IMG2_MIME_UNKNOWN` → `FORMAT`. **[Q6]** It takes the ordinary miss path: since no registry row has `IMG2_MIME_UNKNOWN`, it prints both legacy ERROR lines (`Error: No decoder found for MIME type: %s` + `Error: No decoder available for format: %s`, exactly what master prints for it today via decoder.c:215/:258), and only the returned code differs (`IMG2_E_FORMAT` instead of `UNSUPPORTED`). There is no early silent return;
   - no registry row → `UNSUPPORTED`;
   - decoder failure → that decoder's code;
   - frame validation failures (old decoder.c:274-308) → `FORMAT`/`LIMIT`.

   After success, if `opts && opts->max_frames > 0 && out->count > opts->max_frames`, destroy the extra frames and set `count = max_frames`.
6. Encoders: `typedef img2_status_t (*img2_encode_fn)(const img2_image_t *img, int native_quality, uint8_t **out_data, size_t *out_size);`. Each `return -1;` → `CODEC` for library failures, `NOMEM` for allocation, `ARG` for bad args. `img2_encoder_dispatch(img, fmt, native_quality, out, len)` returns `ARG` for `IMG2_FORMAT_NONE` (legacy `Error: No output format specified`, encoder.c:142-144) and `UNSUPPORTED` for formats without an encoder (legacy lines per **[R2]**).
7. `init.c`:
```c
/* [R19] init/shutdown bodies are serialised by s_init_lock; s_refs changes only under it. */
static pthread_mutex_t s_init_lock = PTHREAD_MUTEX_INITIALIZER;
static int s_refs;                       /* guarded by s_init_lock */
img2_status_t img2_init(void)
{
	pthread_mutex_lock(&s_init_lock);
	if (s_refs++ == 0) {
		img2_encoder_registry_init();   /* encoders before decoders: main.c:114-117 order */
		img2_decoder_registry_init();
	}
	pthread_mutex_unlock(&s_init_lock);
	return IMG2_OK;
}
void img2_shutdown(void)
{
	pthread_mutex_lock(&s_init_lock);
	if (s_refs > 0 && --s_refs == 0) {  /* [R5] unbalanced shutdown at 0 is ignored */
		img2_font_manager_cleanup_final(&s_refs);   /* re-checks s_refs == 0 under s_font_lock */
	}
	pthread_mutex_unlock(&s_init_lock);
}
```
   **[R19]** This replaces the round-2 lock-free CAS loop, which ran the cleanup outside any lock: a concurrent `img2_init` plus a lazy font lookup could then receive freed handles (CWE-362/416).
   - `img2_font_manager_cleanup_final(const int *refs)` (font_manager.c, internal) runs with `s_init_lock` held by the caller. It takes `s_font_lock`, re-checks `*refs == 0` inside (cheap insurance should any caller ever change), frees the cache, the interned handles (**[R4]**) and the fallback, clears `g_initialized`, then unlocks `s_font_lock`.
   - Lock order is always `s_init_lock` → `s_font_lock`. No code path takes `s_init_lock` while holding `s_font_lock`; lazy font init (S6 item 8) takes only `s_font_lock`.
   - **Documented limit:** any other `img2_*` call that runs concurrently with the *final* `img2_shutdown()` is unsupported. Handles and strings obtained earlier are invalid afterwards. Re-init after shutdown works (**[M1a]**).
   - `img2_init` and non-final `img2_shutdown` calls may race each other freely.
   - `init.c.o`'s mutable-state allow-list entry (S6 item 13) becomes `s_init_lock`, `s_refs` (guarded by `s_init_lock`).
   No API requires `img2_init` (the tables are const and fonts init lazily). `img2_init` is recommended and is what emits the INFO lists. Because of the **[R5]** guard, `img2_shutdown()` without a matching `img2_init()` is a no-op. Fonts loaded lazily without `img2_init` stay cached until the next balanced `img2_init`/`img2_shutdown` pair ends (documented).
8. Font lock (**[M1a]**: a mutex plus a flag, not `pthread_once`, so re-init after `img2_shutdown()` works; documented as a deviation): `static pthread_mutex_t s_font_lock = PTHREAD_MUTEX_INITIALIZER;` (font_manager.c).
   - Rename the old bodies to `*_locked`.
   - Entry points (`img2_font_find`, `img2_font_find_fallback`, `img2_font_manager_list_fonts`, `img2_font_face_open`; S7 adds the public `img2_font_load*` wrappers on top of `img2_font_find*`, **[H1]**) lock, call `if (!g_initialized) img2_font_manager_init_locked();`, do the work, then unlock.
   - **[R3] Lock rule (non-recursive mutex):** a function that holds `s_font_lock` calls only `*_locked`/static helpers, never a public entry point. Today `font_manager_load` calls `font_manager_load_fallback()` at font_manager.c:592, :598 and :604. In `img2_font_find_locked` these become `img2_font_find_fallback_locked()`. `img2_font_find_fallback` (public entry) = lock + lazy init + `img2_font_find_fallback_locked()` + unlock. `img2_font_face_open` reads font data under the lock only through `*_locked` helpers. The S7 public wrappers `img2_font_load*` call the locking entry points exactly once and hold no lock themselves. `test_threads` (SVG text, font cache contention) would hang on a violation; its 120 s TIMEOUT turns that into a failure.
   - **[R4] Handle ownership (orchestrator decision): handles are interned in the cache.** Today every `font_manager_load` hit mallocs a new `font_t` and `strdup`s the family (font_manager.c:608-619), and nothing frees it, so each SVG text render leaks. New rule: each cache entry embeds (or owns exactly one) `img2_font_t` handle, built the first time the entry's file is loaded (`load_font_file`), and `img2_font_find_locked` returns a pointer to that interned handle. The fallback handle (`g_fallback_font`, font_manager.c:415-421) is interned the same way. The library owns all handles. They stay valid until the last `img2_shutdown()` (`img2_font_manager_cleanup` frees them together with the cache entries, font_manager.c:448-468). There is no release API, and callers must not free them. Handle contents (family, weight, style, data, size) are identical to today's per-call copies, so SVG text output is unchanged. Documented in `<img2/font.h>` and `docs/libimg2.md` (ownership section).
   - **[SL1]** `expand_path()` (font_manager.c:75-94) uses `secure_getenv("HOME")` where available, and `getenv` otherwise. `src/libimg2/CMakeLists.txt`: `include(CheckSymbolExists)`, `set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)`, `check_symbol_exists(secure_getenv "stdlib.h" IMG2_HAVE_SECURE_GETENV)`, `unset(CMAKE_REQUIRED_DEFINITIONS)`; if found, `set_source_files_properties(text/font_manager.c PROPERTIES COMPILE_DEFINITIONS IMG2_HAVE_SECURE_GETENV)`. **[P1]** Pass only `IMG2_HAVE_SECURE_GETENV`, never `_GNU_SOURCE`: font_manager.c:6 already has `#define _GNU_SOURCE` before its includes, and a second command-line definition would be a macro redefinition that fails under `-Werror`. `_GNU_SOURCE` appears only in `CMAKE_REQUIRED_DEFINITIONS`, for the probe. In font_manager.c: `#ifdef IMG2_HAVE_SECURE_GETENV` → `secure_getenv`, `#else` → `getenv`. For a normal (non-setuid) process both return the same value, so `--fonts` output is unchanged. The font scan stays lazy: only the first `img2_font_*` call or SVG text rendering triggers it, never `img2_init()`. Check that `img2_init` does not touch the font manager.
   - `img2_font_manager_cleanup` takes the lock and clears `g_initialized`, so re-init is allowed. **[R19]** `img2_shutdown` calls it only as `img2_font_manager_cleanup_final(&s_refs)` while holding `s_init_lock` (see item 7), with lock order `s_init_lock` → `s_font_lock`.
   - Remove the lazy `font_mgr_initialized` block (decoder_nanosvg.c:264-270).
   - Font handles stay valid until `img2_shutdown` (documented).
9. CLI call sites:
   - main.c:114-117 → `img2_init();`;
   - add `img2_shutdown();` at the `cleanup:` label and on every early return after `img2_init`;
   - **[R5]** The `--fonts` branch (main.c:93-97) runs **before** that `img2_init()`. It becomes `img2_init(); img2_font_manager_list_fonts(); img2_shutdown(); return EXIT_SUCCESS;` (S7 replaces the middle call with `print_font_list()`), instead of `font_manager_init(); font_manager_list_fonts(); font_manager_cleanup();`. The log level is still WARN at that point (the INFO raise comes after the parse/validate/`--fonts` chain), so the INFO registry lists that `img2_init` emits are suppressed even with `-v --fonts`, and output is unchanged (golden `meta:--fonts`). The regular `img2_init()` at the main.c:114 position stays where it is, so `-v` runs still print the registry lists in today's order;
   - `pipeline.c`: `img2_frames_t fr = {0}; if (img2_decoder_dispatch(&dopts, buffer, size, mime, &fr) != IMG2_OK) { ...same message... }`, then `*out_frames = fr.frames; *out_frame_count = fr.count;` (the CLI keeps its `img2_image_t **` arrays until S7);
   - `file.c` and `iterm2.c` compare against `IMG2_OK`;
   - `cli.c:126-131`: `for (size_t i = 0; i < img2_decoder_count(); i++) printf("  - %s\n", img2_decoder_info(i)->name);` with no init call.
10. Tests: `decoder_internal.h` declares the new signatures. Mechanical rewrite pattern:
```c
/* before */ int n = 0; image_t **f = decode_png(d, len, &n); ASSERT(f != NULL); ... decoder_free_frames(f, n);
/* after  */ img2_frames_t fr = {0}; ASSERT(img2_decode_png(d, len, NULL, &fr) == IMG2_OK);
             int n = fr.count; img2_image_t **f = fr.frames; ... img2_frames_free(&fr);
```
    **[R9] Exact `-1` assertions (part of S6a, integrator plus the owning lanes).** 52 assertions in 16 test files compare against exactly `-1`: `ASSERT_EQUAL(-1, ...)` or `== -1`. Examples: test_image_orientation.c:353 (`ASSERT_EQUAL(-1, image_apply_orientation(...))`), test_encoder{,_jpeg,_png,_webp,_heif,_jxl}.c, test_decoder_yuv.c. Every site whose callee now returns `img2_status_t` (orientation, encoders, encoder dispatch, yuv parse, EXIF/XMP parsers) is rewritten as follows:
    - `ASSERT_EQUAL(-1, f(...))` → `ASSERT_EQUAL(IMG2_E_<code>, f(...))` when the code is determined by the S6 table (e.g. NULL argument → `IMG2_E_ARG`);
    - otherwise → `ASSERT_TRUE(f(...) < 0)`.

    Sites that test CLI functions still returning `-1` (test_cli_arguments.c, test_cli_parsing.c, test_terminal.c, test_file_render.c, test_stdin_pipe.c, test_cross_platform.c, test_animation.c, test_pipeline_end_to_end.c) stay unchanged, unless the asserted value is a library status passed through. Review each of the 52 hits (the list comes from `grep -rnE 'ASSERT_EQUAL\(-1,|== -1\b' src/tests`) and record the decision per site in `progress.md`.
    Tests that assert "registry NULL before init" now assert `img2_decoder_count() > 0`. Add status assertions: truncated JPEG → `IMG2_E_CODEC`; `img2_decoder_dispatch` with `IMG2_MIME_UNKNOWN` → `IMG2_E_FORMAT`; NULL buffer → `IMG2_E_ARG`; 70000x1 `img2_image_calc_size` → false.
11. **No-terminate (S6c).**
    - Vendor error-path audit (fix only if a path could terminate; add no new messages): libjpeg `error_exit` longjmp (P5); libpng handlers + setjmp in the static path (:820) **and** at the 5 sites routed through `log_png_error`/`log_png_warning` (APNG and the other paths); giflib error codes; libwebp return values; libheif `heif_error`; LibRaw C API codes; libjxl status. libtiff goes through the per-handle `log_tiff_error`/`log_tiff_warning` handlers (**[M1b]**). Golden TIFF and PNG cases (including `truncated.png`) prove that the routed text is byte-identical.
    - The asserts of header-only vendors are already `((void)0)` (S5). `nanosvgrast.h`'s `NSVG__assert` sites must stay commented out (check).
    - Extend `img2_symbols` with `-DFORBIDDEN=exit;_exit;_Exit;quick_exit;abort;__assert_fail;__assert_perror_fail;__assert` and with an `-UNDEBUG` object variant:
```cmake
add_library(img2_assertcheck OBJECT EXCLUDE_FROM_ALL $<TARGET_PROPERTY:img2,SOURCES>)
target_include_directories(img2_assertcheck PRIVATE $<TARGET_PROPERTY:img2,INCLUDE_DIRECTORIES>)
target_compile_definitions(img2_assertcheck PRIVATE $<TARGET_PROPERTY:img2,COMPILE_DEFINITIONS>)
target_compile_options(img2_assertcheck PRIVATE -UNDEBUG)
add_dependencies(img2_assertcheck embed_fonts)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/img2_objects_debug.txt"
     CONTENT "$<JOIN:$<TARGET_OBJECTS:img2_assertcheck>,;>")
```
      **[R13]** This block lives in `src/libimg2/CMakeLists.txt`, right after the `img2` target, not in `src/tests/`. `SOURCES` entries are relative to `src/libimg2`, and source-file properties (e.g. the `font_manager.c` `COMPILE_DEFINITIONS` from **[SL1]**, generated-file marks for `embedded_fonts.c`) are directory-scoped. Only a target in the same directory sees them. `add_custom_target(img2_assertcheck_build ALL DEPENDS img2_assertcheck)` sits in the same file and makes sure the objects exist before ctest. `src/tests/CMakeLists.txt` only passes `-DOBJ_LIST=${PROJECT_BINARY_DIR}/src/libimg2/img2_objects_debug.txt` (the `CMAKE_CURRENT_BINARY_DIR` of `src/libimg2`) to `img2_symbols`, which reads the `;` list from the file. If `$<TARGET_PROPERTY:img2,SOURCES>` yields relative paths, use the explicit source list from `src/libimg2/CMakeLists.txt` (exported as `IMG2_LIB_SOURCES` with absolute paths). If a vendor *header* pulls in `__assert_fail` under `-UNDEBUG`, define that vendor's assert macro in the TU, or put `#define NDEBUG` right before that vendor include only, and document it.
12. `test_threads.c` (`img2_add_test(test_threads SOURCES unit/test_threads.c TIMEOUT 120)` + `Threads::Threads`; **[SM3]** extended): 8 threads × 20 iterations. Each iteration decodes, in round-robin, `src/tests/data/png/goldhill.png`, `src/tests/data/jpg/DSCN0010.jpg` (the jpegqs path), `src/tests/data/anim/smoke_anim.gif` (the giflib path; fixture added in this step) and `src/tests/fixtures/svg/text_simple.svg` (font cache contention) through `img2_decoder_dispatch`. It then scales the decoded frame 0 with `img2_image_scale_resize` to 64x64 and encodes it with `img2_encoder_dispatch(..., IMG2_FORMAT_PNG, 6, ...)`. Meanwhile one thread toggles `img2_set_log_callback` between two counting callbacks. Every call must return `IMG2_OK`, and the PNG output of the same input must be identical across threads (compare against a single-threaded reference made first). Optional, not a gate: a `-fsanitize=thread` build of this test in `.claude/tmp/build-tsan`. Results go to `progress.md`.
13. **[SM3] Mutable-state audit (`img2_symbols` extension).** `img2_check_symbols.cmake` gains `-DOBJDUMP=${CMAKE_OBJDUMP} -DMUTABLE_ALLOWLIST=${PROJECT_SOURCE_DIR}/cmake/scripts/img2_mutable_allowlist.txt`. For every object in `$<TARGET_FILE:img2>` it runs `${OBJDUMP} -t` and collects every **object** symbol (**[P4]**: only rows whose type flag is `O`; rows flagged `d` (section symbols) and `f`/`df` (file symbols) are skipped, as are symbols without a name), local or global, in a writable data section: `.data*`, `.bss*`, `.tdata*`, `.tbss*`, or `*COM*`. `.data.rel.ro*` and `.rodata*` count as immutable, which covers PIC const pointer tables such as `s_decoders`. Each such symbol must appear in the allow-list as `<object-basename> <symbol> <reason>`, otherwise the test fails and lists it. Expected allow-list entries:
    - `log.c.o`: `s_level`, `s_lock`, `s_fn`, `s_user` (atomic level / mutex-protected pair);
    - `font_manager.c.o`: `s_font_lock`, `g_font_cache`, `g_fallback_font`, `g_initialized` (under `s_font_lock`), plus function-local `static const char *..._paths[]` arrays (font_manager.c:56/:60/:64; read-only in practice; prefer making them `static const char *const` so they leave `.data`);
    - `init.c.o`: `s_init_lock`, `s_refs` (guarded by `s_init_lock`, **[R19]**);
    - **[R12]** `image.c.o`: stb_image_resize2's function-local `static` dispatch tables in `stbir__update_info_from_resize` (stb_image_resize2.h:7370-7405: `decode_simple`, `decode_alphas`, `decode_simple_scaled_or_not`, `decode_alphas_scaled_or_not`, `encode_simple`, `encode_alphas`, and the further `encode_*` tables that follow). They are non-`const` arrays of function pointers, so they land in `.data`/`.data.rel*`, but they are initialised constants that are never written. Match them by symbol-name prefix (GCC emits them as `decode_simple.<n>` etc.);
    - **[R12]** `decoder_nanosvg.c.o`: `nsvg__colors` (nanosvg.h:1321, `static` via `NSVG_DEF` since S5, non-const table; read-only lookup data);
    - `decoder_stb.c.o`: stb_image's internal flag/gamma globals `stbi__vertically_flip_on_load_global` (stb_image.h:1114), `stbi__l2h_gamma`/`stbi__l2h_scale` (:1573), `stbi__h2l_gamma_i`/`stbi__h2l_scale_i` (:1579), `stbi__unpremultiply_on_load_global`/`stbi__de_iphone_flag_global` (:4993-4994), and the thread-local `..._local` variants `stbi__vertically_flip_on_load_local` (:1124), `stbi__unpremultiply_on_load_local`/`stbi__de_iphone_flag_local` (:5010-5011) together with their `..._set` companion flags declared next to them (**[R12]**; `.tbss`, written only by the `stbi_set_*_thread` setters). Reason: "written only by stbi_set_*/gamma/scale setters, which src/libimg2 never calls". `stbi__g_failure_reason` (:966-970) **is written on every failed decode**. It is safe only because it is `STBI_THREAD_LOCAL` (auto-defined to `_Thread_local` under C11). The audit therefore requires it in `.tbss`/`.tdata`, never in `.bss`, and fails otherwise. A companion check in the same script greps `src/libimg2` for `stbi_set_|stbi_hdr_to_ldr_|stbi_ldr_to_hdr_|stbi_convert_iphone` and fails on any hit.

    Anything else, for example a leftover `static` cache in a decoder, is converted to per-call state, or reported to the integrator and documented in `docs/libimg2.md` as not thread-safe. The audit list (vendors included: libheif's implicit initialisation without `heif_init`, LibRaw's `libraw_init` (decoder_raw.c:233) per call, libjxl runners per call, OpenMP in jpegqs/LibRaw) is written to `progress.md` and summarised in the S9 threading section. The docs claim only what this audit and `test_threads` cover.

**Fan-out protocol for S6b** (single protocol, single commit):
1. The integrator writes S6a (headers, dispatcher, registries, init, font lock, CLI call sites). It is **not** committed, because the tree does not link until every decoder/encoder is converted.
2. Lanes L1-L3 edit only their files in the same working tree. After each file they run `python3 -I .claude/tmp/img2-split/tu_check.py <file>`, which must exit 0. Lanes never build, run ctest or commit.
3. When all lanes report done, the integrator does S6c, runs `gate.sh S6`, fixes leftovers and makes the single commit. If a lane cannot finish, the integrator converts the rest. The step is never committed half-converted.

**Pattern to follow**: P2, P3, P5, P6, P7.
**Verification**:
- `gate.sh S6`; `img2_symbols` (prefix + forbidden + `-UNDEBUG` variant + mutable-data allow-list) and `test_threads` pass.
- Negative check (**[SM3]**): temporarily add `static int s_probe; s_probe++;` to a decoder → `img2_symbols` fails with the unlisted symbol; revert.
- `grep -n 'png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL)' src/libimg2/decoders/decoder_png.c` → empty; `grep -n 'TIFFClientOpen(' src/libimg2/decoders/decoder_tiff.c` → only the `#else` fallback (**[M1b]**).
- `grep -rn 'extern .*img2_decode_' src/libimg2` → empty (**[L6]**).
- Negative check: temporarily add `abort();` in a static helper in `image.c` → `img2_symbols` fails; revert.
- `grep -rn 'return NULL;' src/libimg2/decoders/decoder_*.c` → only inside static helpers (review the list).
- `grep -rnE '^\s*static\s+[^c(][^;(]*=\s*\{' src/libimg2 | grep -v const` → no mutable static tables (the font cache and log state are allowed).
- Golden: 0 mismatches.

**Commit**: `refactor(lib): return img2_status_t from decode/encode chain, add img2_init/shutdown, const registries, thread-safe font cache, no-terminate guard`
**Rollback**: `git reset --hard img2/s05` (discards lane work too).

---

### Step S7: Public headers, generated `img2_config.h`, public API facade, CLI on the public API only
**Files**:
- create `include/img2/{img2,common,log,image,mime,yuv,decode,encode,metadata,font}.h`, `cmake/img2_config.h.in`, `src/libimg2/core/api.c`, `src/libimg2/img2_internal.h`, `cmake/scripts/img2_check_public_headers.cmake`, `src/tests/unit/test_public_api.c`;
- modify the internal headers so they include the public ones instead of defining types: `src/libimg2/core/{image,log,status}.h`, `src/libimg2/decoders/{decoder,magic,yuv}.h`, `src/libimg2/encoders/encoder.h`, `src/libimg2/metadata/exif_reader.h`, `src/libimg2/text/font_manager.h`;
- modify `src/libimg2/core/image.c` (`img2_scale` with filters, `img2_image_validate`/`img2_frames_validate`, public `img2_image_create` wrapper, **[SM2]/[H1]**), `src/libimg2/text/font_manager.c:683-764` (delete the listing, add `img2_font_foreach`, add the public `img2_font_load`/`img2_font_load_fallback` wrappers, **[H1]**);
- modify `src/libimg2/CMakeLists.txt` (PUBLIC include dirs, `configure_file`) and `src/imgcat2/CMakeLists.txt` (link `img2`, not `img2_testing`);
- modify the CLI: `src/imgcat2/{main.c,cli.c,cli.h,pipeline.c,pipeline.h,metadata.c,metadata.h}`, `terminal/{file.c,file.h,iterm2.c,iterm2.h,kitty.c,kitty.h}`, `ansi/escape.{c,h}`;
- modify `src/tests/CMakeLists.txt` (`img2_layering_cli`, `img2_public_headers`, `test_public_api`; tests with `LINK cli` that poke internals keep `img2_testing`).

**Dependencies**: S6
**Description**:
1. **Public header contents.** Rules for every header: an include guard `IMG2_<NAME>_H`; it includes `<img2/common.h>` (except `img2_config.h` and `common.h` itself); declarations are wrapped in `IMG2_BEGIN_DECLS`/`IMG2_END_DECLS`; there are no feature conditionals; every symbol has Doxygen. Only `<stdbool.h> <stddef.h> <stdint.h>` and sibling `<img2/...>` headers are included.

`cmake/img2_config.h.in` → `${PROJECT_BINARY_DIR}/include/img2/img2_config.h`:
```c
#ifndef IMG2_CONFIG_H
#define IMG2_CONFIG_H
#define IMG2_VERSION_MAJOR @PROJECT_VERSION_MAJOR@
#define IMG2_VERSION_MINOR @PROJECT_VERSION_MINOR@
#define IMG2_VERSION_PATCH @PROJECT_VERSION_PATCH@
#define IMG2_VERSION_STRING "@PROJECT_VERSION@"
#define IMG2_VERSION_NUMBER ((IMG2_VERSION_MAJOR << 16) | (IMG2_VERSION_MINOR << 8) | IMG2_VERSION_PATCH)
/* Informative only: what this build contains. Never needed to call the API (use img2_can_decode/encode). */
#cmakedefine01 IMG2_HAS_JPEG
#cmakedefine01 IMG2_HAS_JPEGQS
#cmakedefine01 IMG2_HAS_PNG
#cmakedefine01 IMG2_HAS_APNG
#cmakedefine01 IMG2_HAS_GIF
#cmakedefine01 IMG2_HAS_WEBP
#cmakedefine01 IMG2_HAS_HEIF
#cmakedefine01 IMG2_HAS_TIFF
#cmakedefine01 IMG2_HAS_RAW
#cmakedefine01 IMG2_HAS_JXL
#cmakedefine01 IMG2_HAS_RESVG
#cmakedefine01 IMG2_HAS_EXIF
#endif
```
   Before `configure_file`, set `IMG2_HAS_JPEG` ← `IMG2_HAVE_LIBJPEG`, `JPEGQS` ← `IMG2_HAVE_JPEGQS`, `PNG` ← `IMG2_HAVE_LIBPNG`, `APNG` ← the PNG module's APNG check result (old :507-512), `GIF` ← `IMG2_HAVE_GIFLIB`, `WEBP/HEIF/TIFF/RAW/JXL/RESVG` ← the same-named `IMG2_HAVE_*`, `EXIF` ← `IMG2_HAVE_EXIF_READER`.

`include/img2/common.h`:
```c
#ifndef IMG2_COMMON_H
#define IMG2_COMMON_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <img2/img2_config.h>
#ifdef __cplusplus
#define IMG2_BEGIN_DECLS extern "C" {
#define IMG2_END_DECLS }
#else
#define IMG2_BEGIN_DECLS
#define IMG2_END_DECLS
#endif
IMG2_BEGIN_DECLS
typedef enum img2_status {
	IMG2_OK = 0, IMG2_E_NOMEM = -1, IMG2_E_ARG = -2, IMG2_E_FORMAT = -3, IMG2_E_UNSUPPORTED = -4,
	IMG2_E_LIMIT = -5, IMG2_E_IO = -6, IMG2_E_CODEC = -7, IMG2_E_INTERNAL = -8,
	IMG2_E_NOT_FOUND = -9
} img2_status_t;                                   /* values stable; append only */
const char *img2_strerror(img2_status_t status);   /* never NULL */
void img2_free(void *ptr);                         /* frees buffers returned by img2_encode */
img2_status_t img2_init(void);                     /* refcounted; optional but recommended */
void img2_shutdown(void);                          /* last call frees the font cache; must not race other calls */
const char *img2_version_string(void);             /* == IMG2_VERSION_STRING of the built library */
IMG2_END_DECLS
#endif
```

`include/img2/log.h`:
```c
#ifndef IMG2_LOG_H
#define IMG2_LOG_H
#include <img2/common.h>
IMG2_BEGIN_DECLS
typedef enum img2_log_level { IMG2_LOG_ERROR = 0, IMG2_LOG_WARN = 1, IMG2_LOG_INFO = 2, IMG2_LOG_DEBUG = 3 } img2_log_level_t;
/** msg has no trailing newline; called on the logging thread.
 *  [R20] Contract: the callback must be thread-safe, must not call ANY img2_* function (including
 *  img2_set_log_callback/img2_set_log_level), and may be invoked while library-internal locks are held
 *  (e.g. during img2_init's registry listing, or font cache messages). Re-entry can deadlock. */
typedef void (*img2_log_fn)(img2_log_level_t level, const char *msg, void *user);
void img2_set_log_callback(img2_log_fn fn, void *user);   /* NULL restores the default sink (stderr + "\n") */
void img2_set_log_level(img2_log_level_t max_level);       /* default IMG2_LOG_ERROR */
img2_log_level_t img2_get_log_level(void);
IMG2_END_DECLS
#endif
```

`include/img2/image.h`:
```c
#ifndef IMG2_IMAGE_H
#define IMG2_IMAGE_H
#include <img2/common.h>
IMG2_BEGIN_DECLS
#define IMG2_MAX_DIMENSION 65536
#define IMG2_MAX_PIXELS 200000000UL
#define IMG2_MAX_FILE_SIZE 1073741824UL
typedef struct img2_exif img2_exif_t;    /* defined in <img2/metadata.h> */
typedef struct img2_xmp img2_xmp_t;
typedef struct img2_image {
	uint32_t width, height;
	uint32_t stride;          /* bytes per row; must be == width * 4 in 1.0 (others -> IMG2_E_ARG) */
	uint8_t *pixels;          /* RGBA8888, non-premultiplied */
	uint32_t delay_ms;        /* frame duration; 0 = unknown / not animated */
	img2_exif_t *exif;        /* NULL if none; owned by the image */
	img2_xmp_t *xmp;          /* NULL if none; owned by the image */
} img2_image_t;
typedef struct img2_frames {
	img2_image_t **frames;
	int count;
	int loop_count;           /* 0 = loop forever / unknown; N > 0 = play N times */
} img2_frames_t;
/* [P3] moved verbatim from src/libimg2/core/image.h (defined there in S6a); image.h no longer defines it */
typedef enum img2_filter {
	IMG2_FILTER_DEFAULT = 0,  /* stb_image_resize2 sRGB default (imgcat2 "lanczos") */
	IMG2_FILTER_POINT, IMG2_FILTER_BOX, IMG2_FILTER_TRIANGLE, IMG2_FILTER_CUBIC, IMG2_FILTER_MITCHELL
} img2_filter_t;
/** [H1] Zeroed RGBA pixels, stride = width*4. *out = NULL on error.
 *  IMG2_E_ARG: out NULL or width/height 0; IMG2_E_LIMIT: > IMG2_MAX_DIMENSION or > IMG2_MAX_PIXELS; IMG2_E_NOMEM. */
img2_status_t img2_image_create(uint32_t width, uint32_t height, img2_image_t **out);
void img2_image_destroy(img2_image_t *img);                          /* frees pixels, exif, xmp; NULL-safe */
void img2_frames_free(img2_frames_t *frames);                        /* destroys all frames, zeroes *frames; NULL-safe */
img2_status_t img2_scale(const img2_image_t *src, uint32_t width, uint32_t height, img2_filter_t filter, img2_image_t **out);
/** [R6] Aspect-preserving scale into max_w x max_h (img2_fit_size + scale; legacy "image_scale_fit:" messages). */
img2_status_t img2_scale_fit(const img2_image_t *src, uint32_t max_w, uint32_t max_h, img2_filter_t filter, img2_image_t **out);
img2_status_t img2_fit_size(uint32_t src_w, uint32_t src_h, uint32_t max_w, uint32_t max_h, uint32_t *out_w, uint32_t *out_h);
/** Applies (*img)->exif->orientation (no-op if none or 1); may replace *img. Does NOT reset the
 *  orientation field: call once. TIFF/HEIF/AVIF frames arrive already oriented by their codecs. */
img2_status_t img2_apply_orientation(img2_image_t **img);
static inline uint8_t *img2_image_get_pixel(const img2_image_t *img, uint32_t x, uint32_t y)
{
	if (img == NULL || img->pixels == NULL || x >= img->width || y >= img->height) {
		return NULL;
	}
	return img->pixels + (size_t)y * img->stride + (size_t)x * 4;
}
static inline bool img2_image_set_pixel(img2_image_t *img, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	uint8_t *p = img2_image_get_pixel(img, x, y);
	if (p == NULL) {
		return false;
	}
	p[0] = r; p[1] = g; p[2] = b; p[3] = a;
	return true;
}
IMG2_END_DECLS
#endif
```
   Before writing the inline helpers, check today's bodies at image.h:91-127. The bounds checks must match them, except that `width * 4` becomes `stride`, which is equal in 1.0.

`include/img2/mime.h`:
```c
#ifndef IMG2_MIME_H
#define IMG2_MIME_H
#include <img2/common.h>
IMG2_BEGIN_DECLS
typedef enum img2_mime {   /* append-only: values are ABI */
	IMG2_MIME_UNKNOWN = 0, IMG2_MIME_PNG, IMG2_MIME_JPEG, IMG2_MIME_GIF, IMG2_MIME_BMP, IMG2_MIME_TGA,
	IMG2_MIME_PSD, IMG2_MIME_HDR, IMG2_MIME_PNM, IMG2_MIME_WEBP, IMG2_MIME_HEIF, IMG2_MIME_TIFF,
	IMG2_MIME_RAW, IMG2_MIME_QOI, IMG2_MIME_ICO, IMG2_MIME_CUR, IMG2_MIME_JXL, IMG2_MIME_SVG,
	IMG2_MIME_AVIF,
	IMG2_MIME_YUV              /* headerless raw YUV: never detected, only via img2_decode_opts_t.force_mime */
} img2_mime_t;
img2_mime_t img2_detect_mime(const uint8_t *buf, size_t len);
const char *img2_mime_name(img2_mime_t mime);     /* short name, e.g. "PNG" */
const char *img2_mime_string(img2_mime_t mime);   /* MIME string, e.g. "image/png" */
IMG2_END_DECLS
#endif
```

`include/img2/yuv.h`:
```c
#ifndef IMG2_YUV_H
#define IMG2_YUV_H
#include <img2/common.h>
IMG2_BEGIN_DECLS
typedef enum img2_yuv_format {
	IMG2_YUV_FMT_NONE = 0, IMG2_YUV_FMT_I420, IMG2_YUV_FMT_YV12, IMG2_YUV_FMT_NV12, IMG2_YUV_FMT_NV21,
	IMG2_YUV_FMT_I422, IMG2_YUV_FMT_I444, IMG2_YUV_FMT_YUYV, IMG2_YUV_FMT_UYVY, IMG2_YUV_FMT_GRAY
} img2_yuv_format_t;
typedef struct img2_yuv_params {
	img2_yuv_format_t format;
	uint32_t width, height;
	size_t y_stride;           /* 0 = minimal */
	size_t uv_stride;          /* 0 = minimal (planar / semi-planar only) */
} img2_yuv_params_t;
img2_yuv_format_t img2_yuv_format_from_name(const char *name);   /* IMG2_YUV_FMT_NONE if unknown */
const char *img2_yuv_format_name(img2_yuv_format_t fmt);
/** Parses "format:width:height[:y_stride[:uv_stride]]"; on error writes a message into err. */
img2_status_t img2_yuv_parse_spec(const char *spec, img2_yuv_params_t *out, char *err, size_t err_len);
bool img2_yuv_frame_size(const img2_yuv_params_t *params, size_t *out_size);
IMG2_END_DECLS
#endif
```
   Copy the spec grammar comment verbatim from yuv.h:78-93.

`include/img2/decode.h`:
```c
#ifndef IMG2_DECODE_H
#define IMG2_DECODE_H
#include <img2/common.h>
#include <img2/image.h>
#include <img2/mime.h>
#include <img2/yuv.h>
IMG2_BEGIN_DECLS
typedef struct img2_decode_opts {
	img2_mime_t force_mime;          /* IMG2_MIME_UNKNOWN = detect from content */
	const img2_yuv_params_t *yuv;    /* required when force_mime == IMG2_MIME_YUV */
	int max_frames;                  /* <= 0: no extra limit; N > 0: keep the first N frames */
	int jpeg_deblock;                /* -1 = default (quantsmooth when built), 0 = off, 1 = on */
} img2_decode_opts_t;
#define IMG2_DECODE_OPTS_INIT { IMG2_MIME_UNKNOWN, NULL, 0, -1 }   /* NOTE: a zeroed struct means jpeg_deblock = off */
/** opts NULL = defaults. On success *out owns the frames (free with img2_frames_free); on error *out is zeroed.
 *  buf is untrusted: buf NULL / len 0 / out NULL -> IMG2_E_ARG; len > IMG2_MAX_FILE_SIZE -> IMG2_E_LIMIT;
 *  every frame is limited to IMG2_MAX_DIMENSION / IMG2_MAX_PIXELS. [SM1] */
img2_status_t img2_decode(const uint8_t *buf, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out);
bool img2_can_decode(img2_mime_t mime);
bool img2_is_animated(const uint8_t *buf, size_t len);   /* GIF, APNG, animated WebP, HEIF/AVIF sequences */
typedef struct img2_decoder_info { img2_mime_t mime; const char *name; } img2_decoder_info_t;
size_t img2_decoder_count(void);
const img2_decoder_info_t *img2_decoder_info(size_t index);   /* NULL if index >= count */
IMG2_END_DECLS
#endif
```

`include/img2/encode.h`:
```c
#ifndef IMG2_ENCODE_H
#define IMG2_ENCODE_H
#include <img2/common.h>
#include <img2/image.h>
IMG2_BEGIN_DECLS
typedef enum img2_format {
	IMG2_FORMAT_NONE = 0, IMG2_FORMAT_JPEG, IMG2_FORMAT_PNG, IMG2_FORMAT_HEIF, IMG2_FORMAT_WEBP, IMG2_FORMAT_JXL
} img2_format_t;
/** quality: -1 = format default; otherwise 0..100 with per-format meaning (kept from imgcat2 1.0):
 *  JPEG 0..100 (default 90); PNG zlib level = quality/10 clamped 0..9 (default 6, always lossless);
 *  HEIF 0..100 (default 80); WebP 0..99 lossy, 100 lossless (default 80);
 *  JXL 0..94 lossy (distance (100-q)/10), >= 95 lossless (default 80). */
typedef struct img2_encode_opts { int quality; } img2_encode_opts_t;
#define IMG2_ENCODE_OPTS_INIT { -1 }
/** Encodes in->frames[0]; *out_buf is freed with img2_free(). */
img2_status_t img2_encode(const img2_frames_t *in, img2_format_t fmt, const img2_encode_opts_t *opts,
                          uint8_t **out_buf, size_t *out_len);
bool img2_can_encode(img2_format_t fmt);
typedef struct img2_encoder_info { img2_format_t format; const char *name; const char *extension; } img2_encoder_info_t;
size_t img2_encoder_count(void);
const img2_encoder_info_t *img2_encoder_info(size_t index);
IMG2_END_DECLS
#endif
```

`include/img2/metadata.h`: `#include <img2/image.h>`. `struct img2_exif { ... };` and `struct img2_xmp { ... };` take their field lists **verbatim** from exif_reader.h:22-89 / :97-120 (names, types and order unchanged; only nested type names renamed by the S5 map). Functions:
```c
void img2_exif_init(img2_exif_t *exif);
void img2_exif_clear(img2_exif_t *exif);       /* frees contents, not the struct */
void img2_xmp_init(img2_xmp_t *xmp);
void img2_xmp_clear(img2_xmp_t *xmp);
img2_status_t img2_exif_parse_jpeg(img2_exif_t *exif, const uint8_t *jpeg, size_t len);
img2_status_t img2_exif_parse_tiff(img2_exif_t *exif, const uint8_t *tiff, size_t len);
img2_status_t img2_xmp_parse_jpeg(img2_xmp_t *xmp, const uint8_t *jpeg, size_t len);
img2_status_t img2_xmp_parse_xml(img2_xmp_t *xmp, const char *xml, size_t len);
```
   `exif_reader.h` keeps only the internal aliases `img2_exif_parse`/`img2_xmp_parse` and includes `<img2/metadata.h>`.

`include/img2/font.h`:
```c
#ifndef IMG2_FONT_H
#define IMG2_FONT_H
#include <img2/common.h>
IMG2_BEGIN_DECLS
typedef enum img2_font_weight { IMG2_FONT_WEIGHT_NORMAL = 400, IMG2_FONT_WEIGHT_BOLD = 700 } img2_font_weight_t;
typedef enum img2_font_style { IMG2_FONT_STYLE_NORMAL = 0, IMG2_FONT_STYLE_ITALIC = 1 } img2_font_style_t;
typedef struct img2_font img2_font_t;   /* opaque; [R4] interned in the library's font cache: owned by the library, never freed by
                                           callers, no release API; valid until the last img2_shutdown(). Repeated loads of the same
                                           face return the same pointer. */
/** [H1] *out = NULL on error. IMG2_E_ARG (family/out NULL), IMG2_E_NOT_FOUND, IMG2_E_NOMEM. */
img2_status_t img2_font_load(const char *family, img2_font_weight_t weight, img2_font_style_t style, img2_font_t **out);
img2_status_t img2_font_load_fallback(img2_font_t **out);            /* embedded DejaVu; IMG2_E_ARG / IMG2_E_NOT_FOUND */
const uint8_t *img2_font_get_data(const img2_font_t *font, size_t *size_out);
const char *img2_font_get_family(const img2_font_t *font);
typedef struct img2_font_info {
	const char *family;
	const char *path;                  /* "[embedded]" for built-in fonts */
	img2_font_weight_t weight;
	img2_font_style_t style;
	bool embedded;
} img2_font_info_t;
typedef int (*img2_font_visit_fn)(const img2_font_info_t *info, void *user);   /* nonzero stops */
/** Visits the font cache in cache order (lazy init). Strings valid until img2_shutdown(). */
img2_status_t img2_font_foreach(img2_font_visit_fn fn, void *user);
IMG2_END_DECLS
#endif
```
   `img2_font_foreach` copies the cache entries into a local array under the font lock, unlocks, then calls `fn` for each entry, so no callback runs while the lock is held.

`include/img2/img2.h`: an umbrella that includes `common.h`, `log.h`, `image.h`, `mime.h`, `yuv.h`, `decode.h`, `encode.h`, `metadata.h`, `font.h`.

   Internal umbrella `src/libimg2/img2_internal.h`: `<img2/img2.h>` + `core/log.h` (`img2_log`) + `core/status.h` (now just includes `<img2/common.h>`). Internal headers include the public ones; no type is defined twice.
2. `api.c` (thin wrappers). **Every boundary check added here ([SM1]/[SM2]) logs only at `IMG2_LOG_DEBUG` and keeps CLI output byte-identical.** The CLI already checks file size in `read_file_secure` and only passes library-built images, so these paths never fire for it; the golden gate proves it.
   - `img2_decode`:
     - **[SM1]** First statements, before touching `buf`: `if (out == NULL) return IMG2_E_ARG;` then `*out` zeroed; `buf == NULL || len == 0` → `IMG2_E_ARG`; `len > IMG2_MAX_FILE_SIZE` → `IMG2_E_LIMIT`.
     - `mime = (opts && opts->force_mime) ? opts->force_mime : img2_detect_mime(buf, len)`; then `img2_decoder_dispatch(opts, buf, len, mime, out)`.
     - Frame dimensions are limited by the single gate in `img2_image_alloc` (S6). No total-pixels cap (Out of Scope, documented).
   - **[SM2]** Internal `img2_status_t img2_image_validate(const img2_image_t *img)` (image.c, declared in `core/image.h`; not public). It returns `IMG2_E_ARG` unless: `img != NULL`; `img->pixels != NULL`; `0 < width, height <= IMG2_MAX_DIMENSION`; `(size_t)width * height <= IMG2_MAX_PIXELS` (reuse `img2_image_calc_size`); `stride >= width * 4`, computed in `size_t`; `(size_t)stride * height` does not overflow (`stride > SIZE_MAX / height` → ARG); **and, in 1.0, `stride == width * 4` exactly**. The decoders, encoders and stbir calls still assume packed rows, so a padded stride would be read as packed data. A larger stride also cannot be bounds-checked, because the buffer size is unknown. The `>=`/overflow checks stay in place for when padded strides are supported. Documented in `<img2/image.h>` and `docs/libimg2.md`. Also an internal `img2_frames_validate(const img2_frames_t *f)`: `f != NULL`, `count >= 0`, `frames != NULL` when `count > 0`, and every `frames[i]` passes `img2_image_validate`.
   - **[H1]** `img2_image_create(w, h, out)`: `out == NULL` → ARG; `*out = NULL`; `w == 0 || h == 0` → ARG; `!img2_image_calc_size(w, h, &n)` → LIMIT (checked here, so `img2_image_alloc`'s ERROR log is not emitted for parameter errors); then `*out = img2_image_alloc(w, h)`, NULL → NOMEM. `img2_font_load(family, weight, style, out)` / `img2_font_load_fallback(out)` (font_manager.c): `out == NULL` (or `family == NULL`) → ARG; `*out = img2_font_find(...)` / `img2_font_find_fallback()`; NULL → `IMG2_E_NOT_FOUND`.
   - `img2_encode`: requires `in && in->count >= 1 && out_buf && out_len`, then `img2_frames_validate(in)` (**[SM2]**); `*out_buf = NULL; *out_len = 0` before anything else. **[R2]** Then, before quality is touched: `fmt == IMG2_FORMAT_NONE` → `IMG2_E_ARG`, silent (DEBUG only); `entry = img2_encoder_find(fmt)` (silent finder); **[P9] (orchestrator decision)** `entry == NULL` → log the two legacy dispatcher ERROR lines, same texts and order as `img2_encoder_dispatch`'s miss path (`Error: No encoder found for format: %s` with the JPEG/PNG/HEIF/WebP/JXL/UNKNOWN name ternary, encoder.c:119-120, then `Error: No encoder available for format`, encoder.c:150), then return `IMG2_E_UNSUPPORTED`. `entry` is never dereferenced when NULL. With a CLI-requested format whose encoder is not built, stderr is therefore identical to today's `encoder_encode` miss path. Share the two lines through one internal (non-static, `img2_`-prefixed) helper `void img2_encoder_log_missing(img2_format_t fmt)`, defined in encoder.c and declared in the internal encoder.h, used by both `img2_encode` and `img2_encoder_dispatch`, so the texts cannot drift. `q = (!opts || opts->quality < 0) ? entry->default_quality : opts->quality`. Native mapping: PNG `level = q / 10` clamped 0..9 (default level 6); every other format passes q through. Then `img2_encoder_dispatch(in->frames[0], fmt, native, out_buf, out_len)`.
   - `img2_free` → `free`; `img2_version_string` → `IMG2_VERSION_STRING`.
   - `img2_apply_orientation(img2_image_t **img)`: ARG if `!img || !*img` or `img2_image_validate(*img) != IMG2_OK` (**[SM2]**); `IMG2_OK` if `(*img)->exif == NULL || (*img)->exif->orientation <= 1`; else `img2_image_orient(*img, (*img)->exif->orientation)`.
   - If the EXIF reader is not built, the metadata parse functions still exist (the source is always compiled since S6), so nothing returns UNSUPPORTED there. Only decoders skip extraction.
3. `img2_scale` and `img2_scale_fit` (image.c), both on top of the S6 `scale_impl(tag, ...)` (**[R6]**):
   - `img2_scale(src, w, h, filter, out)`: `out == NULL` → ARG; `*out = NULL`; `src == NULL || src->pixels == NULL` → legacy ERROR `image_scale_resize: invalid source image`; **[SM2]** the remaining `img2_image_validate(src)` failures (dimension limits, stride) → ARG, logged at DEBUG only, because only a caller-built image can fail them. Then `scale_impl("image_scale_resize", src, w, h, filter, out)`. Zero or over-limit target sizes therefore produce today's ERROR lines (`image_scale_resize: invalid target dimensions %u×%u`, or `image_create: invalid dimensions %u×%u` + `image_scale_resize: failed to create output image`) and return `ARG`/`LIMIT`.
   - **[R6] New public additive helper** `img2_status_t img2_scale_fit(const img2_image_t *src, uint32_t max_w, uint32_t max_h, img2_filter_t filter, img2_image_t **out)`: the same prologue with the `image_scale_fit:` tag, then exactly the S6 `img2_image_scale_fit` path (legacy pre-checks, `img2_fit_size`, `scale_impl("image_scale_fit", ...)`). It exists because the CLI's fit mode must keep the `image_scale_fit:` prefixes. Those lines are reachable via iTerm2/Kitty with unchecked `-w`/`-H` (cli.c:344, :352), and composing `img2_fit_size` + `img2_scale` in the CLI would print `image_scale_resize:` instead. It is declared in `<img2/image.h>` next to `img2_scale`, listed in FR-8's additive helpers, and covered by `test_public_api` (fit dims for 3 aspect cases; caller-built invalid image → ARG).
   - DEFAULT → exactly `stbir_resize_uint8_srgb(src->pixels, src->width, src->height, (int)src->stride, dst->pixels, w, h, (int)dst->stride, STBIR_RGBA)`; stride `width*4` equals the old `0`.
   - Other filters → `IMG2_E_UNSUPPORTED` until S10, which wires them.
   - Returns `ARG`/`NOMEM`/`CODEC`.
   - `img2_image_scale_resize`/`img2_image_scale_fit` (internal, tests) keep calling `scale_impl` directly (S6), without the public prologue, so their log text is unchanged (**[R6]**).
4. CMake:
```cmake
configure_file("${PROJECT_SOURCE_DIR}/cmake/img2_config.h.in" "${PROJECT_BINARY_DIR}/include/img2/img2_config.h")
target_include_directories(img2 PUBLIC
	"$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>" "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>"
	"$<INSTALL_INTERFACE:include>")
# src/imgcat2/CMakeLists.txt
target_link_libraries(imgcat2_cli PUBLIC img2)   # was img2_testing
target_compile_definitions(imgcat2_cli PRIVATE IMGCAT2_VERSION_STRING="${PROJECT_VERSION}")
```
5. CLI port. Every CLI file includes only `<img2/...>` from the library, plus CLI headers.
   - `cli.h`: types `img2_format_t`, `img2_yuv_params_t`.
   - `cli.c`: `VERSION_STRING` (cli.c:19) → `IMGCAT2_VERSION_STRING` (same "1.0.0"). The decoder list is printed via `img2_decoder_count()/img2_decoder_info(i)->name`.
   - New `print_font_list()` in `cli.c` reproduces P11 exactly on top of `img2_font_foreach`: collect entries, count total/embedded, build the family list by **prepending** first occurrences, same printf formats, weight/style strings copied from font_manager.c:659-678. `main.c --fonts` → `img2_init(); print_font_list(); img2_shutdown();` (the **[R5]** init stays from S6). Delete `img2_font_manager_list_fonts`.
   - `main.c`:
     - use `img2_frames_t frames` everywhere the old `frames`/`frame_count` pair was;
     - cleanup via `img2_frames_free`; scaled frames are destroyed with `img2_image_destroy`;
     - `#ifdef IMG2_HAVE_EXIF_READER` (main.c:180/:195, metadata.c/.h, pipeline.c:28-30 (the conditional `exif_reader.h` include, which becomes an unconditional `<img2/metadata.h>` via `<img2/img2.h>`, **[Q11]**), pipeline.c:513) → `#if IMG2_HAS_EXIF`;
     - `img2_mime_name`/`img2_mime_string`.
   - `pipeline.c`:
     - decode via `img2_decode(buffer, size, &dopts, &frames)` with `dopts = IMG2_DECODE_OPTS_INIT` style init, `dopts.force_mime = pipeline_input_mime(opts, buffer, size)` and `dopts.yuv`; keep the `IMG2_MIME_UNKNOWN` pre-check and its message (pipeline.c:487-505);
     - orientation via `img2_apply_orientation(&frames->frames[i])` with the same verbose/warning text and the same skip list (pipeline.c:513-531);
     - fit mode = `img2_scale_fit(frame, target.width, target.height, IMG2_FILTER_DEFAULT, &scaled)` (**[R6]**: keeps the `image_scale_fit:` lines), resize mode = `img2_scale(..., IMG2_FILTER_DEFAULT, ...)` (pipeline.c:632-639 today); failure message `pipeline_scale: failed to scale frame %d` as today;
     - `read_file_secure` uses `IMG2_MAX_FILE_SIZE`.
   - `terminal/file.c` (:144-161): `quality` keeps today's per-format selection, except PNG passes `opts->png_compression * 10`. Then `img2_frames_t one = { .frames = &img, .count = 1 }; img2_encode_opts_t eo = { .quality = quality }; img2_encode(&one, opts->output_format, &eo, &data, &size)`; free with `img2_free`.
   - `iterm2.c:102-118`: `img2_encode` with `IMG2_FORMAT_PNG` and quality `30` (level 3), or `IMG2_FORMAT_JPEG` with `opts->jpeg_quality`. **[Q9] Accepted behaviour change (orchestrator decision):** today iterm2.c calls `encode_png`/`encode_jpeg` directly. Through `img2_encode` → `img2_encoder_dispatch`, an encoder failure (in practice OOM) additionally prints the dispatcher line `Error: Encoder '%s' failed to encode image` (encoder.c:157). This path cannot be golden-tested (iTerm2 needs a TTY, see §1 Assumptions), and it only fires on encoder failure. Record it in `progress.md` and in `docs/libimg2.md` "Known limitations / CLI deltas".
   - `kitty.c`/`iterm2.c` `*_is_format_supported`: the switch (iterm2.c:35-69, kitty.c:44-78) becomes `if (img2_is_animated(data, size) && opts->animate) goto force_ansi;`. **[Q5]** Keep today's operand order (probe first, then `opts->animate`, as in kitty.c:47/:56/:64/:72 and iterm2.c:38/:47/:55/:63), so the probe still runs, and logs whatever it logs today, even when `--animate` is off. The decisions are identical because the probe column mirrors those `#if` blocks. `kitty.c:101` → `img2_image_destroy` loop / `img2_frames_free`.
   - `ansi/escape.c:66/:72` → `img2_image_get_pixel`.
6. Guards and tests:
   - Enable `img2_layering_cli` (MODE=cli) and extend MODE=lib to `include/img2`.
   - `cmake/scripts/img2_check_public_headers.cmake` (test `img2_public_headers`, label `guard`) fails if any `include/img2/*.h` contains `IMG2_HAVE_`, `HAVE_`, or any `#if`/`#ifdef`/`#ifndef`/`#elif` other than its own include guard and `__cplusplus`.
   - Self-containment: an OBJECT library `img2_header_check` (built in ALL) is compiled from generated TUs (`file(GENERATE)`, one `.c` per header that includes it **twice**, plus `int img2_hc_<name>(void){return 0;}`). It is compiled with `-std=c11 -Wall -Wextra -Werror -pedantic` and links nothing. **[Q8]** Its only include directories are `${PROJECT_SOURCE_DIR}/include` and `${PROJECT_BINARY_DIR}/include` (for the generated `img2_config.h`), set with `target_include_directories(img2_header_check PRIVATE ...)`. It does **not** link `img2`/`img2_testing` and does not inherit `src/` or vendor include dirs, so a public header that silently depends on an internal header fails to compile. It gets `add_dependencies(img2_header_check img2)` only so that `configure_file` has run.
   - `test_public_api.c` (**[L4]** registered **not** through `img2_add_test`, which would add `img2_testing` and the internal include dirs, but with a plain `add_executable(test_public_api unit/main.c unit/test_public_api.c)` + `target_link_libraries(test_public_api PRIVATE img2)` + `img2_apply_project_settings(test_public_api)` + `add_test(...)` with the same TIMEOUT/LABELS/TMPDIR `ENVIRONMENT` that `img2_add_test` sets; through S8 it also calls `img2_static_executable` like the other tests. It includes only `<img2/img2.h>`):
     - init/shutdown refcount;
     - decode `png/goldhill.png` → `IMG2_OK`, `count == 1`, `stride == width*4`;
     - PNG encode → decode round trip with pixel equality;
     - `IMG2_FORMAT_NONE` → `IMG2_E_ARG`;
     - for every `img2_format_t` with `!img2_can_encode` → `IMG2_E_UNSUPPORTED`;
     - `img2_strerror` non-NULL for every code;
     - a log callback captures an error from decoding garbage bytes. **[Q6]** The garbage is a fixed 64-byte pattern: bytes `0xA5` repeated, with byte 2 = `0x00` and byte 16 = `0x07`, which defeats the TGA header heuristic in magic.c:346-357 (image type at byte 2 / pixel depth at byte 16). The test first asserts `img2_detect_mime(buf, 64) == IMG2_MIME_UNKNOWN`, then `img2_decode` → `IMG2_E_FORMAT` and the callback count >= 1;
     - `img2_frames_free(NULL)` is safe;
     - `img2_fit_size` on 3 aspect cases;
     - `img2_scale` DEFAULT dims;
     - `img2_font_foreach` visits >= 4 embedded fonts;
     - **[H1]** `img2_image_create(0, 1, &img)` → `IMG2_E_ARG` with `img == NULL`; `img2_image_create(70000, 1, &img)` → `IMG2_E_LIMIT`; `img2_image_create(4, 4, &img)` → OK, `stride == 16`; `img2_image_create(1, 1, NULL)` → ARG; `img2_font_load_fallback(&f)` → OK; `img2_font_load("no-such-family-xyz", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL, &f)` → `IMG2_OK` with the embedded fallback (`img2_font_get_family(f)` is the DejaVu family name), because today's lookup ends in `font_manager_load_fallback()` for unknown families (font_manager.c:566-600 steps 4-5); `img2_font_load(NULL, ..., &f)` → ARG;
     - **[SM1]** `img2_decode(NULL, 10, NULL, &fr)` and `img2_decode(buf, 0, NULL, &fr)` → `IMG2_E_ARG` with `fr` zeroed; `img2_decode(buf64, (size_t)IMG2_MAX_FILE_SIZE + 1, NULL, &fr)` → `IMG2_E_LIMIT` (`buf64` is a 64-byte buffer; the length check runs before any read); `img2_is_animated` with the same oversize length → false;
     - **[SM2]** caller-built images rejected with `IMG2_E_ARG` by `img2_scale`, `img2_encode` and `img2_apply_orientation`: `pixels == NULL`; `width == 0`; `width = 70000`; `stride = width * 4 - 1`; `stride = width * 4 + 4` (padded rows are unsupported in 1.0); `width = 1, height = 65536, stride = UINT32_MAX`; `img2_frames_t {frames = NULL, count = 1}`; `count = -1`. `img2_scale` to `0x5` → ARG and to `70000x1` → LIMIT. `img2_fit_size` with any zero argument → ARG, and its outputs never exceed `max_w`/`max_h` over a sweep of 1000 random aspect pairs (fixed seed). `img2_yuv_frame_size` with `width = height = 65536` and `y_stride = SIZE_MAX / 2` → false (overflow kept, decoder_yuv.c:104-114).

**Pattern to follow**: section 5 Key Design Decisions; P6 ownership (the library allocates, `img2_*` frees); P10, P11.
**Verification**:
- `gate.sh S7`; `img2_layering_cli`, `img2_public_headers`, `test_public_api` pass; `img2_header_check` builds.
- `grep -rn 'IMG2_HAVE_' src/imgcat2 include` → empty; `grep -rn '#include "\.\..*libimg2' src/imgcat2` → empty.
- Golden: 0 mismatches. `--fonts`, `--version`, `-p`/`--jxl`/`--webp` and verbose cases are the critical ones.

**Commit**: `feat(api): add public <img2/...> headers, generated img2_config.h and API; port imgcat2 CLI to the public API`
**Rollback**: `git reset --hard img2/s06`.

---

### Step S8: CMake consumer hygiene, PIC, tests linked as dynamic PIE
**Files**:
- `CMakeLists.txt` (build type/compile_commands defaults; `-O3 -g` removal);
- `cmake/Img2Options.cmake` (`IMG2_WERROR`, `IMG2_POSITION_INDEPENDENT_CODE`);
- `cmake/Img2Platform.cmake`, `cmake/Img2VendorHelpers.cmake` (vendor section: BUILD_SHARED_LIBS, policies, `-O3 -g`, NDEBUG, PIC);
- `cmake/vendor/Img2VendorZlib.cmake` (old :139-146, :159-160), `cmake/vendor/Img2VendorJpeg.cmake` (old :235, :260-273), `cmake/vendor/Img2VendorHeif.cmake` (old :680-684, :767, :802-807, :812-817, :823, :837-867), `cmake/vendor/Img2VendorTiff.cmake` (old :1047-1095, incl. `WebP_*` FORCE :1069-1070 and `CACHE INTERNAL` :1094-1095), and, for the **[L3]** knob conversion, `cmake/vendor/Img2Vendor{Png,Webp,Brotli,Jxl}.cmake` (old :492-497, :572-583, :651-653, :1266-1270, :1299-1311);
- `cmake/Img2ProjectSettings.cmake` (`-O3 -g`, **[SL2]** hardening flags), `cmake/Img2Testing.cmake` (tests dynamic), `src/libimg2/CMakeLists.txt` (PIC property), `src/imgcat2/CMakeLists.txt` (**[SL2]** RELRO/now link options on `imgcat2`).

**Dependencies**: S7
**Description**:
1. `CMAKE_BUILD_TYPE` default (old :5-7) only `if(PROJECT_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)`. FORCE stays in that branch: it is a top-level cache default, not a leak. `CMAKE_EXPORT_COMPILE_COMMANDS` only when top-level.
2. Vendor section (extend `img2_vendor_section_begin/end`; every value is saved and restored, using `unset` when the saved value is empty):
   - `set(BUILD_SHARED_LIBS OFF)` (normal variable). Delete both cache sets: old :44 (FORCE) and old :146 (non-FORCE, zlib-ng block, **[L1]**).
   - `set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)` (`option()` honours normal variables), `CMP0126 NEW`, `CMP0125 NEW` (find_* honour normal variables).
   - `set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS -O3 -g)`. The directory property is copied into targets at creation and inherited by subdirectories, so vendors compile exactly as before.
   - **[R1] NDEBUG only if missing.** Master never set NDEBUG itself; vendors got it from `CMAKE_C_FLAGS_RELWITHDEBINFO`/`CMAKE_CXX_FLAGS_RELWITHDEBINFO` (`-DNDEBUG`) through the default build type (CMakeLists.txt:5-7). An unconditional directory `NDEBUG` would add a second `-DNDEBUG` to every vendor command. So: for each enabled language `<L>` in `C;CXX`, `string(TOUPPER "${CMAKE_BUILD_TYPE}" _cfg)`. If `CMAKE_<L>_FLAGS_${_cfg}` (or `CMAKE_<L>_FLAGS`) already contains `-DNDEBUG`, add nothing. Otherwise add `$<$<COMPILE_LANGUAGE:<L>>:NDEBUG>` to the directory `COMPILE_DEFINITIONS` (decision 5: vendors are always built with NDEBUG). Multi-config generators use the per-config genex `$<$<CONFIG:Debug>:NDEBUG>` for configs whose flags lack it. With the default `RelWithDebInfo` this adds nothing, so vendor commands are unchanged.
   - `set(CMAKE_POSITION_INDEPENDENT_CODE ${IMG2_POSITION_INDEPENDENT_CODE})`. ExternalProjects (libjpeg-turbo old :235, aom old :767) already pass `-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON`; make them pass `${IMG2_POSITION_INDEPENDENT_CODE}`.

   Delete the global `add_compile_options(-O3 -g)`. `img2_apply_project_settings()` adds `target_compile_options(<tgt> PRIVATE -O3 -g)` to `img2`, `imgcat2_cli`, `imgcat2` and tests, so behaviour is byte-identical and nothing reaches consumer targets.
3. **[H3]** Replace **every** FORCE/INTERNAL cache write with a normal variable inside the vendor modules: `ZLIB_INCLUDE_DIR`, `ZLIB_LIBRARY` (old :159-160), `JPEG_INCLUDE_DIR`, `JPEG_LIBRARY` (:272-273), `WITH_LIBSHARPYUV` (:837/:839), `WebP_INCLUDE_DIR`, `WebP_LIBRARY` (:1069-1070), and the `CACHE INTERNAL` pre-seeds `HAVE_JPEGTURBO_DUAL_MODE_8`/`_12` (:1094-1095). A normal variable counts as `DEFINED`, so libtiff's `check_*` macros skip the probe exactly as with the cache entry; confirm in the configure log.
   **[L3]** Convert the non-FORCE vendor knob cache sets listed in section 3 "Build System" (zlib-ng :139-145, libpng :492-497, libwebp :572-583, brotli :651-653, libde265 :680-684, openjpeg :812-817, libheif :847-867, libtiff :1047-1075, highway :1266-1270, libjxl :1299-1311) to normal `set(<var> <value>)`. With `CMAKE_POLICY_DEFAULT_CMP0077 NEW` (above), the vendors' `option()` calls honour them and create no cache entry, so nothing leaks into an `add_subdirectory` consumer's cache. Rules:
   - The values stay identical. The **[M2]** gate (below) proves that vendor compile flags did not change.
   - If a vendor reads a knob in a way that ignores normal variables (`if(DEFINED CACHE{X})`, `$CACHE{X}`, or a `set(X ... CACHE ... FORCE)` of its own), or a vendored `find_package()` stops resolving to the vendored copy (check the configure log), keep a non-FORCE `set(... CACHE ...)` guarded by `if(NOT DEFINED CACHE{<var>})` for that one variable, and list it in `docs/libimg2.md` "Known cache entries".
   - Keep `-D...` arguments of ExternalProjects (libjpeg-turbo :236-237, aom :768-775) as they are; they do not touch our cache.
4. Guards: `if(NOT TARGET JPEG::JPEG)` wraps **both** the `add_library` and the `set_target_properties` (old :260-266). Add `if(NOT TARGET AOM::aom)` around old :802-807. Keep `unset(EXECUTABLE_OUTPUT_PATH CACHE)` (it only removes openjpeg's own leak).
5. `option(IMG2_WERROR "Treat warnings as errors" ${PROJECT_IS_TOP_LEVEL})`: drop `-Werror` from `IMGCAT2_WARNING_FLAGS` when OFF. `-Werror=format-security` from S4 (**[SM4]**) stays regardless of `IMG2_WERROR`.
5a. **[SL2] Hardening (our sources only; never vendors).** In `img2_apply_project_settings()`, for GCC/Clang: `-fstack-protector-strong`. Add `-D_FORTIFY_SOURCE=2` only if the toolchain does not already define `_FORTIFY_SOURCE` at the project's optimisation level. Probe once in `Img2ProjectSettings.cmake` with `check_c_source_compiles("#ifndef _FORTIFY_SOURCE\n#error not predefined\n#endif\nint main(void){return 0;}" IMG2_FORTIFY_PREDEFINED)` under `CMAKE_REQUIRED_FLAGS=-O3`. Ubuntu's GCC predefines it, and redefining it would trip `-Werror`. `_FORTIFY_SOURCE` needs optimisation, and `-O3` is always on for our targets, so "only when optimising" holds. In `src/imgcat2/CMakeLists.txt`: `target_link_options(imgcat2 PRIVATE $<$<PLATFORM_ID:Linux>:-Wl,-z,relro,-z,now>)` (exe only; static non-PIE keeps working). These flags change no codec arithmetic. **CLI output must stay byte-identical; the golden gate proves it.** If a golden mismatch ever traces to these flags, drop the offending flag and record it in `progress.md` as a GAP.
6. **PIC** (`option(IMG2_POSITION_INDEPENDENT_CODE "Build libimg2 and bundled vendors as PIC (needed for PIE/shared consumers)" ON)`): `set_target_properties(img2 PROPERTIES POSITION_INDEPENDENT_CODE ${IMG2_POSITION_INDEPENDENT_CODE})`, plus the vendor section above. The `imgcat2` executable keeps `-static` without `-pie`. Add `-fno-semantic-interposition` (GCC) to `img2` and to the vendor section options **only if** step 8 below shows golden mismatches, and re-run the gate.
7. **Tests link dynamically (default PIE).** `img2_add_test` no longer calls `img2_static_executable`. The tests become the first in-tree PIE consumers. The gcc_s removal (old :1490-1496) stays: for dynamic links the driver adds libgcc_s through its own specs. If a dynamic test fails with undefined `_Unwind_*`, add `$<$<PLATFORM_ID:Linux>:gcc_s>` to test link libraries only (never to `imgcat2`) and note it. Missing OpenMP/Threads/stdc++ deps are fixed in `img2`'s usage requirements (`SYSTEM_LIBS` in `img2_feature`), never in tests.
8. **PIC fallback.** If golden mismatches appear with PIC ON even with `-fno-semantic-interposition`:
   - Rebuild with `-DIMG2_POSITION_INDEPENDENT_CODE=OFF`. 0 mismatches proves PIC is the sole cause.
   - Set the option default OFF. When OFF, the in-tree tests and (in S9) the exported config add `INTERFACE_LINK_OPTIONS $<$<PLATFORM_ID:Linux>:-no-pie>`, so executables still link. Shared-object consumers then need `-DIMG2_POSITION_INDEPENDENT_CODE=ON` (documented).
   - Record the mismatching ids in `progress.md` and flag them in the step report as a **GAP for the orchestrator** ("PIC default OFF because it changes codec output bits").

**Pattern to follow**: P14; decision 10.
**Verification**:
- `rm -rf build && gate.sh S8` (a fresh configure proves nothing relies on stale cache).
- **[H3]** `grep -rnE '[[:space:]]FORCE[[:space:]]*\)|CACHE[[:space:]]+INTERNAL' CMakeLists.txt cmake/*.cmake cmake/vendor/*.cmake` → only the top-level-guarded `CMAKE_BUILD_TYPE` line. This does not match variable names such as `HWY_FORCE_STATIC_LIBS`/`JPEGXL_FORCE_SYSTEM_*` or comments, and it does catch `CACHE INTERNAL`. `cmake/vendored-find/` is excluded; its modules are not ours to change.
- **[L3]** `grep -rnE 'set\([A-Za-z0-9_${}-]+ [^)]* CACHE ' cmake/vendor/*.cmake` → only the guarded `if(NOT DEFINED CACHE{...})` fallbacks, each listed in `docs/libimg2.md` "Known cache entries".
- **[M2] Vendor compile-flag gate.** Save `build/compile_commands.json` from `img2/s07` as `.claude/tmp/img2-split/s07-compile_commands.json` before the S8 edits. After S8, `cc_vendor_diff.py s07-compile_commands.json build/compile_commands.json --allow-add-flag -fPIC --archive-members <list>` (plus `--allow-add-flag -fno-semantic-interposition` only if step 6 needed it) must exit 0. **[R1]** With `CMAKE_POSITION_INDEPENDENT_CODE ON`, vendor *executables* (tools/tests that some vendor still builds) get `-fPIE`, not `-fPIC`. Such objects never reach the fat archive. `--archive-members <list>` restricts the comparison to entries whose object path (directory + `-o`, **[Q4]**) is an archive member. All other vendor entries are listed as `SKIPPED (not in a vendored archive)` with a count and do not fail the gate. The list file `.claude/tmp/img2-split/s08-archive-members.txt` holds the absolute object paths of every vendored `*.a` under `build/` that appears on `build/src/imgcat2/CMakeFiles/imgcat2.dir/link.txt` (the per-feature archive registration only arrives in S9). Each path is read from that archive target's `CMakeFiles/<tgt>.dir/link.txt` `ar` line (**[Q4]**). **[P5]** The list must include the `png_static` members (the patched libpng built from `build/_deps/libpng-src` into `build/_deps/libpng-build`, CMakeLists.txt:447/:499/:503). Their compile entries are kept by the `_deps/*-src/` rule of `cc_vendor_diff.py`. Assert that at least one `png_static` object was compared, not skipped. A duplicate `-DNDEBUG` in any compared entry is a failure. `-O3 -g`/`-DNDEBUG` and every other vendor flag are unchanged, and the **[L3]** knob conversion changed no vendor flag. Also run it against `master-compile_commands.json` as a cumulative check (**[Q4]**), with `--archive-members .claude/tmp/img2-split/s08-archive-members.txt` (object paths, matched by directory + `-o` path, never basename), the S3 allowances (`--allow-drop-define` list and the **[Q2]** `--allow-drop-include ... --only-targets ...` set) plus `--allow-add-flag -fPIC`. The ExternalProject arguments (libjpeg-turbo, aom) are reviewed by reading `Img2VendorJpeg.cmake`/`Img2VendorHeif.cmake`: only the PIC value expression changed.
- **[SL2]** `grep -c 'fstack-protector-strong' build/compile_commands.json` > 0 for `src/libimg2` entries and 0 for `vendor/` entries; `grep -n 'relro' build/src/imgcat2/CMakeFiles/imgcat2.dir/link.txt` hits; `readelf -lW build/imgcat2 | grep -c GNU_RELRO` >= 1.
- `grep -rn 'CMAKE_SOURCE_DIR\|CMAKE_BINARY_DIR\|add_definitions\|^include_directories' CMakeLists.txt cmake src/*/CMakeLists.txt src/tests/CMakeLists.txt` → empty.
- `file build/imgcat2` → "statically linked"; `file build/src/tests/test_magic` → "dynamically linked" and "pie executable".
- `readelf -r build/src/libimg2/libimg2.a | grep -c R_X86_64_32S` → 0 when PIC is ON.
- `.claude/tmp/build-min`: `cmake -S . -B .claude/tmp/build-min -DIMG2_ENABLE_HEIF=OFF -DIMG2_ENABLE_JXL=OFF && cmake --build .claude/tmp/build-min -j32 && ctest --test-dir .claude/tmp/build-min -R 'test_public_api|img2_' --output-on-failure` passes (UNSUPPORTED paths exercised). Not a golden gate.
- Golden: 0 mismatches.

**Commit**: `build(cmake): stop leaking cache/FORCE settings and global flags to consumers; PIC library, PIE tests`
**Rollback**: `git reset --hard img2/s07`; `rm -rf build`.

---

### Step S9: Fat archive, CLI/tests link it, install/export, consumer tests, docs
**Files**:
- create `cmake/Img2FatArchive.cmake`, `cmake/Img2Install.cmake`, `cmake/img2Config.cmake.in`, `cmake/scripts/img2_fat_archive.cmake`, `cmake/scripts/img2_consumer_smoke.cmake`, `src/tests/consumer/CMakeLists.txt`, `src/tests/consumer/consumer.c`, `docs/libimg2.md`;
- modify every `cmake/vendor/Img2Vendor*.cmake` (complete `ARCHIVES`/`ARCHIVE_TARGETS`/`SYSTEM_LIBS` in the `img2_feature` calls), `cmake/Img2Options.cmake` (`IMG2_INSTALL`, `IMG2_FAT_ARCHIVE`), `src/libimg2/CMakeLists.txt` (`img2_bundled`), `src/imgcat2/CMakeLists.txt` (link the bundled target; GNUInstallDirs), `cmake/Img2Testing.cmake` (`img2_testing` links the bundled target), top-level `CMakeLists.txt` (includes), `src/tests/CMakeLists.txt`, `README.md`.

**Dependencies**: S8
**Description**:
1. Options: `option(IMG2_INSTALL "Generate install rules for img2" ${PROJECT_IS_TOP_LEVEL})`; `option(IMG2_FAT_ARCHIVE "Merge img2 + vendored archives into one libimg2.a" ${IMG2_INSTALL})`.
2. **Archive registration.** Each vendor module lists the archives it contributes in its `img2_feature(... ARCHIVES ... ARCHIVE_TARGETS ...)` call, as `$<TARGET_FILE:t>` or absolute paths (ExternalProject outputs). They are stored per feature in `IMG2_<X>_ARCHIVES` (**[R8]**), so module include order cannot change the merge order. The build-order targets go into `ARCHIVE_TARGETS`. Vendors that are not features (lcms2, brotli and the other `IMGCAT2_VENDORED_TRAILING_LIBS` providers, CMakeLists.txt:1461-1477) call `img2_register_archives(ARCHIVES ... ARCHIVE_TARGETS ...)` (S3 helper) from `cmake/Img2LinkOrder.cmake`, in trailing-lib order. The ordered list is computed once in `Img2FatArchive.cmake`:
```cmake
set(IMG2_VENDORED_ARCHIVES "")                         # [R8] == today's link order
foreach(_f IN LISTS IMG2_LINK_ORDER)
	if(IMG2_HAVE_${_f})
		list(APPEND IMG2_VENDORED_ARCHIVES ${IMG2_${_f}_ARCHIVES})
	endif()
endforeach()
list(APPEND IMG2_VENDORED_ARCHIVES ${IMG2_TRAILING_ARCHIVES})
```
   (This loop is the first part of the `img2_add_fat_archive` macro in item 4, **[P6]**.) The expected set: zlib-ng, libjpeg-turbo (`${LIBJPEG_TURBO_LIBRARY}`, target `libjpeg-turbo-ext`), `jpegqs`, png static, `vendor_giflib`, `webp`, `webpdemux`, `sharpyuv`, `vendor_lcms2`, brotli enc/dec/common, libde265, `vendor_kvazaar`, aom (ExternalProject path, `aom-ext`), openjp2, libheif, tiff, LibRaw, hwy, libjxl (+ `jxl_cms`/`jxl_threads` if linked). The authoritative cross-check is every `*.a` path inside `${PROJECT_BINARY_DIR}` on `build/src/imgcat2/CMakeFiles/imgcat2.dir/link.txt` at `img2/s08`: the registered set must equal it. Host libraries (`IMG2_USE_SYSTEM_*`, resvg) go to `SYSTEM_LIBS` instead, with a `message(WARNING)` that the installed package depends on host libraries. `SYSTEM_LIBS` also carries Threads, m, OpenMP (Linux: link flag `-fopenmp` as old :348-366 does, for static libgomp in the CLI) and stdc++.
3. **Duplicate-symbol pre-flight.** Before writing the MRI order:
   - Run `.claude/tmp/img2-split/dupsyms.sh $(cd build/src/imgcat2 && grep -o '[^ ]*\.a' CMakeFiles/imgcat2.dir/link.txt | xargs -n1 realpath) > .claude/tmp/img2-split/logs/S9-dupsyms.txt` (the archive paths in `link.txt` are relative to `build/src/imgcat2`).
   - For every strong symbol defined in 2+ archives, record which archive(member) wins today. Re-run the exact command from `build/src/imgcat2/CMakeFiles/imgcat2.dir/link.txt` (cwd `build/src/imgcat2`), appending `-Wl,-Map=$R/.claude/tmp/img2-split/logs/S8.map -o $R/.claude/tmp/img2-split/imgcat2.s8probe`. This writes only under `.claude/tmp`.
   - After the fat switch, do the same → `S9.map`. Each duplicate must resolve to the same archive member in both maps.
   - If one differs, reorder the MRI `ADDLIB` lines so the winner's archive comes first, and document it in `Img2FatArchive.cmake`.
4. `Img2FatArchive.cmake` — `img2_add_fat_archive()` (when `IMG2_FAT_ARCHIVE`). **[P6]** It is a **macro**, not a function, so `IMG2_VENDORED_ARCHIVES`, `IMG2_FAT_ARCHIVE_NAME`, `IMG2_FAT_ARCHIVE_FILE` (and the other **[R16]** variables computed here) land in the caller's scope. Top-level call order:
   1. after all vendor modules, `Img2LinkOrder` (which runs `img2_register_archives`) and `img2_vendor_section_end()`: `include(Img2FatArchive)` + `img2_add_fat_archive()`;
   2. then `add_subdirectory(src/libimg2)`, which inherits these variables for `img2_bundled`, and `src/imgcat2`/`src/tests`;
   3. then `include(Img2Install)`, which needs the `imgcat2` target and the variables.

   `$<TARGET_FILE:img2>` and `DEPENDS img2` may name `img2` before it is defined, because both are resolved at generate time.
```cmake
# body of macro(img2_add_fat_archive) ... endmacro(), after the IMG2_VENDORED_ARCHIVES loop from item 2
set(IMG2_FAT_ARCHIVE_NAME "${CMAKE_STATIC_LIBRARY_PREFIX}img2${CMAKE_STATIC_LIBRARY_SUFFIX}")   # [R16]
set(IMG2_FAT_ARCHIVE_FILE "${PROJECT_BINARY_DIR}/fat/${IMG2_FAT_ARCHIVE_NAME}")
file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/fat/inputs.txt"
     CONTENT "$<TARGET_FILE:img2>\n$<JOIN:${IMG2_VENDORED_ARCHIVES},\n>\n")   # [R8] order == IMG2_LINK_ORDER walk + IMG2_TRAILING_ARCHIVES
add_custom_command(OUTPUT "${IMG2_FAT_ARCHIVE_FILE}"
	COMMAND ${CMAKE_COMMAND} -DMODE=$<IF:$<PLATFORM_ID:Darwin>,libtool,mri> -DAR=${CMAKE_AR} -DRANLIB=${CMAKE_RANLIB}
	        -DINPUTS=${PROJECT_BINARY_DIR}/fat/inputs.txt -DOUTPUT=${IMG2_FAT_ARCHIVE_FILE}
	        -P ${PROJECT_SOURCE_DIR}/cmake/scripts/img2_fat_archive.cmake
	DEPENDS img2 ${IMG2_VENDORED_ARCHIVE_TARGETS} "${PROJECT_BINARY_DIR}/fat/inputs.txt" VERBATIM)
add_custom_target(img2_fat ALL DEPENDS "${IMG2_FAT_ARCHIVE_FILE}")
```
   `cmake/scripts/img2_fat_archive.cmake`:
   - read the inputs and de-duplicate them, keeping the first occurrence;
   - fail if any path matches `[+; ]` (MRI cannot quote);
   - `mri` mode: `file(REMOVE <out>)`; write `CREATE <out>` / `ADDLIB <a>`... / `SAVE` / `END` to `<out>.mri`; `execute_process(COMMAND ${AR} -M INPUT_FILE <out>.mri RESULT_VARIABLE rv ERROR_VARIABLE e)`; fail on `rv`; then `${RANLIB} <out>`;
   - `libtool` mode: `libtool -static -o <out> <inputs...>` (comment: "macOS: best effort, untested").
5. **CLI and tests link the fat archive** (`src/libimg2/CMakeLists.txt`):
```cmake
if(IMG2_FAT_ARCHIVE)
	add_library(img2_bundled STATIC IMPORTED GLOBAL)
	set_target_properties(img2_bundled PROPERTIES
		IMPORTED_LOCATION "${IMG2_FAT_ARCHIVE_FILE}"
		IMPORTED_LINK_INTERFACE_LANGUAGES "C$<$<BOOL:${CMAKE_CXX_COMPILER_LOADED}>:;CXX>"
		INTERFACE_INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/include;${PROJECT_BINARY_DIR}/include"
		INTERFACE_LINK_LIBRARIES "${IMG2_SYSTEM_LINK_LIBS}"
		INTERFACE_LINK_OPTIONS "${IMG2_INTERFACE_LINK_OPTIONS}")   # -fopenmp (Linux, if OpenMP); -no-pie if PIC OFF
	set(IMG2_LINK_TARGET img2_bundled)
else()
	set(IMG2_LINK_TARGET img2)
endif()
```
   - `imgcat2_cli` links `PUBLIC ${IMG2_LINK_TARGET}` and gets `add_dependencies(imgcat2_cli img2_fat)`.
   - `img2_testing` links `INTERFACE ${IMG2_LINK_TARGET}`; `img2_add_test` adds `add_dependencies(<t> img2_fat)` when the fat archive is on. `test_public_api` likewise.
   - If `IMPORTED_LINK_INTERFACE_LANGUAGES` does not accept a genex, compute the list in plain CMake (`C` or `C;CXX`) from `CMAKE_CXX_COMPILER_LOADED`.
   - Verify that `link.txt` still uses the C++ driver.
   - `add_subdirectory` consumers keep getting the real `img2` target through `img2::img2`.
6. `Img2Install.cmake` (only `if(IMG2_INSTALL)`), using `include(GNUInstallDirs)` and `include(CMakePackageConfigHelpers)`:
   - `install(DIRECTORY include/img2 DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT img2_development)`;
   - `install(FILES ${PROJECT_BINARY_DIR}/include/img2/img2_config.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/img2 COMPONENT img2_development)`;
   - `install(FILES ${IMG2_FAT_ARCHIVE_FILE} DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT img2_development)` (`IMG2_INSTALL` requires `IMG2_FAT_ARCHIVE`; FATAL_ERROR otherwise);
   - **[R16] Variable contract** (all set in `Img2FatArchive.cmake`/`Img2Install.cmake` before `configure_package_config_file`; one name per concept, no aliases):

     | Variable | Definition | Used by |
     |---|---|---|
     | `IMG2_FAT_ARCHIVE_NAME` | `${CMAKE_STATIC_LIBRARY_PREFIX}img2${CMAKE_STATIC_LIBRARY_SUFFIX}` | `IMG2_FAT_ARCHIVE_FILE` (= `${PROJECT_BINARY_DIR}/fat/${IMG2_FAT_ARCHIVE_NAME}`), `IMG2_INSTALL_ARCHIVE` |
     | `IMG2_SYSTEM_LINK_LIBS` | accumulated by `img2_feature(... SYSTEM_LIBS ...)` (S3): Threads, m, OpenMP, stdc++ and host libraries, as CMake targets/names | in-tree `img2_bundled` `INTERFACE_LINK_LIBRARIES` |
     | `IMG2_NEED_OPENMP` | `1` if `JPEGQS_OPENMP STREQUAL "OpenMP"` (CMakeLists.txt:367) or the LibRaw module linked OpenMP (old :1182), else `0` | config `find_dependency(OpenMP)` |
     | `IMG2_HOST_LINK_LIBS` | `IMG2_SYSTEM_LINK_LIBS` minus `Threads::Threads`, `OpenMP::*`, `m`, `stdc++`/`c++` (the config re-adds those portably), with imported targets flattened to their `IMPORTED_LOCATION`/link names; host libraries only | config `INTERFACE_LINK_LIBRARIES` |
     | `IMG2_INTERFACE_LINK_OPTIONS` | Linux + OpenMP via jpegqs: `-fopenmp` (the old :358 flag form); plus `$<$<PLATFORM_ID:Linux>:-no-pie>` if PIC is OFF | in-tree `img2_bundled` `INTERFACE_LINK_OPTIONS` |
     | `IMG2_CONFIG_LINK_OPTIONS` | only the `-no-pie` part of `IMG2_INTERFACE_LINK_OPTIONS` (the config gets OpenMP through `OpenMP::OpenMP_C`) | config `INTERFACE_LINK_OPTIONS` |
     | `IMG2_FEATURES` | the `IMG2_ALL_FEATURES` entries with `IMG2_HAVE_<X>` ON, `;`-joined | config `img2_FEATURES` (informative) |

     `set(IMG2_INSTALL_ARCHIVE "${CMAKE_INSTALL_LIBDIR}/${IMG2_FAT_ARCHIVE_NAME}")` (relative), then `configure_package_config_file(cmake/img2Config.cmake.in ${PROJECT_BINARY_DIR}/img2Config.cmake INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/img2 PATH_VARS CMAKE_INSTALL_INCLUDEDIR IMG2_INSTALL_ARCHIVE)` (**[SL3]**: `PATH_VARS` produces `@PACKAGE_...@` values relative to `PACKAGE_PREFIX_DIR`, so the package is relocatable). Assert at configure time that `IMG2_HOST_LINK_LIBS` and `IMG2_CONFIG_LINK_OPTIONS` contain no path under `${PROJECT_BINARY_DIR}` or `${PROJECT_SOURCE_DIR}` (`message(FATAL_ERROR)` otherwise);
   - `write_basic_package_version_file(${PROJECT_BINARY_DIR}/img2ConfigVersion.cmake VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)`; install both to `${CMAKE_INSTALL_LIBDIR}/cmake/img2`;
   - CLI: `install(TARGETS imgcat2 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT imgcat2)` replaces the S3 rule.
7. `cmake/img2Config.cmake.in`:
```cmake
@PACKAGE_INIT@
# [SL3] relocatable: every path derives from PACKAGE_PREFIX_DIR; set_and_check fails early if the files are missing.
set_and_check(img2_INCLUDE_DIR "@PACKAGE_CMAKE_INSTALL_INCLUDEDIR@")
set_and_check(img2_LIBRARY "@PACKAGE_IMG2_INSTALL_ARCHIVE@")
include(CMakeFindDependencyMacro)
find_dependency(Threads)
set(_img2_need_openmp @IMG2_NEED_OPENMP@)
if(_img2_need_openmp)
	find_dependency(OpenMP COMPONENTS C)
endif()
if(NOT TARGET img2::img2)
	add_library(img2::img2 STATIC IMPORTED)
	set(_img2_deps Threads::Threads)
	if(_img2_need_openmp)
		list(APPEND _img2_deps OpenMP::OpenMP_C)
	endif()
	list(APPEND _img2_deps m $<IF:$<PLATFORM_ID:Darwin>,c++,stdc++> @IMG2_HOST_LINK_LIBS@)
	set_target_properties(img2::img2 PROPERTIES
		IMPORTED_LOCATION "${img2_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${img2_INCLUDE_DIR}"
		INTERFACE_LINK_LIBRARIES "${_img2_deps}"
		INTERFACE_LINK_OPTIONS "@IMG2_CONFIG_LINK_OPTIONS@")   # empty, or $<$<PLATFORM_ID:Linux>:-no-pie> if PIC OFF
endif()
set(img2_FEATURES "@IMG2_FEATURES@")
check_required_components(img2)
```
   `IMG2_NEED_OPENMP`, `IMG2_HOST_LINK_LIBS`, `IMG2_CONFIG_LINK_OPTIONS` and `IMG2_FEATURES` are defined in the **[R16]** table in item 6.
8. Consumer project `src/tests/consumer/` (standalone; `cmake_minimum_required(VERSION 3.21)`; `project(img2_consumer LANGUAGES C)`):
   - `IMG2_CONSUMER_MODE=package` → `find_package(img2 1.0 REQUIRED CONFIG)`;
   - `subdir` → `set(IMG2_BUILD_CLI OFF) set(IMG2_BUILD_TESTS OFF) set(IMG2_INSTALL OFF)` then `add_subdirectory(${IMG2_SOURCE_DIR} img2 EXCLUDE_FROM_ALL)`;
   - `add_executable(img2_consumer consumer.c)` + `target_link_libraries(img2_consumer PRIVATE img2::img2)`. This is a normal default-PIE link.

   `consumer.c` includes only `<img2/img2.h>`. Given `argv[1]` (a PNG path), it:
   - reads the file and calls `img2_init`;
   - checks `img2_detect_mime == IMG2_MIME_PNG`;
   - `img2_decode` → OK, `count >= 1`, `stride == width*4`;
   - `img2_scale` to 16x16 with `IMG2_FILTER_DEFAULT`;
   - if `img2_can_encode(IMG2_FORMAT_JPEG)`: encode JPEG with q = -1, then `img2_free`;
   - PNG encode → decode again;
   - garbage decode → `IMG2_E_FORMAT`, with a counting log callback seeing >= 1 call. It uses the same fixed 64-byte pattern as `test_public_api` and first asserts `img2_detect_mime == IMG2_MIME_UNKNOWN` (**[Q6]**);
   - `img2_strerror` non-NULL;
   - frees everything and calls `img2_shutdown`;
   - prints `img2 consumer OK`. Any failure prints `img2_strerror` and returns 1.
9. `cmake/scripts/img2_consumer_smoke.cmake` (`-P`; args `MODE SRC BIN GENERATOR IMG`):
   - `package` mode: `cmake --install ${BIN} --prefix ${BIN}/consumer-tests/prefix --component img2_development`. **[SL3] Relocation and path-leak checks:**
     - `file(REMOVE_RECURSE ${BIN}/consumer-tests/prefix-moved)` + `file(COPY ${BIN}/consumer-tests/prefix/ DESTINATION ${BIN}/consumer-tests/prefix-moved)`, then `file(REMOVE_RECURSE ${BIN}/consumer-tests/prefix)`, so the original location no longer exists;
     - grep every installed text file (`*.cmake`, `*.h`) under `prefix-moved` for the strings `${BIN}`, `${SRC}` and `${BIN}/consumer-tests/prefix`; any hit → `message(FATAL_ERROR "build-tree path leaked into installed file ...")`;
     - configure the consumer into `${BIN}/consumer-tests/pkg` with `-DCMAKE_PREFIX_PATH=${BIN}/consumer-tests/prefix-moved`, build, run.
   - `subdir` mode: configure into the persistent `${BIN}/consumer-tests/subdir` with `-DIMG2_SOURCE_DIR=${SRC}`, build `--parallel 32`, run. Then do a **cache leak audit**: the consumer `CMakeCache.txt` must not contain `BUILD_SHARED_LIBS`, `ZLIB_LIBRARY`, `JPEG_LIBRARY`, `WITH_LIBSHARPYUV`, `WebP_LIBRARY`, `HAVE_JPEGTURBO_DUAL_MODE_*`, any of the **[L3]** knob names from S8 item 3 (e.g. `ZLIB_COMPAT`, `PNG_TESTS`, `WEBP_BUILD_CWEBP`, `ENABLE_SDL`, `ENABLE_PLUGIN_LOADING`, `HWY_FORCE_STATIC_LIBS`, `JPEGXL_FORCE_SYSTEM_HWY`) or `IMG2_*` entries forced by us. The only exceptions are the guarded entries listed in `docs/libimg2.md` "Known cache entries" and cache entries created by vendors' own `option()`/`set(CACHE)` for knobs we never set. Print the full residual list into the log.

   Tests (in `src/tests/CMakeLists.txt`, registered only when top-level):
```cmake
# [L4] TMPDIR captured at configure time (gate.sh exports it); never hard-coded.
set(_img2_consumer_tmp "$ENV{TMPDIR}")
if(_img2_consumer_tmp STREQUAL "")
	set(_img2_consumer_tmp "${PROJECT_BINARY_DIR}/test-tmp")
endif()
if(IMG2_INSTALL)
	add_test(NAME img2_consumer_find_package COMMAND ${CMAKE_COMMAND} -DMODE=package -DSRC=${PROJECT_SOURCE_DIR}
		-DBIN=${PROJECT_BINARY_DIR} -DGENERATOR=${CMAKE_GENERATOR} -DIMG=${PROJECT_SOURCE_DIR}/src/tests/data/png/rgb8_srgb.png
		-P ${PROJECT_SOURCE_DIR}/cmake/scripts/img2_consumer_smoke.cmake)
	set_tests_properties(img2_consumer_find_package PROPERTIES LABELS consumer TIMEOUT 900
		RESOURCE_LOCK img2_consumer PASS_REGULAR_EXPRESSION "img2 consumer OK"
		ENVIRONMENT "TMPDIR=${_img2_consumer_tmp}")
endif()
add_test(NAME img2_consumer_subdir COMMAND ${CMAKE_COMMAND} -DMODE=subdir ... same args ...)
set_tests_properties(img2_consumer_subdir PROPERTIES LABELS "consumer;expensive" TIMEOUT 7200
	RESOURCE_LOCK img2_consumer PASS_REGULAR_EXPRESSION "img2 consumer OK"
	ENVIRONMENT "TMPDIR=${_img2_consumer_tmp}")
``` The `expensive` label is excluded by `gate.sh` (`-LE expensive`), which makes the subdir test opt-in.
10. `docs/libimg2.md` (lane B):
    - Overview; Building and the options table (`IMG2_ENABLE_*`, `IMG2_USE_SYSTEM_*`, `IMG2_BUILD_CLI`, `IMG2_BUILD_TESTS`, `IMG2_INSTALL`, `IMG2_FAT_ARCHIVE`, `IMG2_WERROR`, `IMG2_POSITION_INDEPENDENT_CODE`);
    - Consuming (`find_package`, `add_subdirectory`, FetchContent snippets);
    - An API tour per header with a ~25-line decode/scale/encode example;
    - Ownership and memory (`img2_image_create` (status + out-param)/`img2_image_destroy`, `img2_frames_free`, `img2_exif_init/clear`, `img2_free`, font handles from `img2_font_load*` are interned in the cache, owned by the library, have no release API and stay valid until the last `img2_shutdown()` (**[R4]**); `img2_shutdown()` without a matching `img2_init()` is a no-op (**[R5]**));
    - Errors and logging (status table incl. `IMG2_E_NOT_FOUND`, levels and their mapping, default sink, legacy message prefixes, the CLI's WARN/INFO choice). It lists the **[M7]** exceptions to "status everywhere": the pointer-returning allocator/lookup functions and the bool predicates named in section 1 "Assumptions". libpng/libtiff messages arrive through the log callback with their historic text (`libpng error: ...`, `libpng warning: ...`, `<module>: ...`, `<module>: Warning, ....`) at ERROR/WARN (**[M1b]**). **[SM4] Untrusted text warning:** messages can embed strings taken from the input file (EXIF/XMP fields, SVG text, vendor messages quoting file data), which may contain terminal control/escape bytes. The default stderr sink writes them unmodified for byte compatibility. Consumers that show messages on a terminal or in logs should install a callback with `img2_set_log_callback` and sanitise there;
    - Threading model (**[SM3]**): exactly what the S6 audit and `test_threads` cover (concurrent decode of PNG/JPEG/GIF/SVG, scale, PNG encode; log reconfiguration during logging), the mutable-state allow-list with reasons, vendor notes from the audit (any vendor not cleared is named as not thread-safe), and `img2_shutdown` must not race. **[R19]** `img2_init`/`img2_shutdown` are serialised internally (`s_init_lock`), but other `img2_*` calls concurrent with the final shutdown are unsupported, and handles are invalid afterwards. **[R20]** Log callback contract (no message queue, by orchestrator decision): the callback may run while `s_init_lock` is held (the INFO registry lists printed by `img2_init`) or while `s_font_lock` is held (font-cache messages such as `Warning: Failed to load font '%s', using fallback` at font_manager.c:603 and `Error: Fallback font not available` at :628). It must therefore be thread-safe and must not call any `img2_*` function; re-entry deadlocks;
    - Limits (**[SM1]/[SM2]**): `IMG2_MAX_FILE_SIZE` enforced on `len` by `img2_decode`/`img2_is_animated` (`IMG2_E_LIMIT`), `IMG2_MAX_DIMENSION`/`IMG2_MAX_PIXELS` per frame, `max_frames`; no total-pixels-across-frames cap (rationale from section 1 "Out of Scope"); validation of caller-built `img2_image_t`/`img2_frames_t` (`stride == width*4` in 1.0) with `IMG2_E_ARG`;
    - Font discovery (**[SL1]**): the searched directories (Linux `/usr/share/fonts`, `/usr/local/share/fonts`, `~/.local/share/fonts`, `~/.fonts`; macOS `/System/Library/Fonts`, `/Library/Fonts`, `~/Library/Fonts`; Windows `C:\Windows\Fonts`), that `~` expands from `HOME` via `secure_getenv` where available (ignored in setuid/setgid processes), and that the scan is lazy (first `img2_font_*` call or SVG text), never at `img2_init`;
    - Encode quality per format and the deviation from "100 = lossless";
    - **Deviations from locked decisions**: font cache mutex + flag instead of `pthread_once` (**[M1a]**); JXL lossless from quality 95 (**[M1c]**); libtiff handlers installed per handle via `TIFFOpenOptions` instead of the process-global `TIFFSetErrorHandlerExt` (**[M1b]**); CLI default WARN / `-v` INFO as the realisation of "--silent lowers level";
    - Orientation semantics (does not reset the field; TIFF/HEIF/AVIF arrive pre-oriented);
    - Known limitations: C++ vendor aborts; vendors other than libpng/libtiff/libjpeg report only through return codes; with a host libtiff older than 4.5 its default handlers print directly to stderr; **[SI1]** the fat archive embeds the vendored libpng, libjpeg-turbo, zlib-ng, giflib, libwebp, libheif (+codecs), libtiff, LibRaw, libjxl (+highway, brotli, lcms2), so a consumer that also links its own copy of any of them gets duplicate-symbol errors or silent first-wins binding (decision 3: the consumer's problem; mitigation: link only `img2::img2` for those codecs, or build img2 with `IMG2_USE_SYSTEM_*`); **[SI2]** header-only vendor asserts (stb, nanosvg, qoi) are compiled out (`((void)0)`), which equals today's `NDEBUG` Release behaviour; **[Q9]** CLI delta: iTerm2 output on an encoder failure gains the line `Error: Encoder '<name>' failed to encode image`; **[R19]** any `img2_*` call concurrent with the final `img2_shutdown()` is unsupported, and font handles are invalid after it; only frame 0 is encoded; macOS fat archive untested; host-library packages; known cache entries; the nanosvg reversed-kerning do-not-fix note;
    - Naming and layout conventions; the guard tests;
    - **Adding a codec** checklist:
      1. `cmake/vendor/Img2Vendor<X>.cmake` ending with `img2_feature(<X> LIBS ... INCLUDE_DIRS ... ARCHIVES ... ARCHIVE_TARGETS ... SYSTEM_LIBS ...)`. Add `<X>` to `IMG2_ALL_FEATURES`, to `IMG2_MACRO_FEATURES` (if code tests `IMG2_HAVE_<X>`), its link position to `IMG2_LINK_ORDER` and its include position to `IMG2_INCLUDE_ORDER` (`cmake/Img2Features.cmake`, **[R7]/[R17]**). Add `IMG2_ENABLE_<X>` in `cmake/Img2Options.cmake`. Include the module in the top-level `CMakeLists.txt` in dependency order inside the vendor section.
      2. `src/libimg2/decoders/decoder_<x>.c` implementing `img2_status_t img2_decode_<x>(const uint8_t *, size_t, const img2_decode_opts_t *, img2_frames_t *)` (and optionally `bool img2_<x>_is_animated(...)`). Errors go through status + `img2_log`; vendor errors are trapped with setjmp/handlers and never terminate.
      3. A prototype in `src/libimg2/decoders/decoder.h` (unconditional).
      4. One row under `#ifdef IMG2_HAVE_<X>` in the table in `decoders/decoder.c` (and/or `encoders/encoder.c`).
      5. A new `IMG2_MIME_<X>` **appended** at the end of `img2_mime_t` in `include/img2/mime.h` (before nothing; values are ABI). Detection goes in `decoders/magic.c`, names in `img2_mime_name`/`img2_mime_string`.
      6. An `if(IMG2_HAVE_<X>)` source block in `src/libimg2/CMakeLists.txt`.
      7. Add `IMG2_HAS_<X>` to `cmake/img2_config.h.in` and the mapping.
      8. Tests via `img2_add_test(... REQUIRES <X>)` and golden cases.
      9. Run `img2_symbols`, `img2_layering_*` and `img2_public_headers`.
11. README: add a "Using imgcat2 as a library (libimg2)" section (`find_package(img2 1.0 REQUIRED)` + `target_link_libraries(app PRIVATE img2::img2)`, `add_subdirectory`, a short example, a link to `docs/libimg2.md`) and the build options table with the new names.

**Pattern to follow**: decisions 9/10; the link order from section 2 Constraints.
**Verification**:
- `gate.sh S9`; the ctest run includes `img2_consumer_find_package` passing. Per **[SL3]** it consumes the moved prefix and greps it for path leaks.
- Once: `ctest --test-dir build -L expensive --output-on-failure 2>&1 | tee .claude/tmp/img2-split/logs/S9-subdir.log` → `img2_consumer_subdir` passes, and the leak audit prints nothing unexpected.
- `nm -A build/fat/libimg2.a | grep -c ' T png_create_read_struct$'` >= 1 and `... ' T img2_decode$'` == 1.
- `grep -o '[^ ]*\.a' build/src/imgcat2/CMakeFiles/imgcat2.dir/link.txt` shows `fat/libimg2.a` and no vendored archive paths; `file build/imgcat2` → statically linked.
- Duplicate-symbol attribution unchanged between `S8.map` and `S9.map` (step 3).
- `cmake --install build --prefix .claude/tmp/img2-prefix` lists `include/img2/*.h` (+ `img2_config.h`), `lib/libimg2.a`, `lib/cmake/img2/img2Config{,Version}.cmake`, `bin/imgcat2`.
- Golden: 0 mismatches. This is the key check that the merged archive resolves symbols identically.

**Commit**: `feat(build): install fat libimg2.a with img2Config package; CLI and tests link it; consumer smoke tests; libimg2 docs`
**Rollback**: `git reset --hard img2/s08`; `rm -rf build`.

---

### Step S10: CLI `--interpolation` → `img2_filter_t`
**Files**: `src/libimg2/core/image.c` (`img2_scale` filter switch); `src/imgcat2/cli.c:38-39` (help), `:194`, `:328-333` (validation); `src/imgcat2/cli.h:38`; `src/imgcat2/main.c:34` (default); `src/imgcat2/pipeline.c` (scale calls); `src/tests/unit/test_cli_parsing.c`, `src/tests/unit/test_image_processing.c`; `README.md` options; `docs/libimg2.md` filter table.
**Dependencies**: S9
**Description**:
- `img2_scale` non-default filters use `stbir_resize(src->pixels, sw, sh, (int)sstride, dst->pixels, w, h, (int)dstride, STBIR_RGBA, STBIR_TYPE_UINT8_SRGB, STBIR_EDGE_CLAMP, f)` (stb_image_resize2.h:522) with this mapping:
  - POINT → `STBIR_FILTER_POINT_SAMPLE`;
  - BOX → `STBIR_FILTER_BOX`;
  - TRIANGLE → `STBIR_FILTER_TRIANGLE`;
  - CUBIC → `STBIR_FILTER_CATMULLROM`;
  - MITCHELL → `STBIR_FILTER_MITCHELL` (enum at :502-507).

  DEFAULT keeps the P10 call. The function returns `IMG2_E_CODEC` if stbir fails.
- CLI: add `img2_filter_t filter;` to `cli_options_t`, set in `validate_options`:

| CLI name | filter |
|---|---|
| `lanczos` (default) | `IMG2_FILTER_DEFAULT` (today's output, byte-identical; stb has no Lanczos, and the help says so) |
| `nearest` | `IMG2_FILTER_POINT` |
| `bilinear` | `IMG2_FILTER_TRIANGLE` |
| `cubic` | `IMG2_FILTER_CUBIC` |
| `box` (new) | `IMG2_FILTER_BOX` |
| `mitchell` (new) | `IMG2_FILTER_MITCHELL` |

  - Keep `Error: Invalid interpolation method '%s'`. The second line becomes `Valid methods: lanczos, bilinear, nearest, cubic, box, mitchell`, and the help line becomes `Available: lanczos, bilinear, nearest, cubic, box, mitchell`.
  - `pipeline_scale` passes `opts->filter` to `img2_scale` (both fit and resize modes).
- Tests: parse each name and check the enum; `img2_scale` with each filter returns OK and the right dims.
- Golden: add two cases to the matrix, with ids per the S0 scheme: `interp:-i box -w 64 png/goldhill.png` and `interp:-i mitchell -w 64 png/goldhill.png`. They are new, so the first check reports them as `NEW <id>`, which is not a mismatch. The rebase records them.

**Pattern to follow**: cli.c:190-197 option handling; P10.
**Verification**:
- **[M5]** Write the allowances to an args file (one argument per line, no shell quoting involved), then pass it through `GOLDEN_ARGS_FILE`:
```bash
printf '%s\n' --allow '^meta:--help$' --allow '^meta:-i bogus png/gray4\.png$' \
	--allow '^interp:-i (nearest|cubic|bilinear) ' > .claude/tmp/img2-split/s10-allow.txt
GOLDEN_ARGS_FILE=.claude/tmp/img2-split/s10-allow.txt .claude/tmp/img2-split/gate.sh S10
```
  Only those ids may differ, reported as `ALLOWED`. `interp:-i lanczos -w 64 png/goldhill.png` must still match. The regexes match the S0 case-id scheme exactly; `golden.py check` fails if an `--allow` regex matches no id at all, which catches a stale regex.
- Then `python3 -I .claude/tmp/img2-golden/golden.py rebase build/imgcat2` (keeps `ref-v1/`), and `gate.sh S10` again without `GOLDEN_ARGS_FILE` → 0 mismatches. Record old and new md5s of the allowed cases in `progress.md`.

**Commit**: `feat(cli): map --interpolation to img2_filter_t (adds box, mitchell)`
**Rollback**: `git reset --hard img2/s09`; restore the golden reference with `mv ref-v1 ref` in `.claude/tmp/img2-golden/`.

---

### Step S11: Fill `delay_ms` / `loop_count` in animated decoders (last step)
**Files** (fan-out lanes from the Execution notes):
- L1: `src/libimg2/decoders/decoder_gif.c` (`img2_decode_gif_animated`, ~:273-445; GCB ~:363), `src/libimg2/decoders/decoder_png.c` (APNG fcTL read ~:595-605, currently discarded; acTL);
- L2: `src/libimg2/decoders/decoder_webp.c` (~:296-427; `WebPAnimDecoderGetNext` timestamps; `WebPAnimInfo.loop_count`);
- L3: `src/libimg2/decoders/decoder_heif.c`, `decoder_avif.c` (sequence paths), `decoder_jxl.c` (~:317-606);
- integrator: create `src/tests/unit/test_animation_timing.c` and `src/tests/data/anim/{smoke_anim.png,smoke_anim.webp}` (copied from `.claude/tmp/vendor-smoke/`, a few KB each); `src/tests/data/anim/smoke_anim.gif` already exists since S6 (**[SM3]**). Modify `src/tests/CMakeLists.txt` and `docs/libimg2.md` (field semantics).

**Dependencies**: S10
**Description**:
- Convention (documented in `<img2/image.h>`): `loop_count` 0 = loop forever or unknown, N > 0 = play N times; `delay_ms` 0 = unknown or not animated. Single-frame images: `delay_ms = 0`, `loop_count = 0`.
- GIF: per frame `DGifSavedExtensionToGCB(gif, i, &gcb)` → `delay_ms = gcb.DelayTime * 10`. Loop: the `NETSCAPE2.0` application extension → `loop_count = data[1] | data[2] << 8` (0 = forever); without it `loop_count = 1`.
- APNG: `delay_ms = den == 0 ? num * 10 : (uint32_t)((num * 1000u + den / 2) / den)` (den 0 means 1/100 s); `loop_count = num_plays` from `png_get_acTL`.
- WebP: `loop_count = anim_info.loop_count`; per frame `delay_ms = timestamp - prev_timestamp` (first frame: its timestamp).
- JXL: `JxlBasicInfo.animation.{tps_numerator,tps_denominator,num_loops}`; per frame `delay_ms = duration * 1000 * tps_den / tps_num` (64-bit math); `loop_count = num_loops`.
- AVIF/HEIF: use per-image timing only if the decoder already uses the libheif sequence API. Otherwise leave 0, add a `/* TODO */` and a limitation line in `docs/libimg2.md`. Keep this lane minimal.
- The CLI does **not** consume the delays (`--fps` stays, pipeline.c:746), so output must not change.
- `test_animation_timing.c` (`img2_add_test(... LINK lib)`, includes `<img2/img2.h>`): each `anim/*` file decoded with `img2_decode` must match the facts recorded in S0 (`.claude/tmp/img2-golden/anim-facts.tsv`, **[M6]**), hard-coded into the test with a comment naming that file. The expected values are `count == 3`, every `delay_ms == 100` and `loop_count == 0`; if S0 recorded different values, the recorded values win. The GIF loop expectation follows the convention above (`NETSCAPE2.0` count 0 → 0; no extension → 1). `png/rgb8_srgb.png` must give `count == 1`, `delay_ms == 0`, `loop_count == 0`. HEIF/JXL timing is covered only if a sequence fixture exists; otherwise the lane documents the gap.

**Pattern to follow**: the S6 status contract; lanes use `tu_check.py`.
**Verification**: `gate.sh S11` (golden against the S10-rebased reference → 0 mismatches; any difference is a bug, not an allowance); `test_animation_timing` passes.
**Commit**: `feat(decoders): report per-frame delay_ms and loop_count for GIF, APNG, WebP, HEIF/AVIF and JXL`
**Rollback**: `git reset --hard img2/s10`.

---

### Execution errata (validation round 5, binding; override the step text above where they conflict)
- **[E1] S5 order.** The `font_manager.c` bullet of item 2 (`STBTT_STATIC`, `STBTT_assert`) moves into item 3: nanosvg.h still calls `stbtt_*` directly (:2875-3036) until the wrappers exist. Order: item 2 without stbtt + item 3 → green rebuild → item 1 → items 4-5. `nm_unused.py` runs only after `cmake --build build -j32` exited 0.
- **[E2] S9 item 6.** `install(FILES ${PROJECT_BINARY_DIR}/img2Config.cmake ${PROJECT_BINARY_DIR}/img2ConfigVersion.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/img2 COMPONENT img2_development)`; every other install() of the library also uses `COMPONENT img2_development`.
- **[E3] S6 test rewrites.** ctest.h has no `ASSERT(...)`; use `ASSERT_EQUAL(IMG2_OK, ...)`, `ASSERT_TRUE(... != IMG2_OK)`, `ASSERT_NOT_NULL(...)` (ctest.h:178/196/199).
- **[E4] S5 item 1.** `nm_unused.py` skips every symbol defined in `embedded_fonts.c.o` (generated file; the `embed_font.py` rename handles the prefix).
- **[E5] S6 item 12.** If S0 `omp-sensitive.txt` lists any `jpg/` case, `test_threads` gets `OMP_NUM_THREADS=1` in its ENVIRONMENT (after TMPDIR), or compares only dimensions for JPEG.
- **[E6] S6 item 11.** `add_custom_target(img2_assertcheck_build ALL)` + `add_dependencies(img2_assertcheck_build img2_assertcheck)`.
- **[E7] S2b item 4.** `mkdir -p .claude/tmp/img2-split/fontcheck` before running `tools/embed_font.py` (it does not create its output dir, embed_font.py:115-119).
- **[E8] S7/S9 docs ([R21]).** `<img2/log.h>` and `docs/libimg2.md`: after `img2_set_log_callback` returns, threads already logging may still call the previous fn/user; the host keeps `user` valid until those calls finish (or until `img2_shutdown`).

---

## 7. Critical Files

| File | Role | Action |
|---|---|---|
| `CMakeLists.txt` | top-level orchestrator (1919 → ~120 lines) | modify |
| `cmake/Img2Options.cmake` | all user options (`IMG2_*`) | create |
| `cmake/Img2Platform.cmake` | static/libc++ settings, `IMG2_EXE_LINK_FLAGS` (Threads is found at top level after the vendor section, **[Q10]**) | create |
| `cmake/Img2Tooling.cmake` | clang-tidy/format detection, format target (excludes `compat/`) | create |
| `cmake/Img2VendorHelpers.cmake` | vendor scope macros, `img2_vendor_section_begin/end`, ldflag stripping | create |
| `cmake/Img2Features.cmake` | `img2_feature()`, `img2_register_archives()`, `IMG2_ALL_FEATURES`, `IMG2_MACRO_FEATURES`, `IMG2_LINK_ORDER`, `IMG2_INCLUDE_ORDER`, macro prefix (**[R7]/[R8]/[R17]**) | create |
| `cmake/Img2LinkOrder.cmake` | trailing vendored providers (`IMGCAT2_VENDORED_TRAILING_LIBS`) | create |
| `cmake/Img2ProjectSettings.cmake` | `img2_apply_project_settings()` | create |
| `cmake/Img2Testing.cmake` | `img2_add_test()`, `img2_static_executable()` | create |
| `cmake/Img2FatArchive.cmake`, `cmake/Img2Install.cmake`, `cmake/img2Config.cmake.in` | packaging | create |
| `cmake/Img2Summary.cmake` | configure summary | create |
| `cmake/img2_config.h.in` | generated public config header | create |
| `cmake/vendor/Img2Vendor{Zlib,Jpeg,Jpegqs,Png,Gif,Webp,Lcms2,Brotli,Heif,Tiff,Raw,Jxl,HeaderOnly,Resvg}.cmake` | one module per vendor stack | create |
| `cmake/scripts/img2_check_symbols.cmake` | prefix + forbidden-call guard + mutable-data audit (**[SM3]**) | create |
| `cmake/scripts/img2_mutable_allowlist.txt` | allowed writable statics with reasons (**[SM3]**) | create |
| `cmake/vendored-find/README.md` | doc-only option rename at :14 (**[L2]**) | modify |
| `cmake/scripts/img2_check_layering.cmake` | include layering guard | create |
| `cmake/scripts/img2_check_public_headers.cmake` | public header purity guard | create |
| `cmake/scripts/img2_fat_archive.cmake` | `ar -M` / libtool merge | create |
| `cmake/scripts/img2_consumer_smoke.cmake` | consumer smoke driver + cache leak audit | create |
| `cmake/FindCompressionLib.cmake` | unused | delete |
| `include/img2/{img2,common,log,image,mime,yuv,decode,encode,metadata,font}.h` | public API | create |
| `src/libimg2/CMakeLists.txt` | `img2`, `img2_testing`, `img2_bundled`, fonts, config | create |
| `src/libimg2/img2_internal.h` | internal umbrella | create |
| `src/libimg2/core/{image.c,image.h}` | image model, scale, orientation, fit | move + modify |
| `src/libimg2/core/{log.c,log.h,status.c,status.h,init.c,api.c}` | log, status, lifecycle, facade | create |
| `src/libimg2/decoders/decoder.{c,h}` | const table + accessors + dispatcher | move + modify |
| `src/libimg2/decoders/decoder_*.c` (16) | codecs (`decoder_png.c`/`decoder_tiff.c` route vendor diagnostics through `img2_log`, **[M1b]**) | move + modify |
| `src/libimg2/decoders/{magic.c,magic.h,yuv.h}` | MIME detection (static tables), YUV | move + modify |
| `src/libimg2/decoders/{nanosvg.h,nanosvgrast.h}` | local fork: `NSVG_DEF`, font wrappers | move + modify |
| `src/libimg2/encoders/encoder*.{c,h}` | const table + dispatcher | move + modify |
| `src/libimg2/metadata/{exif_reader,xml_parser}.{c,h}` | EXIF/XMP (public types), XML (internal) | move + modify |
| `src/libimg2/text/font_manager.{c,h}` | font cache + lock + face wrappers + foreach | move + modify |
| `src/libimg2/font/*.ttf` | font sources | move |
| `src/imgcat2/font/embedded_fonts.{c,h}` | generated file in git (6.9 MB) | delete |
| `tools/embed_font.py` | font generator (img2_ names) | modify |
| `src/imgcat2/CMakeLists.txt` | `imgcat2_cli`, `imgcat2` | create |
| `src/imgcat2/{cli,pipeline,metadata,base64}.{c,h}` | CLI core (flattened) | move + modify |
| `src/imgcat2/main.c`, `src/imgcat2/terminal/*`, `src/imgcat2/ansi/*` | CLI on public API | modify |
| `src/imgcat2/compat/` | untracked, not ours | never touch |
| `src/tests/CMakeLists.txt` | `img2_add_test` registration + guards + consumers | rewrite |
| `src/tests/decoder_internal.h`, `src/tests/unit/*.c`, `src/tests/integration/*.c` | tests | modify |
| `src/tests/unit/{test_log,test_threads,test_public_api,test_animation_timing}.c` | new tests | create |
| `src/tests/consumer/{CMakeLists.txt,consumer.c}` | consumer project | create |
| `src/tests/data/anim/*` | `smoke_anim.gif` in S6 (thread test), `.png`/`.webp` in S11 | create |
| `docs/libimg2.md` | library documentation | create |
| `README.md` | option names + library section | modify |
| `.claude/tmp/img2-golden/*`, `.claude/tmp/img2-split/*` | golden harness, scripts (incl. `cc_vendor_diff.py`, `probe_anim.py`), logs (untracked) | create |

## 8. Post-Implementation Checklist

- [ ] `git log --oneline a50e93d..feature/split` shows S1-S11 (S2 as two commits) with conventional messages. Tags `img2/s00`..`img2/s11` exist locally. Nothing was pushed.
- [ ] Fresh `rm -rf build && gate.sh final` is green with `TMPDIR` exported. `ctest -L expensive` (`img2_consumer_subdir`) passed once (log `S9-subdir.log`).
- [ ] Golden: logs for S1-S9 show `golden: 0 mismatches` vs master; S10 shows only the allowed ids; S11 shows 0 vs the rebased reference. `unstable.txt`/`omp-sensitive.txt` were reported in S0.
- [ ] `img2_symbols` (prefix, forbidden calls, `-UNDEBUG` variant), `img2_layering_lib`, `img2_layering_cli`, `img2_public_headers`, `img2_header_check`, `test_log`, `test_threads`, `test_public_api`, `test_animation_timing` and `img2_consumer_find_package` all pass.
- [ ] Error handling follows the S6 status table: `IMG2_OK == 0`, errors negative, `img2_strerror` covers every code. No library path terminates the process. C++ vendor aborts are documented.
- [ ] Memory management: everything the library allocates is freed by an `img2_*` function (`img2_frames_free`, `img2_image_destroy`, `img2_exif_clear`/`img2_xmp_clear`, `img2_free`). exif/xmp are always freed by the library.
- [ ] Logging: `grep -rn 'fprintf(stderr' src/libimg2` → only commented-out lines. Nothing in the library writes to stdout (the font listing moved to the CLI). Level mapping as in section 1.
- [ ] Thread safety: no mutable static tables except the font cache (mutex) and the log state (atomic level + mutex-protected callback pair). jpegqs options are per call. The `img2_symbols` mutable-data audit passes with a reviewed allow-list. `stbi__g_failure_reason` is thread-local. `docs/libimg2.md` claims no more than the audit and `test_threads` prove (**[SM3]**).
- [ ] Constructors: the public `img2_image_create`/`img2_font_load`/`img2_font_load_fallback` return `img2_status_t` with out-params; the internal `img2_image_alloc`/`img2_font_find*` were named once in S5 (**[H1]**). Scale/convert helpers return status; the remaining pointer/bool exceptions are documented (**[M7]**).
- [ ] libpng/libtiff diagnostics go through `img2_log` with byte-identical text; no `png_create_read_struct(..., NULL, NULL, NULL)` is left (**[M1b]**).
- [ ] Round 2: finders are silent and the dispatchers print the legacy lines (**[R2]**); no font function calls a locking entry point while holding `s_font_lock` (**[R3]**); font handles are interned (**[R4]**); `img2_shutdown` at refcount 0 is a no-op and `--fonts` is wrapped in `img2_init`/`img2_shutdown` (**[R5]**); legacy `image_scale_fit:`/`image_scale_resize:`/`image_create:` ERROR lines are intact (**[R6]**); every `-1` assertion was reviewed (**[R9]**); no local codec `extern`s remain (**[R10]**).
- [ ] Round 4: no `_GNU_SOURCE` redefinition (**[P1]**); public API symbols survived the S5 static pass (**[P2]**); `img2_filter_t` lives in core/image.h from S6 and moved verbatim in S7 (**[P3]**); `img2_encode` prints the two legacy lines on a missing encoder (**[P9]**); the log-callback contract is documented in `<img2/log.h>` and `docs/libimg2.md` (**[R20]**).
- [ ] Round 3: every gate configured `RelWithDebInfo` explicitly and the cache assertion passed (**[Q1]**); the S3 include-drop allowance was limited to the listed top-level vendor targets and their object-identity proof is recorded (**[Q2]**); `img2_init`/`img2_shutdown` are serialised by `s_init_lock`, and the cleanup runs under `s_init_lock` + `s_font_lock` with a `refs == 0` re-check (**[R19]**); the iTerm2 encoder-failure delta is documented (**[Q9]**).
- [ ] Untrusted input: `img2_decode` returns `IMG2_E_ARG` for NULL/0 and `IMG2_E_LIMIT` for `len > IMG2_MAX_FILE_SIZE`; caller-built images/frames are validated by `img2_scale`/`img2_encode`/`img2_apply_orientation`; boundary checks log at DEBUG only (**[SM1]/[SM2]**).
- [ ] Hardening: `-Wformat=2 -Wformat-security -Werror=format-security`, `-fstack-protector-strong`, `_FORTIFY_SOURCE=2` (if not predefined) on our sources only; `imgcat2` links with RELRO + BIND_NOW; golden still 0 mismatches (**[SM4]/[SL2]**).
- [ ] Vendor compile flags: `cc_vendor_diff.py` passed in S3 and S8 with only the allowed differences (**[M2]**). No vendor knob leaks into a consumer cache (**[L3]**).
- [ ] The installed package is relocatable (`set_and_check`, `PACKAGE_PREFIX_DIR`) and contains no build/source-tree path (**[SL3]**).
- [ ] Public headers: no feature conditionals; self-contained; `img2_config.h` installed; the CLI includes only `<img2/...>`.
- [ ] Build: no `FORCE` except the guarded top-level `CMAKE_BUILD_TYPE`; no `add_definitions`, global `include_directories`, `CMAKE_SOURCE_DIR` or `CMAKE_BINARY_DIR`; `-static`/stack flags only on `imgcat2`; vendors still built with `-O3 -g`/`NDEBUG`; PIC status (and any fallback) recorded.
- [ ] `cmake --install build --prefix <p>` installs `lib/libimg2.a`, `include/img2/*.h` + `img2_config.h`, `lib/cmake/img2/img2Config{,Version}.cmake`, `bin/imgcat2`.
- [ ] `src/imgcat2/font/embedded_fonts.{c,h}` are gone from git; fonts are generated via `Python3::Interpreter` into the build dir.
- [ ] README options renamed (and `cmake/vendored-find/README.md:14`) and the library section added. `docs/libimg2.md` is complete: API, ownership, errors/logging (incl. the untrusted-text warning), threading, limits, font directories, quality deviation, deviations from locked decisions, known limitations (incl. **[SI1]** fat-archive duplicates and **[SI2]** asserts == NDEBUG), nanosvg kerning note, "Adding a codec".
- [ ] `src/imgcat2/compat/` and `sbre-structure-visual.svg` are untouched and untracked. `git status --porcelain -- vendor` shows only the user's ` M vendor/nanosvg`. Nothing was written to `/tmp`.
- [ ] `.claude/tmp/img2-split/progress.md` lists, per step: gate log, ctest count, golden result, commit hash, tag.
