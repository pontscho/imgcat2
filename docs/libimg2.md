# libimg2

libimg2 is the image library behind `imgcat2`: a static C11 library that decodes PNG/APNG, JPEG, GIF, BMP,
TGA, PSD, HDR, PNM, QOI, ICO/CUR, SVG, WebP, HEIF/AVIF, TIFF, camera RAW, JPEG XL and raw YUV into RGBA8888
frames, scales them, applies EXIF orientation, and encodes JPEG, PNG, HEIF, WebP and JPEG XL. Every codec
library is vendored (`vendor/`) and built from source, so one archive carries everything.

- Target: `img2::img2` (alias of the static `img2` target for `add_subdirectory`/FetchContent; an imported
  target over the installed fat `libimg2.a` for `find_package`).
- Headers: `#include <img2/img2.h>` (umbrella) or the individual `<img2/X.h>` headers.
- Version: 1.0.0 (`IMG2_VERSION_*` in `<img2/img2_config.h>`, `img2_version_string()` at run time).

## Contents

- [Building](#building)
- [Consuming](#consuming)
- [API tour](#api-tour)
- [Ownership and memory](#ownership-and-memory)
- [Errors and logging](#errors-and-logging)
- [Threading model](#threading-model)
- [Limits](#limits)
- [Font discovery](#font-discovery)
- [Encode quality](#encode-quality)
- [Orientation](#orientation)
- [Deviations from locked decisions](#deviations-from-locked-decisions)
- [Known limitations / CLI deltas](#known-limitations--cli-deltas)
- [Known cache entries](#known-cache-entries)
- [Naming, layout and guard tests](#naming-layout-and-guard-tests)
- [Adding a codec](#adding-a-codec)

## Building

Requirements: CMake >= 3.21, a C11 compiler (GCC or Clang) plus a C++ compiler for the vendored C++ codecs
(LibRaw, libheif, libjxl, highway), Python 3 (font embedding), nasm (aom), and the git submodules
(`git submodule update --init --recursive`).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build -LE expensive --output-on-failure
cmake --install build --prefix /opt/img2 --component img2_development   # library package only
cmake --install build --prefix /opt/img2 --component imgcat2            # CLI only
```

### Options

| Option | Default | Meaning |
|---|---|---|
| `IMG2_ENABLE_GIF` | ON | GIF decoding (giflib) |
| `IMG2_ENABLE_WEBP` | ON | WebP decoding/encoding (libwebp) |
| `IMG2_ENABLE_HEIF` | ON | HEIF/AVIF decoding/encoding (libheif + libde265, kvazaar, aom, OpenJPEG) |
| `IMG2_ENABLE_TIFF` | ON | TIFF decoding (libtiff) |
| `IMG2_ENABLE_RAW` | ON | Camera RAW decoding (LibRaw) |
| `IMG2_ENABLE_JXL` | ON | JPEG XL decoding/encoding (libjxl) |
| `IMG2_ENABLE_RESVG` | OFF | resvg SVG renderer (host library; preferred over nanosvg when found) |
| `IMG2_ENABLE_EXIF_READER` | ON | EXIF/XMP feature flag (the parser itself is always compiled) |
| `IMG2_ENABLE_JPEGQS` | ON | jpeg-quantsmooth JPEG deblocking (OpenMP) |
| `IMG2_USE_SYSTEM_<X>` | OFF | Use the host library instead of the vendored copy; `<X>` = `JPEG`, `PNG`, `GIF`, `WEBP`, `TIFF`, `LCMS2`, `RAW`, `HEIF`, `JXL` |
| `IMG2_BUILD_CLI` | top level | Build the `imgcat2` command line tool |
| `IMG2_BUILD_TESTS` | top level | Build the test suite and register the ctests |
| `IMG2_INSTALL` | top level | Generate install rules and the `img2Config.cmake` package (requires `IMG2_FAT_ARCHIVE`) |
| `IMG2_FAT_ARCHIVE` | `IMG2_INSTALL` | Merge `libimg2.a` and every vendored archive into `build/fat/libimg2.a`; the CLI and the tests link it |
| `IMG2_WERROR` | top level | `-Werror` for our own sources (`-Werror=format-security` stays on regardless) |
| `IMG2_POSITION_INDEPENDENT_CODE` | ON | Build libimg2 and the vendors as PIC (needed by PIE executables and shared objects) |
| `IMG2_ENABLE_CLANG_TIDY` / `IMG2_ENABLE_CLANG_FORMAT` | OFF / ON | Developer tooling (clang-tidy on our targets, `format` target) |
| `IMG2_ENABLE_STATIC_LIBCXX` | OFF | macOS: link libc++ statically (Homebrew LLVM) |
| `IMG2_STATIC_PIE` | ON | Linux, CLI builds only: link `imgcat2` with `-static-pie` (ASLR for the main image) when the toolchain supports it and `IMG2_POSITION_INDEPENDENT_CODE` is ON; otherwise plain `-static` (configure prints which) |

"top level" means `PROJECT_IS_TOP_LEVEL`: ON when img2 is the main project, OFF under `add_subdirectory`.
The vendors are always built with `-O3 -g`, `NDEBUG` and the hardening flags (`IMG2_HARDENING_FLAGS`,
`cmake/Img2Hardening.cmake`: `-fstack-protector-strong`, plus `-D_FORTIFY_SOURCE=2` unless the toolchain
predefines it; C/C++ sources only, the ExternalProject builds of libjpeg-turbo and aom get them through
`CMAKE_C_FLAGS`/`CMAKE_CXX_FLAGS`, assembler flags stay upstream's), but never with our warning flags. Our own
targets get `-O3 -g`, warnings and the same hardening flags. Nothing is set globally, so a consumer's targets see
none of these flags. `imgcat2` is linked fully static (static PIE where supported, see `IMG2_STATIC_PIE`) with
full RELRO (`-z relro -z now`).

### The fat archive

With `IMG2_FAT_ARCHIVE` (default for top-level builds) the `img2_fat` target merges `libimg2.a` and the
vendored archives, in the former static link order, into `build/fat/libimg2.a` with a GNU `ar -M` MRI script
(`cmake/scripts/img2_fat_archive.cmake`; macOS `libtool -static`, best effort and untested). Merged: zlib-ng,
libpng (+APNG patch), jpeg-quantsmooth, libjpeg-turbo, giflib, libwebp (webp, webpdemux, sharpyuv), libheif,
libde265, aom, OpenJPEG, kvazaar, libtiff, LibRaw, libjxl (+jxl_cms), highway, lcms2 and brotli. Host libraries
(`IMG2_USE_SYSTEM_*`, resvg) are not merged: they become link dependencies of the package, and configure warns
about it. The imgcat2 executable, every test and the installed package use this one archive, so the shipped
artefact is the tested one.

Duplicate-symbol check: right after every merge, `cmake/scripts/img2_check_duplicate_symbols.cmake` runs `nm`
on the fat archive and fails the build (and deletes the archive, so the next build retries) when a strong
global symbol (nm types `T`, `D`, `B`, `R`) is defined by more than one member: the linker would silently take
the first member in merge order. Known-legitimate duplicates go into
`cmake/scripts/img2_duplicate_symbols_allowlist.txt` with a comment each; the list is empty today, and an entry
that is no longer duplicated also fails the check. Skipped on Apple and when CMake found no `nm`.

Vendored-library manifest: configure writes `build/fat/vendored-libraries.txt` (`cmake/Img2VendorManifest.cmake`),
installed as `share/img2/vendored-libraries.txt` with the `img2_development` component. One tab-separated row
per `vendor/` submodule: name, whether it is compiled into this build, submodule path, the commit pinned by the
superproject (gitlink), the checked-out commit, `git describe --tags --always` (upstream version tag where the
repository has one), working-tree state (`clean`/`modified`/`unknown`), upstream URL and notes (e.g. the APNG
patch applied to libpng). It is a snapshot of configure time: re-run CMake after moving a submodule. Without a
git checkout the commits read `unknown`.

## Consuming

### find_package (installed package)

```cmake
find_package(img2 1.0 REQUIRED)
add_executable(app main.c)
target_link_libraries(app PRIVATE img2::img2)
```

`img2Config.cmake` is relocatable (every path derives from the package prefix). `img2::img2` brings the
include directory and the link dependencies Threads, OpenMP (when jpeg-quantsmooth or LibRaw use it), `m`,
the C++ runtime (`stdc++`, `c++` on macOS) and any host libraries. `img2_FEATURES` lists the built features
(informative). The installed package is the fat archive only; there is no shared library and no pkg-config
file.

### add_subdirectory / FetchContent

```cmake
set(IMG2_BUILD_CLI OFF)
set(IMG2_BUILD_TESTS OFF)
set(IMG2_INSTALL OFF)
add_subdirectory(third_party/imgcat2 img2 EXCLUDE_FROM_ALL)
target_link_libraries(app PRIVATE img2::img2)
```

```cmake
include(FetchContent)
set(IMG2_BUILD_CLI OFF)
set(IMG2_BUILD_TESTS OFF)
set(IMG2_INSTALL OFF)
FetchContent_Declare(img2 GIT_REPOSITORY <url-of-imgcat2> GIT_TAG <tag> GIT_SUBMODULES_RECURSE TRUE)
FetchContent_MakeAvailable(img2)
target_link_libraries(app PRIVATE img2::img2)
```

Here `img2::img2` is the real `img2` target; the vendored libraries (and, for the C++ vendors, `stdc++`, so a
C-only consumer links with the C driver) come through its link interface. The
options are normal variables (CMP0077), so they never land in your cache. img2 does not set your build type,
`CMAKE_EXPORT_COMPILE_COMMANDS`, global compile flags or `BUILD_SHARED_LIBS`.

Both ways are tested: `img2_consumer_find_package` (install into the build tree, move the prefix, check that
no build/source path leaked into an installed file, then build `src/tests/consumer` with `find_package`) and
`img2_consumer_subdir` (label `expensive`: `add_subdirectory` of the source tree plus a cache leak audit).

## API tour

| Header | Contents |
|---|---|
| `<img2/common.h>` | `img2_status_t`, `img2_strerror`, `img2_free`, `img2_init`/`img2_shutdown`, `img2_version_string`, `IMG2_BEGIN_DECLS` |
| `<img2/log.h>` | `img2_log_level_t`, `img2_set_log_callback`, `img2_set_log_level`, `img2_get_log_level` |
| `<img2/image.h>` | `img2_image_t`, `img2_frames_t`, `img2_filter_t`, `img2_image_create`/`destroy`, `img2_frames_free`, `img2_scale`, `img2_scale_fit`, `img2_fit_size`, `img2_apply_orientation`, inline `img2_image_get_pixel`/`set_pixel`, the `IMG2_MAX_*` limits |
| `<img2/mime.h>` | `img2_mime_t` (append-only), `img2_detect_mime`, `img2_mime_name`, `img2_mime_string` |
| `<img2/yuv.h>` | raw YUV formats, `img2_yuv_params_t`, `img2_yuv_parse_spec`, `img2_yuv_frame_size`, name helpers |
| `<img2/decode.h>` | `img2_decode`, `img2_decode_opts_t` (+ `IMG2_DECODE_OPTS_INIT`), `img2_can_decode`, `img2_is_animated`, decoder table |
| `<img2/encode.h>` | `img2_encode`, `img2_format_t`, `img2_encode_opts_t` (+ `IMG2_ENCODE_OPTS_INIT`), `img2_can_encode`, encoder table |
| `<img2/metadata.h>` | `struct img2_exif`, `struct img2_xmp`, init/clear, standalone JPEG/TIFF/XML parsers |
| `<img2/font.h>` | interned font handles: `img2_font_load`, `img2_font_load_fallback`, data/family getters, `img2_font_foreach` |
| `<img2/img2_config.h>` | generated: `IMG2_VERSION_*`, informative `IMG2_HAS_*` |

The public headers contain no feature conditionals: every function and type always exists, and a codec that
is not built returns `IMG2_E_UNSUPPORTED` (`img2_can_decode`/`img2_can_encode` tell in advance).

Decode, scale to fit 256x256, encode as PNG:

```c
#include <img2/img2.h>
#include <stdio.h>

int thumbnail(const uint8_t *data, size_t len, uint8_t **png, size_t *png_len)
{
	img2_frames_t fr;
	img2_image_t *small = NULL;
	img2_decode_opts_t dopts = IMG2_DECODE_OPTS_INIT;   /* a zeroed struct would disable JPEG deblocking */
	img2_status_t st = img2_decode(data, len, &dopts, &fr);
	if (st != IMG2_OK) {
		fprintf(stderr, "decode: %s\n", img2_strerror(st));
		return -1;
	}
	st = img2_apply_orientation(&fr.frames[0]);         /* EXIF orientation; call once */
	if (st == IMG2_OK) {
		st = img2_scale_fit(fr.frames[0], 256, 256, IMG2_FILTER_DEFAULT, &small);
	}
	img2_frames_free(&fr);
	if (st != IMG2_OK) {
		return -1;
	}
	img2_frames_t one = { &small, 1, 0 };               /* a one-frame view */
	img2_encode_opts_t eopts = IMG2_ENCODE_OPTS_INIT;   /* quality -1 = format default */
	st = img2_encode(&one, IMG2_FORMAT_PNG, &eopts, png, png_len);   /* free *png with img2_free() */
	img2_image_destroy(small);
	return st == IMG2_OK ? 0 : -1;
}
```

`img2_init()`/`img2_shutdown()` are optional (the tables are const, the font cache initialises lazily) but
recommended around library use; see [Threading model](#threading-model).

### Scaling filters

`img2_scale`/`img2_scale_fit` resample with stb_image_resize2 (RGBA, sRGB, edge clamp):

| `img2_filter_t` | stb_image_resize2 | imgcat2 `--interpolation` |
|---|---|---|
| `IMG2_FILTER_DEFAULT` | `stbir_resize_uint8_srgb` (stb's default filter choice) | `lanczos` (default; stb has no Lanczos) |
| `IMG2_FILTER_POINT` | `STBIR_FILTER_POINT_SAMPLE` | `nearest` |
| `IMG2_FILTER_BOX` | `STBIR_FILTER_BOX` | `box` |
| `IMG2_FILTER_TRIANGLE` | `STBIR_FILTER_TRIANGLE` | `bilinear` |
| `IMG2_FILTER_CUBIC` | `STBIR_FILTER_CATMULLROM` | `cubic` |
| `IMG2_FILTER_MITCHELL` | `STBIR_FILTER_MITCHELL` | `mitchell` |

A value outside the enum returns `IMG2_E_ARG`; a resampler failure returns `IMG2_E_CODEC`.

### Animation timing

`img2_decode` reports the frame timing of animated inputs:

- `img2_image_t.delay_ms`: how long the frame is shown, in ms; `0` = unknown or not animated.
- `img2_frames_t.loop_count`: `0` = loop forever or unknown, `N > 0` = play the animation N times.
- Single-frame images (also a one-frame GIF/APNG/WebP/JXL) always report `delay_ms = 0` and `loop_count = 0`.

| Format | `delay_ms` | `loop_count` |
|---|---|---|
| GIF | Graphic Control Extension `DelayTime * 10` (0 without a GCE) | `NETSCAPE2.0` application extension count (0 = forever); `1` when the extension is absent (plays once) |
| APNG | `fcTL` `delay_num/delay_den` in ms, rounded (`delay_den == 0` means 1/100 s) | `acTL` `num_plays` (0 = forever) |
| WebP | difference of consecutive `WebPAnimDecoderGetNext` timestamps (first frame: its timestamp) | `WebPAnimInfo.loop_count` (0 = forever) |
| JPEG XL | frame `duration * 1000 * tps_denominator / tps_numerator` (64-bit) | `num_loops` (0 = forever) |
| HEIF / AVIF sequence track | sample duration in the **track** timescale (`heif_track_get_timescale`), rounded to ms; 0 if unknown | `heif_track_get_number_of_repetitions`: N = play N times, indefinite = 0, uninterpreted edit list (libheif reports 0) = 1 (see [Known limitations](#known-limitations--cli-deltas)) |
| HEIF / AVIF top-level image list | always 0 | always 0 |

`img2_frames_t.loop_count` survives `max_frames` trimming. The imgcat2 CLI does not use the timing (it
animates with `--fps`), so its output is unchanged.

## Ownership and memory

Everything the library allocates is released by an `img2_*` function, never by `free()`:

| Object | Created by | Released by |
|---|---|---|
| `img2_image_t *` | `img2_image_create` (status + out-param), `img2_scale`, `img2_scale_fit` | `img2_image_destroy` (frees pixels, exif, xmp, struct; NULL-safe) |
| `img2_frames_t` contents | `img2_decode` | `img2_frames_free` (destroys every frame, frees the array, zeroes the struct; NULL-safe) |
| `img2_exif_t` / `img2_xmp_t` contents | decoders (attached to frames), `img2_exif_parse_*`, `img2_xmp_parse_*` | owned by the image when attached; caller-owned structs: `img2_exif_init`/`img2_exif_clear`, `img2_xmp_init`/`img2_xmp_clear` |
| encoded buffers | `img2_encode` | `img2_free` |
| `img2_font_t *` | `img2_font_load`, `img2_font_load_fallback` | never: interned in the font cache, owned by the library, no release API, valid until the last `img2_shutdown()`, or until process exit if the cache was built lazily without an `img2_init()` reference (**[R4]**, see below) |

- `img2_apply_orientation(&img)` may replace the pixel buffer and swap width/height of `*img`.
- On error every out-param is reset (`*out = NULL`, frame sets zeroed, `*out_len = 0`).
- `img2_init()` is reference counted; `img2_shutdown()` without a matching `img2_init()` (no reference left) is
  a no-op (**[R5]**). The last `img2_shutdown()` frees the font cache.
- Implicit font-cache reference: if an `img2_font_*` call (or SVG `<text>` rendering) builds the font cache
  while no `img2_init()` reference is held, the library takes one implicit reference that is never released.
  `img2_shutdown()` never drops the count below it, so the cache, its handles and its strings (family names,
  `img2_font_foreach` strings) stay valid until process exit; leak checkers report the cache as still
  reachable. Explicit `img2_init()`/`img2_shutdown()` pairs work as before (the INFO registry lists are
  printed on the first explicit `img2_init()`), but that cache is no longer freed by them. If the cache is
  first built while an `img2_init()` reference is held, the final `img2_shutdown()` frees it as before.

## Errors and logging

### Status codes

Fallible functions return `img2_status_t`: `IMG2_OK == 0`, errors are negative and stable (new codes are only
appended). `img2_strerror()` never returns NULL.

| Code | Value | Meaning |
|---|---|---|
| `IMG2_OK` | 0 | success |
| `IMG2_E_NOMEM` | -1 | out of memory |
| `IMG2_E_ARG` | -2 | invalid argument (NULL, zero size, invalid caller-built image) |
| `IMG2_E_FORMAT` | -3 | malformed or unrecognised data |
| `IMG2_E_UNSUPPORTED` | -4 | format/feature not built into this library |
| `IMG2_E_LIMIT` | -5 | resource limit exceeded (dimensions, pixels, input size) |
| `IMG2_E_IO` | -6 | I/O error |
| `IMG2_E_CODEC` | -7 | error reported by a codec library |
| `IMG2_E_INTERNAL` | -8 | internal error |
| `IMG2_E_NOT_FOUND` | -9 | requested item (e.g. a font) not found |

**[M7]** Exceptions to "status everywhere":

- pointer-returning lookups and table accessors (NULL = invalid index/input): `img2_decoder_info`,
  `img2_encoder_info`, `img2_font_get_data`, `img2_font_get_family`, `img2_mime_name`/`img2_mime_string`,
  `img2_yuv_format_name`, and internally `img2_image_alloc`, `img2_font_find`, `img2_font_find_fallback`,
  `img2_font_face_open`, `img2_xml_document_create`, the `img2_xml_element_*` accessors,
  `img2_decoder_find`/`img2_encoder_find`;
- `bool` predicates and size helpers: `img2_can_decode`/`img2_can_encode`, `img2_is_animated` (and the
  internal `img2_*_is_animated`), `img2_yuv_frame_size`, `img2_image_set_pixel`, internal `img2_image_calc_size`;
- `void` destructors and setters.

### Logging

Every diagnostic goes through one process-global sink; the library never writes to stdout.

| Level | Used for |
|---|---|
| `IMG2_LOG_ERROR` (0) | unconditional error text (e.g. `Error: ...`, `libpng error: ...`) |
| `IMG2_LOG_WARN` (1) | unconditional `Warning:`/`Note:`/`Info:` text, libpng/libtiff warnings |
| `IMG2_LOG_INFO` (2) | messages imgcat2 used to print only in verbose mode (registry lists at `img2_init`, decode summaries) |
| `IMG2_LOG_DEBUG` (3) | API boundary checks (rejected caller-built images, invalid filter values, ...) |

- Default level `IMG2_LOG_ERROR`; `img2_set_log_level(max)` emits every message with `level <= max`.
- Default sink: the message plus `\n` to stderr, one line per message. Messages keep the historic imgcat2
  prefixes (`Error: `, `Warning: `, `Note: `, `image_scale_fit: `, ...) so the CLI's stderr is unchanged.
- libpng and libtiff diagnostics arrive through the callback with their historic text (`libpng error: ...`,
  `libpng warning: ...`, `<module>: ...`, `<module>: Warning, ....`) at ERROR/WARN (**[M1b]**); libjpeg errors
  are trapped by its `error_exit` handler and logged.
- `imgcat2` sets `IMG2_LOG_WARN` at start-up and `IMG2_LOG_INFO` with `-v`.
- **Log callback contract** (**[R20]**): `img2_set_log_callback(fn, user)` installs `fn` process-wide (NULL
  restores the stderr sink). `fn` runs on the logging thread, possibly while `s_init_lock` is held (the INFO
  registry lists printed by `img2_init`) or while `s_font_lock` is held (font-cache messages such as
  `Warning: Failed to load font '%s', using fallback` and `Error: Fallback font not available`). It must
  therefore be thread-safe and must not call any `img2_*` function (including the log setters); re-entry
  deadlocks. `msg` has no trailing newline and is valid only during the call.
- **[E8]/[R21]** After `img2_set_log_callback()` returns, threads that are already logging may still call the
  previous `fn`/`user`; keep the previous `user` valid until those calls finish (or until `img2_shutdown()`).
- **[SM4] Untrusted text warning:** messages can embed strings taken from the input file (EXIF/XMP fields,
  SVG text, vendor messages quoting file data), which may contain terminal control/escape bytes. The default
  stderr sink writes them unmodified (byte compatibility with imgcat2). Hosts that show messages on a terminal
  or in logs should install a callback with `img2_set_log_callback` and sanitise there.

## Threading model

What is covered by the S6 audit and the `test_threads` ctest (**[SM3]**), and nothing more:

- Concurrent `img2_decode` of PNG, JPEG, GIF and SVG (with text) on distinct buffers, `img2_scale` and PNG
  `img2_encode` on distinct images, from 8 threads, while another thread toggles the log callback and level
  (`test_threads`).
- The library's own mutable state is limited to, and enforced by the `img2_symbols` mutable-data audit
  (`cmake/scripts/img2_mutable_allowlist.txt`):
  - log: atomic level, mutex-protected callback pair (`s_level`, `s_lock`, `s_fn`, `s_user`);
  - init: `s_init_lock` + reference count (`s_refs`) and the implicit font-cache reference flag
    (`s_font_implicit_ref`);
  - font cache: `s_font_lock` + cache list, interned fallback handle and the initialised flag;
  - read-only in practice: stb_image_resize2's dispatch/weight tables and nanosvg's colour table (non-const
    in the vendor code, never written), and stb_image's flag/gamma globals (written only by `stbi_set_*`
    setters, which the library never calls; a guard check enforces that);
  - `stbi__g_failure_reason` is thread-local.
- Per-call options: jpeg-quantsmooth settings travel with each call (no global), libraw/libjxl/libheif decoder
  and encoder objects are created per call, libtiff/libpng handlers are per handle/per struct.
- Vendor notes from the audit: libpng (PNG/APNG) and libjpeg-turbo (JPEG, with per-call jpeg-quantsmooth
  settings), giflib and the header-only stb/nanosvg code are exercised concurrently by `test_threads`.
  libheif is used without `heif_init()` (its implicit initialisation); LibRaw (`libraw_init`), libjxl and
  libheif objects are created per call; libtiff handlers are per handle; jpeg-quantsmooth and LibRaw use
  OpenMP, i.e. the process-global OpenMP runtime.
- **Not verified thread-safe**: the vendors behind WebP (libwebp), HEIF/AVIF (libheif, libde265, kvazaar, aom,
  OpenJPEG), TIFF (libtiff), RAW (LibRaw), JPEG XL (libjxl, highway, brotli, lcms2) and the JPEG/HEIF/WebP/JXL
  encoders. The library keeps no shared state of its own on those paths, but no concurrency test covers them;
  serialise calls for these formats if you need a guarantee.
- **[R19]** `img2_init`/`img2_shutdown` are serialised internally (`s_init_lock`), but any other `img2_*` call
  concurrent with the final `img2_shutdown()` is unsupported, and font handles are invalid afterwards
  (unless the cache holds its implicit reference, see [Ownership and memory](#ownership-and-memory)).
  `img2_shutdown` must not race other calls.
- The log callback contract above applies (thread-safe, no re-entry).

## Limits

- **[SM1]** `img2_decode` and `img2_is_animated` reject `len > IMG2_MAX_FILE_SIZE` (1 GiB) before reading
  (`IMG2_E_LIMIT` / `false`).
- Every frame goes through one gate: each axis `<= IMG2_MAX_DIMENSION` (65536), total
  `<= IMG2_MAX_PIXELS` (200 megapixels); violations are `IMG2_E_LIMIT`.
- **[P33]** The size in the file header is checked against the same gate before the codec allocates for the
  image (JPEG before `jpeg_start_decompress`, QOI header, `stbi_info` for the stb_image formats, WebP image /
  animation canvas from `WebPGetFeatures`, HEIF/AVIF image handles, RAW size after `libraw_open_buffer`), so a
  small file announcing a huge image fails with `IMG2_E_LIMIT` without the gigabyte-sized codec buffers. PNG,
  GIF, TIFF, JPEG XL, ICO, PNM, YUV and SVG already checked before allocating.
- `img2_decode_opts_t.max_frames > 0` keeps only the first N frames. It trims the result after decoding,
  so it does not bound decode time or memory.
- **[F14]** Animated GIF, APNG, WebP and JPEG XL decodes are capped at 2^30 pixels summed over all output
  frames (each frame is a full canvas, 4 GiB of RGBA at the cap): checked up front from the frame count and
  again while allocating frame by frame; exceeding it frees everything and returns `IMG2_E_LIMIT`. HEIF/AVIF
  image lists (frames may differ in size) sum the image handles' sizes before decoding anything and keep a
  running total while decoding. HEIF/AVIF sequence tracks (sample count unknown up front) check the track
  resolution against `IMG2_MAX_PIXELS`/`IMG2_MAX_DIMENSION` before the first sample and keep the running total
  per decoded frame. TIFF pages have no total cap.
- **Frame cap**: GIF, APNG, WebP, JPEG XL, HEIF/AVIF image lists and HEIF/AVIF sequence tracks keep the first
  200 frames of a longer animation and log a WARN line (`... limiting to 200`); the rest is not decoded (a
  sequence decodes one sample past the cap to detect it). JPEG XL stops its frame-header walk at the 201st
  frame (imgcat2 1.0 and libimg2 before S13b failed such files with `IMG2_E_CODEC`). Sequence tracks are decoded
  with libheif's `ignore_sequence_editlist`, so a repeating/indefinite edit list is reported as `loop_count`
  and never unrolled into repeated frames.
- Frame and canvas allocations of the animated decoders (and of the JPEG XL/HEIF/AVIF still paths) report
  `IMG2_E_LIMIT` when the size fails the `IMG2_MAX_DIMENSION`/`IMG2_MAX_PIXELS` gate and `IMG2_E_NOMEM` only for
  a real allocation failure. JPEG XL checks the canvas against both limits from the basic info, before any frame
  is walked or decoded (a canvas over `IMG2_MAX_DIMENSION` was `IMG2_E_CODEC` before S13b).
- **[F16]** GIF: before giflib decodes anything, the records are walked without LZW-decoding the rasters to
  count the frames and check the logical screen (`IMG2_E_LIMIT`) and the total above; `img2_is_animated`
  on a GIF only walks the records up to the second image descriptor. The GIF decode then reads the records
  one by one and stops after the 200th image: later rasters are never LZW-decoded, and a defect after the
  201st image descriptor (corrupt or truncated data past the cap) no longer fails the decode. HEIF/AVIF:
  `img2_is_animated` counts top-level images and, with at most one, reads at most two raw samples of the first
  visual sequence track (no decoding).
- **[SM2]** Every entry point that takes a caller-built `img2_image_t`/`img2_frames_t` (`img2_scale`,
  `img2_scale_fit`, `img2_encode`, `img2_apply_orientation`) validates it first: non-NULL pixels, non-zero
  dimensions within the limits, `stride == width * 4` (1.0 accepts packed rows only), frame count >= 1 and
  non-NULL frames. Violations return `IMG2_E_ARG` and log at DEBUG only.

## Font discovery

The font cache (used by SVG `<text>` rendering and `img2_font_*`) scans, in this order:

- Linux: `/usr/share/fonts`, `/usr/local/share/fonts`, `~/.local/share/fonts`, `~/.fonts`
- macOS: `/System/Library/Fonts`, `/Library/Fonts`, `~/Library/Fonts`
- Windows: `C:\Windows\Fonts`

`~` expands from `HOME`, read with `secure_getenv` where available (ignored in setuid/setgid processes). The
scan is lazy: it runs on the first `img2_font_*` call or the first SVG with text, never in `img2_init` (a
scan while no `img2_init()` reference is held keeps the cache until process exit, see
[Ownership and memory](#ownership-and-memory)). The
embedded DejaVu Sans Mono family (4 faces) is always available and is the fallback for unknown families.

## Encode quality

`img2_encode_opts_t.quality`: `-1` = format default, otherwise 0..100 with a per-format meaning (kept from
imgcat2 1.0 for byte-identical output):

| Format | Meaning | Default |
|---|---|---|
| JPEG | libjpeg quality 0..100 (never lossless) | 90 |
| PNG | zlib level = quality / 10, clamped 0..9; always lossless | level 6 |
| HEIF | 0..100 (never lossless) | 80 |
| WebP | 0..99 lossy, 100 lossless | 80 |
| JPEG XL | 0..94 lossy (distance (100 - q) / 10), >= 95 lossless | 80 |

This deviates from the locked decision "100 = lossless where supported": PNG is always lossless, JPEG/HEIF are
never lossless, and JPEG XL is already lossless from 95 (see below). Only `in->frames[0]` is encoded.

## Orientation

`img2_apply_orientation(&img)` applies `img->exif->orientation` (1..8) in place and does **not** reset the
field, so call it once per image (imgcat2 prints the original orientation in `--info` after applying it).
TIFF, HEIF and AVIF frames arrive already oriented by their codecs; their EXIF orientation is informative.

## Deviations from locked decisions

- **[M1a]** Font cache: a statically initialised mutex plus an `initialized` flag checked under it, instead
  of `pthread_once` (a literal `pthread_once` would block re-initialisation after `img2_shutdown()`).
- **[M1c]** JPEG XL is lossless from quality 95 (not only at 100), as imgcat2 always did; changing it would
  change `--jxl 95..99` output.
- **[M1b]** libtiff handlers are installed per handle through `TIFFOpenOptions`/`TIFFClientOpenExt` instead of
  the process-global `TIFFSetErrorHandlerExt` (no global state; a thread-safety gain).
- "CLI `--silent` lowers the level" is realised as: the CLI has no `--silent` flag (silent is its default);
  it sets `IMG2_LOG_WARN` at start-up and `IMG2_LOG_INFO` with `-v`.

## Known limitations / CLI deltas

- C++ vendors (LibRaw, libheif, libjxl, highway) can still terminate the process on internal fatal errors
  (`std::terminate` on an uncaught exception, `JXL_ABORT`); the no-terminate guarantee (`img2_symbols`)
  covers our own objects only.
- Vendors other than libpng, libtiff and libjpeg report only through return codes (no message text).
- With a host libtiff older than 4.5 (`IMG2_USE_SYSTEM_TIFF=ON`) the per-handle handlers are unavailable and
  libtiff's default handlers print directly to stderr.
- **[SI1]** The fat archive embeds the vendored libpng, libjpeg-turbo, zlib-ng, giflib, libwebp, libheif
  (+ libde265, kvazaar, aom, OpenJPEG), libtiff, LibRaw and libjxl (+ highway, brotli, lcms2). A consumer that
  also links its own copy of any of them gets duplicate-symbol errors or silent first-wins binding. Mitigation:
  link only `img2::img2` for those codecs, or build img2 with the matching `IMG2_USE_SYSTEM_*`;
  `share/img2/vendored-libraries.txt` says which versions are inside. The build only checks for duplicates
  inside the archive itself (see [The fat archive](#the-fat-archive)); prefixing the vendored symbols or hiding
  them (partial link + hidden visibility) is a possible follow-up, not done.
- **[SI2]** The asserts of the header-only vendors compiled into our objects (stb, nanosvg, qoi) are compiled
  out (`((void)0)`), which equals the previous `NDEBUG` release behaviour.
- **[Q9]** CLI delta: imgcat2's iTerm2 output now encodes through `img2_encode`, so an encoder failure (in
  practice only out of memory) additionally prints `Error: Encoder '<name>' failed to encode image` (from
  `encoders/encoder.c`, one extra stderr line; only on encoder failure / OOM, normal output is unchanged).
  Not covered by the golden test (needs a TTY).
- **[R19]** Any `img2_*` call concurrent with the final `img2_shutdown()` is unsupported; font handles are
  invalid after it, unless the font cache was built lazily without an `img2_init()` reference (implicit
  reference: then it lives until process exit).
- CLI delta: `--interpolation nearest|bilinear|cubic` now select their filters (imgcat2 1.0 accepted the
  names but always used the default filter, so their output changes); `box` and `mitchell` are new; `lanczos`
  (default) output is unchanged. See [Scaling filters](#scaling-filters).
- Only frame 0 is encoded (no animated output).
- HEIF/AVIF: a file with more than one top-level image decodes as an image list (one frame per image, no
  timing: `delay_ms = 0`, `loop_count = 0`), even if it also carries a track. Otherwise the first visual
  sequence track (`ftyp avis`/`hevc`, libheif track API) is decoded when the file has no top-level image, or
  when it has one (e.g. the compatibility still of an animated AVIF) and the track has at least two samples;
  a still with a one-sample track stays a still. Only the first visual track is read; other tracks are ignored. Needs libheif >= 1.23 (the vendored copy);
  an older host libheif (`IMG2_USE_SYSTEM_HEIF`) keeps the old behaviour (a sequence-only file fails with
  `IMG2_E_CODEC`).
- HEIF/AVIF `loop_count`: libheif only interprets the edit list as a repetition count when the track
  timescale equals the movie (`mvhd`) timescale; otherwise `heif_track_get_number_of_repetitions` returns 0
  and img2 reports `loop_count = 1` (play once, as libheif advises), even if the writer asked for N
  repetitions. libheif's writer uses a 90000 Hz track timescale by default, so such files only round-trip
  the repetition count if the writer sets the track timescale (`heif_track_options_set_timescale`) to the
  sequence timescale. Indefinite repetition (`heif_context_set_number_of_sequence_repetitions(ctx, 0)`)
  with matching timescales reads back as `loop_count = 0`. `img2_is_animated` reads raw samples through
  the edit list, so with a repeating edit list two byte-identical first samples count as one frame.
- Tests: `test_animation_timing` (GIF/APNG/WebP fixtures, synthetic GIF and APNG), `test_animation_jxl`,
  `test_animation_webp` and `test_animation_heif` (image lists and sequence tracks; fixtures encoded in-test
  with the vendored libjxl, libwebp and libheif encoders).
- The macOS fat archive path (`libtool -static`) is written but untested.
- Host-library packages: with any `IMG2_USE_SYSTEM_*` (or resvg) the installed package links those host
  libraries (configure warns), and with a static link they must come after the fat archive, which cannot
  resolve symbols a host library needs from a vendored one (e.g. a host libheif using the vendored libjpeg);
  use the vendored stack for packages.
- Known cache entries: see below.
- nanosvg text rendering passes the kerning codepoints in reversed order in one of its two kerning calls
  (`img2_font_face_kern(face, codepoint, prev_codepoint)` in the render loop, nanosvg.h). It looks like a bug,
  but fixing it would change SVG text output bytes; do not fix it (both call sites carry a comment pointing
  here).
- nanosvg SVG `<text>` cost bounds. Rendering is linear in the text length since nanosvgrast.h merges the
  edges starting on a scanline in one pass (`nsvg__insertActive`; the one-by-one insertion made a `<text>`
  quadratic: 4000 characters took 1.5 s, now 0.14 s). A `<text>` / `<tspan>` collects at most 4094 bytes of content
  (`NSVGparser.textContent[4096]`): a content chunk (the text between two tags) that does not fit is dropped
  whole, not truncated, so one 8000-character run renders nothing. Every `<text>` / `<tspan>` run is its own
  shape, so many short texts cost linear in their count. What stays: every shape is rasterized over all
  canvas rows (a per-shape cost of clearing width x height scanline bytes, about 0.14 ms per shape on
  2000x2000), glyphs outside the canvas are still flattened and kept in the active-edge list, and the active
  list's bubble sort is quadratic in the edges that cross on one scanline (any path, not text-specific).

## Known cache entries

img2 sets every vendor knob as a normal variable inside its vendor section (`CMAKE_POLICY_DEFAULT_CMP0077`,
`CMP0126` and `CMP0125` NEW), so its own settings never land in a consumer's cache. What remains:

- **img2 options** (`option()`, user-settable): `IMG2_ENABLE_*`, `IMG2_USE_SYSTEM_*`, `IMG2_WERROR`,
  `IMG2_POSITION_INDEPENDENT_CODE`, `IMG2_FAT_ARCHIVE` (and `IMG2_BUILD_*`/`IMG2_INSTALL` unless you set
  them as normal variables first). `IMG2_STATIC_PIE` exists only when the CLI is built (`IMG2_BUILD_CLI`).
- **img2 check results** (INTERNAL): `IMG2_FORTIFY_PREDEFINED`, `IMG2_HAVE_SECURE_GETENV` (and
  `IMG2_HAVE_STATIC_PIE`, CLI builds on Linux only).
- **Vendor-created entries with upstream defaults.** brotli, libjxl, zlib-ng and highway still create their
  own cache entries through their own non-FORCE `set(... CACHE ...)` / `option()` / `INTERNAL` calls, with the
  *upstream* default value, e.g. `BROTLI_BUILD_TOOLS=ON`, `BROTLI_BUILD_FOR_PACKAGE`
  (vendor/brotli/CMakeLists.txt), `JPEGXL_FORCE_SYSTEM_HWY=false`, `JPEGXL_ENABLE_TOOLS=true`,
  `JPEGXL_STATIC=false`, `JPEGXL_ENABLE_SKCMS=YES` (vendor/libjxl), `WITH_GTEST`/`WITH_BENCHMARKS` (zlib-ng,
  INTERNAL), `HWY_*` such as `HWY_FORCE_STATIC_LIBS` (highway, INTERNAL), and `BUILD_TESTING=ON` (zlib-ng's
  `include(CTest)`; img2 sets it OFF as a normal variable around the vendors it wraps, and no vendor test is
  registered). **These cache values are not what is built**: with CMP0126 NEW
  a `set(CACHE)` keeps the normal variable binding, so what gets built follows img2's variables (e.g. no brotli
  tools, no libjxl tools, highway static). Changing these cache entries has no effect.
- Other vendor `option()`s/`set(CACHE)`s for knobs img2 never sets keep their upstream defaults.
- No guarded `if(NOT DEFINED CACHE{...})` fallback was needed for any knob.

`img2_consumer_subdir` audits the consumer cache after an `add_subdirectory` build and prints the full
residual list into its log.

## Naming, layout and guard tests

- Every non-static symbol of libimg2 starts with `img2_` (internal ones too; never `img2__`); types are
  `img2_*_t`; macros and enum constants `IMG2_*`. Header-only vendors are TU-local; nanosvg reaches glyph data
  only through the internal `img2_font_face_*` wrappers.
- Layout: `include/img2/` (public), `src/libimg2/{core,decoders,encoders,metadata,text,font}` (library),
  `src/imgcat2/` (CLI, public API only), `src/tests/` (unit, integration, consumer), `cmake/` (modules),
  `cmake/vendor/` (one module per vendor stack), `cmake/scripts/` (guards, fat archive, consumer driver).
- Guard ctests (label `guard`), all part of the default ctest run:
  - `img2_symbols`: only `img2_`-prefixed globals in `libimg2.a`; no `exit`/`_exit`/`_Exit`/`quick_exit`/
    `abort`/`__assert*` references (also in the `-UNDEBUG` object variant `img2_assertcheck`); mutable-data
    allow-list; no `stbi_set_*` calls.
  - `img2_layering_lib`: the library includes nothing from the CLI.
  - `img2_layering_cli`: the CLI includes only `<img2/...>` and its own headers.
  - `img2_public_headers`: no feature conditionals in `include/img2/*.h`; `img2_header_check` (built in ALL)
    compiles every public header alone, twice, with `-std=c11 -Wall -Wextra -Werror -pedantic`.
  - `img2_consumer_find_package` (label `consumer`) and `img2_consumer_subdir` (labels `consumer;expensive`).

## Adding a codec

1. `cmake/vendor/Img2Vendor<X>.cmake` ending with `img2_feature(<X> LIBS ... INCLUDE_DIRS ... ARCHIVES ...
   ARCHIVE_TARGETS ... SYSTEM_LIBS ...)` (vendored archives as `$<TARGET_FILE:t>` or absolute ExternalProject
   paths; host libraries in `SYSTEM_LIBS` plus `img2_warn_host_dependency()`). Add `<X>` to
   `IMG2_ALL_FEATURES`, to `IMG2_MACRO_FEATURES` (if code tests `IMG2_HAVE_<X>`), its link position to
   `IMG2_LINK_ORDER` and its include position to `IMG2_INCLUDE_ORDER` (`cmake/Img2Features.cmake`). Add
   `IMG2_ENABLE_<X>` in `cmake/Img2Options.cmake`. Include the module in the top-level `CMakeLists.txt` in
   dependency order inside the vendor section.
2. `src/libimg2/decoders/decoder_<x>.c` implementing
   `img2_status_t img2_decode_<x>(const uint8_t *, size_t, const img2_decode_opts_t *, img2_frames_t *)`
   (and optionally `bool img2_<x>_is_animated(...)`). Errors go through status + `img2_log`; vendor errors are
   trapped with setjmp/handlers and never terminate.
3. A prototype in `src/libimg2/decoders/decoder.h` (unconditional).
4. One row under `#ifdef IMG2_HAVE_<X>` in the table in `decoders/decoder.c` (and/or `encoders/encoder.c`).
5. A new `IMG2_MIME_<X>` **appended** at the end of `img2_mime_t` in `include/img2/mime.h` (values are ABI).
   Detection goes in `decoders/magic.c`, names in `img2_mime_name`/`img2_mime_string`.
6. An `if(IMG2_HAVE_<X>)` source block in `src/libimg2/CMakeLists.txt`.
7. Add `IMG2_HAS_<X>` to `cmake/img2_config.h.in` and the mapping in `src/libimg2/CMakeLists.txt`.
8. Tests via `img2_add_test(... REQUIRES <X>)` and golden cases.
9. Run `img2_symbols`, `img2_layering_*` and `img2_public_headers` (the default `ctest` run does).
