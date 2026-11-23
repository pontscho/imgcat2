# Encoder API v2: AVIF output and encoder options

Status: phase 1 (AVIF output) implemented (img2/s24, default AVIF quality 80 at aom speed 6, see
[Measurements](#measurements)); phase 2 (encoder options) implemented (img2/s25, see
[Phase 2 results](#phase-2-results)); phase 3 open. Branch base: feature/split (img2/s23).

## Goals

1. AVIF output (AV1 in HEIF, via the vendored libheif + aom encoder).
2. Encoder options beyond `quality`: lossless, effort, chroma subsampling, alpha quality, progressive, threads.
3. Opt-in metadata passthrough (EXIF, XMP) from the decoded image into the encoded file.

Non-goals: animated output (APNG, animated WebP, AVIF/HEIF sequences), ICC profiles (no decoder reads them today), new
encoder libraries.

Hard compatibility rule: with default options every encoder produces byte-identical output to img2/s23. The golden
harness must pass with 0 mismatches before any new golden case is added.

## Current state (facts)

- `img2_encode_opts_t` has one field, `quality` (-1 = format default); `img2_encode()` maps it to a native value
  (`core/api.c:83-93`) and calls a per-format `img2_encode_fn(img, native_quality, ...)` (`encoders/encoder.h:41`).
- Hard-coded today: JPEG 4:2:0, baseline, no Huffman optimisation; PNG no interlace, default filter; WebP simple API
  (method 4, alpha_quality 100), lossless only at q=100; JXL effort 7, lossless at q>=95, single-threaded; HEIF
  HEVC (kvazaar) with `options=NULL`.
- No encoder writes metadata. Decoded images carry only parsed `img2_exif_t` / `img2_xmp_t`; the raw EXIF/XMP bytes are
  freed in the decoders (e.g. `decoder_heif.c:451-493`). Scaling drops exif/xmp (`core/image.c:75-104`).
- The vendored libheif is built with `HAVE_AOM_ENCODER=1`; on the host-library path (`IMG2_USE_SYSTEM_HEIF`) aom is
  optional.
- libwebpmux is not built (`Img2VendorWebp.cmake:17,25`); `jxl_threads` is built but not linked (`Img2VendorJxl.cmake`).

## Public API

### Format

```c
typedef enum img2_format {
	IMG2_FORMAT_NONE = 0,
	IMG2_FORMAT_JPEG,
	IMG2_FORMAT_PNG,
	IMG2_FORMAT_HEIF,
	IMG2_FORMAT_WEBP,
	IMG2_FORMAT_JXL,
	IMG2_FORMAT_AVIF           /* new, appended: existing values unchanged */
} img2_format_t;
```

### Options

Every new field uses 0 as "format default", so a zero-initialised struct and the existing designated initialiser
`{ .quality = q }` keep their current meaning.

```c
typedef enum img2_chroma {
	IMG2_CHROMA_DEFAULT = 0,
	IMG2_CHROMA_420,
	IMG2_CHROMA_422,
	IMG2_CHROMA_444
} img2_chroma_t;

enum {
	IMG2_META_EXIF = 1u << 0,
	IMG2_META_XMP  = 1u << 1
};

typedef struct img2_encode_opts {
	int quality;            /* -1 = default, else 0..100 (unchanged) */
	bool lossless;          /* false = format default (lossy, except PNG) */
	int effort;             /* 0 = default, else 1 (fastest) .. 10 (smallest) */
	img2_chroma_t chroma;   /* subsampling of lossy YCbCr formats */
	int alpha_quality;      /* 0 = default, else 1..100 */
	bool progressive;       /* progressive / interlaced output */
	int threads;            /* 0 = default, else 1..64 encoder threads */
	unsigned metadata;      /* IMG2_META_* bits to write; 0 = none (default) */
} img2_encode_opts_t;

#define IMG2_ENCODE_OPTS_INIT { -1, false, 0, IMG2_CHROMA_DEFAULT, 0, false, 0, 0u }
```

Out-of-range values: `IMG2_E_ARG`. A valid option the chosen format cannot honour: the encode proceeds and one
`IMG2_LOG_WARN` names the option and the format (e.g. `encode: chroma 444 has no effect for WebP (always 4:2:0)`).

### Per-format mapping

| Option | JPEG | PNG | WebP | JXL | HEIF (kvazaar) | AVIF (aom) |
|---|---|---|---|---|---|---|
| quality | as today | zlib level q/10 | as today | as today | as today | aom `quality` |
| lossless | warn | always lossless | `config.lossless` + `exact` (q=100 still selects it; decision 6) | `SetFrameLossless` (q>=95 still selects it) | warn (kvazaar lossless is 4:2:0 YCbCr, not RGB-lossless) | aom `lossless` + 4:4:4, identity matrix (verified bit-exact) |
| effort 1..10 | >= 6: `optimize_coding` | zlib level e-1 when quality is default (decision 3), else warn (CLI: error, decision 5) | `method` = round((e-1)*6/9) | `EFFORT` = e | warn (no preset parameter) | aom `speed` = 10 - e (9..0, linear: decision 8) |
| chroma | comp_info sampling factors | warn | 420 only: warn otherwise | warn (XYB) | 420 only: warn otherwise | aom `chroma` "420"/"422"/"444" |
| alpha_quality | warn | warn | `alpha_quality` | extra channel distance from quality | warn | aom `alpha-quality` |
| progressive | `jpeg_simple_progression` | Adam7 interlace | warn | lossy: `PROGRESSIVE_AC` = 1; lossless: warn (decision 7) | warn | warn |
| threads | warn | warn | `thread_level` = threads > 1 | parallel runner (links `jxl_threads`) | warn (kvazaar auto) | aom `threads` |
| metadata | APP1 Exif / XMP | eXIf / iTXt `XML:com.adobe.xmp` | VP8X EXIF / XMP chunks (needs libwebpmux) | container + `Exif` / `xml ` boxes | `heif_context_add_exif_metadata` / `_XMP_metadata` | same as HEIF |

Defaults stay as today: JPEG optimize off, 4:2:0, baseline; WebP method 4; JXL effort 7; AVIF uses the libheif aom
defaults (speed 6, 4:2:0, threads = hardware concurrency, tune auto = `iq` with the vendored aom 3.15) with quality
from the registry default (80).

### Capability query

`img2_can_encode(IMG2_FORMAT_AVIF)` and `(IMG2_FORMAT_HEIF)` become runtime checks via
`heif_have_encoder_for_format(heif_compression_AV1 / _HEVC)`, so a host libheif without aom reports false.

## Metadata passthrough

### Source of the bytes

Writing EXIF back from the parsed struct would drop every tag we do not parse. Instead decoders keep the raw blocks:

```c
/* img2_image_t, new fields (owned by the image, freed by img2_image_free) */
uint8_t *exif_raw;      /* TIFF-header form ("II*\0" / "MM\0*"), container prefixes stripped; NULL if none */
size_t exif_raw_len;
char *xmp_raw;          /* XMP packet, NUL-terminated; NULL if none */
size_t xmp_raw_len;
uint8_t orientation;    /* EXIF orientation still to be applied to the pixels: 1 = upright */
```

Plus a setter for callers who build images themselves: `img2_image_set_metadata(img, exif, exif_len, xmp, xmp_len)`.

Size caps: EXIF and XMP are each capped (proposal: 1 MiB) at decode; larger blocks are dropped with a WARN. Scaling and
`img2_image_orient` copy the raw blocks to the result image.

### Orientation

Pixels from the TIFF, HEIF and AVIF decoders are already upright (the codecs apply it); JPEG, PNG, WebP, JXL and RAW are
not. Today this is known only to the CLI (`pipeline.c:530-551`, by format). Proposal: decoders set
`img->orientation` (1 for the self-orienting codecs, the EXIF value for the others), `img2_apply_orientation` sets it to
1, and the encoder rewrites the EXIF Orientation tag (0x0112 in IFD0) to `img->orientation` in its copy of the block.
The written file is then correct whether or not the caller rotated the pixels. The CLI pipeline can test the field
instead of the format. `img2_exif_t.orientation` keeps reporting the file's value (no `--info` change).

### Writers

- JPEG: APP1 `Exif\0\0` + TIFF block, APP1 XMP namespace + packet, after `jpeg_start_compress`. A block over the
  65533-byte marker limit is skipped with a WARN (no extended XMP).
- PNG: `png_set_eXIf_1`, iTXt `XML:com.adobe.xmp`.
- WebP: switch to `WebPConfig` + `WebPEncode`, then WebPMux for the EXIF/XMP chunks. Build change: enable libwebpmux
  and add it to the fat archive and the vendor manifest.
- JXL: `JxlEncoderUseContainer`, `Exif` box (4 zero bytes + TIFF block), `xml ` box.
- HEIF/AVIF: `heif_context_add_exif_metadata`, `heif_context_add_XMP_metadata` on the primary handle.

Known limitation (documented, not fixed): the EXIF IFD1 thumbnail and the PixelX/YDimension tags are written as
decoded, so they can be stale after scaling.

## CLI

| Flag | Maps to |
|---|---|
| `--avif Q` | conversion to AVIF, quality 0..100 |
| `--lossless` | `lossless = true` |
| `--effort N` | 1..10 |
| `--chroma 420\|422\|444` | `chroma` |
| `--alpha-quality N` | 1..100 |
| `--progressive` | `progressive = true` |
| `--threads N` | 1..64 |
| `--keep-metadata` | `metadata = IMG2_META_EXIF \| IMG2_META_XMP` |

All long-only. The option flags are valid only in conversion mode (error otherwise). Out of range: error, as for
`--heif/--webp/--jxl` today. Error messages and help text list AVIF with the other formats.

## Phases

Each phase ends with gate (golden 0 mismatches on the existing cases), coverage checker PASS and ASan clean.

1. AVIF output: enum, registry row (`.avif`), shared libheif encode path for HEVC/AV1, runtime capability check,
   `--avif`, tests (round trip, alpha, `img2_can_encode`), golden cases with `--threads 1` if aom output turns out
   thread-dependent.
2. Options: struct + mapping table + warnings, CLI flags, per-format tests (lossless round trips by `memcmp`, chroma
   checked by decoding the file's sampling info, effort/threads checked by "valid and decodable", not by size order).
   Links `jxl_threads`.
3. Metadata: raw retention + `orientation` field in decoders, setter, copy on scale/orient, the five writers,
   libwebpmux. Tests: encode with `--keep-metadata`, decode, compare parsed fields, Orientation = 1 after an oriented
   JPEG, size-cap and marker-limit paths.

## Measurements

Phase 1, 2026-11-23, vendored libheif + aom 3.15.3, RelWithDebInfo build, 32-core x86-64 host (light load).
`img2_encode()` with default options except `quality` (AVIF: aom speed 6, 4:2:0, threads = 32, tune auto; HEIF:
kvazaar defaults). PSNR over R, G, B of the decoded result against the decoded source (`img2_decode()` of the test
file), alpha-premultiplied for the image with alpha (fully transparent pixels do not count); time = fastest of 3
encodes. Tool: a small C program against `build/fat/libimg2.a` (not committed). Images from `src/tests/data`.

Cells: size / PSNR / encode time.

| Setting | goldhill (photo, 720x576) | DSCN0010 (photo, 640x480) | hell (graphic + alpha, 512x512) | example (photo, 300x225) | BSG1 (scan, 635x348) |
|---|---|---|---|---|---|
| HEIF q80 | 280 KiB / 39.90 dB / 131 ms | 294 KiB / 32.28 dB / 112 ms | 183 KiB / 35.89 dB / 118 ms | 40 KiB / 47.30 dB / 42 ms | 63 KiB / 43.57 dB / 61 ms |
| AVIF q30 | 11 KiB / 29.59 dB / 139 ms | 18 KiB / 22.71 dB / 150 ms | 8 KiB / 31.13 dB / 128 ms | 3 KiB / 29.42 dB / 48 ms | 5 KiB / 32.79 dB / 91 ms |
| AVIF q40 | 18 KiB / 30.85 dB / 156 ms | 30 KiB / 24.08 dB / 169 ms | 11 KiB / 31.92 dB / 134 ms | 4 KiB / 30.95 dB / 48 ms | 7 KiB / 34.65 dB / 92 ms |
| AVIF q50 | 29 KiB / 32.24 dB / 194 ms | 52 KiB / 26.21 dB / 196 ms | 17 KiB / 32.66 dB / 141 ms | 6 KiB / 32.91 dB / 51 ms | 10 KiB / 36.73 dB / 99 ms |
| AVIF q60 | 45 KiB / 33.64 dB / 226 ms | 77 KiB / 28.29 dB / 211 ms | 27 KiB / 33.28 dB / 157 ms | 9 KiB / 35.14 dB / 61 ms | 13 KiB / 38.50 dB / 105 ms |
| AVIF q70 | 66 KiB / 34.90 dB / 216 ms | 107 KiB / 30.07 dB / 241 ms | 41 KiB / 33.93 dB / 146 ms | 13 KiB / 37.56 dB / 59 ms | 17 KiB / 40.04 dB / 100 ms |
| **AVIF q80 (default)** | 96 KiB / 36.25 dB / 241 ms | 135 KiB / 31.10 dB / 257 ms | 61 KiB / 34.64 dB / 169 ms | 17 KiB / 39.86 dB / 66 ms | 22 KiB / 41.24 dB / 99 ms |
| AVIF q90 | 147 KiB / 37.76 dB / 292 ms | 176 KiB / 31.79 dB / 293 ms | 96 KiB / 35.27 dB / 193 ms | 24 KiB / 43.55 dB / 84 ms | 31 KiB / 42.36 dB / 110 ms |
| AVIF q95 | 202 KiB / 38.93 dB / 343 ms | 217 KiB / 32.10 dB / 272 ms | 133 KiB / 35.64 dB / 234 ms | 29 KiB / 45.91 dB / 102 ms | 43 KiB / 43.02 dB / 131 ms |
| AVIF q96 | 224 KiB / 39.27 dB / 308 ms | 232 KiB / 32.16 dB / 276 ms | 146 KiB / 35.73 dB / 238 ms | 31 KiB / 46.55 dB / 101 ms | 48 KiB / 43.21 dB / 117 ms |
| AVIF q97 | 224 KiB / 39.27 dB / 323 ms | 232 KiB / 32.16 dB / 278 ms | 147 KiB / 35.73 dB / 230 ms | 31 KiB / 46.55 dB / 103 ms | 48 KiB / 43.21 dB / 132 ms |
| AVIF q98 | 247 KiB / 39.54 dB / 313 ms | 256 KiB / 32.23 dB / 293 ms | 162 KiB / 35.80 dB / 243 ms | 34 KiB / 47.05 dB / 103 ms | 54 KiB / 43.38 dB / 146 ms |
| AVIF q99 (PSNR match) | 273 KiB / 39.75 dB / 322 ms | 286 KiB / 32.26 dB / 284 ms | 180 KiB / 35.86 dB / 246 ms | 37 KiB / 47.50 dB / 102 ms | 64 KiB / 43.58 dB / 158 ms |
| AVIF q100 | 324 KiB / 40.34 dB / 271 ms | 339 KiB / 32.35 dB / 196 ms | 214 KiB / 36.01 dB / 205 ms | 47 KiB / 49.07 dB / 74 ms | 90 KiB / 44.13 dB / 135 ms |

Alpha of hell.png (separate aux image, aom tune PSNR): 46.6 dB at q30, 56.2 dB at q80, 67.4 dB at q99 (HEIF q80:
66.6 dB).

Choice:

- **PSNR rule: quality 99.** It is the closest match to the HEIF q80 PSNR on every image (deltas 0.01-0.20 dB; the
  next best, q98 on every image, is 0.05-0.36 dB off). The match is only reached at the top of the scale because
  kvazaar at q80 spends 2.3-7.8 bpp; consequently AVIF at q99 is about as large as HEIF at q80
  (0.94-1.02x). The PSNR rule also undervalues aom's default `tune=iq`, which optimises perceived quality rather
  than PSNR; users who want small files pick a lower quality (q80 is one third to one half of the HEIF q80 size). A
  different rule (e.g. matching SSIM, or a size target) would give a much lower default; revisit together with
  `effort` in phase 2 if wanted.
- **Default quality 80 (user override of the PSNR rule, 2026-11-23).** The PSNR match against kvazaar q80 lands at
  the top of the aom scale with no size win (q99: 0.94-1.02x the HEIF q80 size). q80 gives one third to one half of
  the HEIF q80 size (goldhill 96 vs 280 KiB, 36.25 vs 39.90 dB) and keeps the same default number as
  HEIF/WebP/JXL. `--avif 99` remains the PSNR-equivalent of `--heif 80`.
- **Speed 6 kept** (libheif default): AVIF q99 takes 2.1-2.6x the HEIF q80 encode time, below the ~3x threshold;
  the default q80 takes 1.4-2.3x.

Determinism (aom, the same image encoded repeatedly, md5/FNV of the output): identical run to run for every thread
count. Across thread counts the output is identical for every `threads >= 2` (2, 4, 8, 16, 32 and the default) but
differs at `threads = 1` (libheif enables aom row-MT only when threads > 1; e.g. goldhill q99: 279316 bytes with
>= 2 threads, 278907 with 1). The default thread count is `std::thread::hardware_concurrency()` (the number of
online CPUs, not the affinity mask: `taskset -c 0` gives the same bytes). So the golden AVIF cases use the default
options: they are stable on any host with at least 2 CPUs; phase 2's `--threads 1` would change their bytes.

## Phase 2 results

Implemented as in the mapping table, with these facts and refinements (2026-11-23, vendored libraries):

- **Internal API.** `img2_encode_fn` takes `(img, native_quality, opts, ...)`; `img2_encode()` validates `opts` and
  resolves the native quality as before, the encoder reads the other fields (`opts->quality < 0` tells PNG that
  the level is the registry default, decision 3). The 4-argument `img2_encode_<x>()` / `img2_encoder_dispatch()`
  entry points stay (default options) next to the `_opts` / `img2_encoder_dispatch_opts()` forms.
- **Warnings.** One `IMG2_LOG_WARN` per unhonoured option, `encode: <option> has no effect for <format> (<why>)`;
  `chroma 420` is never warned for the 4:2:0-only formats (it is what they do). Beyond the table: with lossless
  WebP, JXL or AVIF, `alpha_quality` warns (alpha is lossless), lossless AVIF warns for chroma 420/422
  (always 4:4:4), and lossless JXL warns for `progressive` (decision 7). PNG `lossless` and explicit
  `chroma 420` on JPEG/WebP/HEIF/AVIF are silent no-ops.
- **Metadata (phase 3).** The `IMG2_META_*` bits are defined and validated (unknown bits: `IMG2_E_ARG`); a nonzero
  `metadata` logs one WARN `encode: metadata is not supported yet (ignored)` and the encode proceeds.
- **WebP config equivalence.** `WebPConfig` + `WebPEncode` set up as libwebp's simple API does
  (`picture_enc.c`: `WEBP_PRESET_DEFAULT` with the quality, `use_argb = lossless`; lossless with quality factor
  70) gives the same bytes: golden 0 mismatches on the WebP cases at S25, and `test_encode_options` compares
  lossy output against `WebPEncodeRGBA` (q 0/50/80/99) directly. `lossless` keeps that quality factor 70 (so
  `{ .lossless = true }` equals `quality = 100`); `effort` only changes `method`. Since S25b lossless sets
  `exact` (decision 6), so it no longer equals `WebPEncodeLosslessRGBA` when the image has fully transparent
  pixels; every RGBA byte round-trips (tested with an alpha-0 block of non-black RGB).
- **AVIF lossless.** aom lossless + `chroma` "444" + an output NCLX profile with `matrix_coefficients` 0 (GBR,
  identity), BT.709 primaries, sRGB transfer, full range (written as a `colr` nclx box) round-trips RGB and alpha
  bit-exactly (`memcmp` after `img2_decode()`, alpha included). libheif's aom plugin switches `tune=auto` from
  `iq` to `ssim` for lossless by itself (aom rejects `iq` with lossless), so no tune parameter is set.
- **AVIF effort.** speed = 10 - effort, so effort 4 equals the default speed 6 (same bytes); effort 1..10 maps to
  speed 9..0 without clamping. `threads` sets aom `threads`; as measured in phase 1, `threads = 1` (row-MT off)
  gives different (deterministic) bytes than any count >= 2, which all equal the default.
- **JXL threads.** `threads >= 2` installs a `JxlThreadParallelRunner` with that many workers; 0 and 1 stay
  single-threaded without a runner (default bytes unchanged). The output does not depend on the thread count:
  goldhill (720x576, 9 groups) at quality 80 and 100, effort 3, 6 and 9, threads 0/1/2/3/4/8/16/32/64, twice
  each: one byte stream per (quality, effort). `jxl_threads` is linked (vendored archive in the fat archive and
  the manifest note; the host path requires `libjxl_threads` next to `libjxl`).
- **JPEG.** `chroma` sets the luma sampling factors (420: 2x2, 422: 2x1, 444: 1x1; Cb/Cr 1x1); `progressive`
  calls `jpeg_simple_progression()`; effort >= 6 sets `optimize_coding` (effort 1..5 leave the default).
- **JXL progressive** (S25b, decision 7). Lossy: `JXL_ENC_FRAME_SETTING_PROGRESSIVE_AC` = 1. Fed to libjxl's
  decoder in chunks with `JxlDecoderSetProgressiveDetail(kPasses)`, goldhill q80 reports 3 progression steps
  (DC + 2 AC passes) instead of 1 (DC), +2.6% size; the default output is unchanged. Lossless: one WARN.
- **PNG.** `progressive` = Adam7 (libpng's `png_write_image()` runs the passes). Through the CLI `--png N` always
  sets the quality, so `--png N --effort E` is a CLI error (decision 5); the API keeps decision 3.
- **CLI.** `--lossless`, `--effort N`, `--chroma 420|422|444`, `--alpha-quality N`, `--progressive`,
  `--threads N`: ranges checked while parsing ("Error: --effort must be 1-10 (got '11')"); outside conversion
  mode `validate_options()` rejects them ("Error: --effort can only be used with --jpeg, --png, --heif, --webp,
  --jxl or --avif"). `--keep-metadata` is phase 3.
- **Golden.** Existing cases: 0 mismatches in every output file; the only text change is `meta:--help` (seven new
  lines under "Conversion Options"). New cases (S25): progressive 4:4:4 JPEG, interlaced PNG with the effort
  warning, lossless WebP, JXL effort 3, JXL `--threads 4` (same bytes as the single-threaded `--jxl 100` case),
  AVIF 4:4:4 effort 4 and lossless (default thread count), the HEIF chroma/threads warnings, and the two CLI
  errors (conversion-only flag, out-of-range effort).
  S25b: `enc:--png 9 --effort 10 --progressive` replaced by `enc:--png 9 --progressive` (same output bytes) and
  the error case `meta:--png 9 --effort 10`; new `enc:--jxl 80 --progressive`; `enc:--webp 80 --lossless` on
  `pal4_trns_interlaced.png` changes bytes (decision 6, the image has transparent pixels; `--webp 100` on the
  opaque goldhill keeps its bytes); `meta:--help` text.

## Decisions (2026-11-23)

1. Orientation: the `img->orientation` field. Default 1 (upright) for images created by the caller
   (`img2_image_create` and friends), so a caller-built image never gets a stale rotation written.
2. AVIF default quality and effort: measured in phase 1. For q in a sweep, record file size, PSNR against the source
   and encode time on several test images; the default quality is the one whose PSNR matches HEIF at q80, the default
   effort is libheif's speed 6 unless the encode time is unreasonable. The numbers go into this document.
   Result ([Measurements](#measurements)): the PSNR rule gives quality 99 (closest PSNR to HEIF q80 on all five
   images); speed **6** kept (2.1-2.6x the HEIF encode time). **Overridden by the user (Zoltán, 2026-11-23): default
   quality 80.** The PSNR match against kvazaar q80 lands at the top of the aom scale with no size win; q80 gives
   about a third of the HEIF q80 size and keeps the same default number as HEIF/WebP/JXL.
3. PNG `effort`: mapped to the zlib level when `quality` is left at default (-1): level = round((e-1)*9/9) clamped
   0..9, i.e. effort 1 -> level 0 ... effort 10 -> level 9. An explicit `quality` wins (and `effort` then logs the usual
   no-effect warning).
4. Raw metadata cap: 1 MiB each for EXIF and XMP.
5. (Zoltán, 2026-11-23, after phase 2) CLI `--png N` with `--effort`: an error (exit 1, "Error: --effort has no
   effect with --png N (N is already the zlib level)"), since `--png` always carries the level. The API keeps
   decision 3 (effort maps to the level only when `quality` is -1, else a WARN).
6. (Zoltán, 2026-11-23) Lossless WebP sets `config.exact = 1`, so the RGB under alpha 0 is kept and lossless means
   every RGBA byte. Lossy WebP is unchanged. This is the one approved exception to the byte-identical rule: lossless
   WebP of images with fully transparent pixels (`--webp 100`, `--lossless`) changes bytes.
7. (Zoltán, 2026-11-23) JXL `progressive` maps to `JXL_ENC_FRAME_SETTING_PROGRESSIVE_AC` = 1 for lossy (VarDCT)
   output; no WARN. Checked alternatives: `QPROGRESSIVE_AC` is not needed (`PROGRESSIVE_AC` alone already gives the
   decoder AC passes, and cjxl's `--progressive` also uses spectral `progressive_ac`, not `qprogressive_ac`).
   Lossless JXL is modular, where `PROGRESSIVE_AC` has no effect; `RESPONSIVE` = 1 (squeeze) was measured on
   goldhill: +21% size (485 -> 589 KB) and libjxl's decoder reports no progression step for it either way, so
   lossless + `progressive` logs `encode: progressive has no effect for lossless JXL (no VarDCT passes)`.
8. (Zoltán, 2026-11-23) The AVIF effort mapping stays linear, speed = 10 - effort (effort 4 = the default speed 6),
   although the other formats' defaults sit near effort 7.
