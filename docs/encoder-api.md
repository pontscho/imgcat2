# Encoder API v2: AVIF output and encoder options

Status: draft for review. Branch base: feature/split (img2/s23).

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
| lossless | warn | always lossless | `config.lossless` (q=100 still selects it) | `SetFrameLossless` (q>=95 still selects it) | warn (kvazaar lossless is 4:2:0 YCbCr, not RGB-lossless) | aom `lossless` + 4:4:4, identity matrix (to verify) |
| effort 1..10 | >= 6: `optimize_coding` | zlib level e-1 when quality is default (decision 3), else warn | `method` = round((e-1)*6/9) | `EFFORT` = e | warn (no preset parameter) | aom `speed` = 10 - e (clamped 0..9) |
| chroma | comp_info sampling factors | warn | 420 only: warn otherwise | warn (XYB) | 420 only: warn otherwise | aom `chroma` "420"/"422"/"444" |
| alpha_quality | warn | warn | `alpha_quality` | extra channel distance from quality | warn | aom `alpha-quality` |
| progressive | `jpeg_simple_progression` | Adam7 interlace | warn | warn | warn | warn |
| threads | warn | warn | `thread_level` = threads > 1 | parallel runner (links `jxl_threads`) | warn (kvazaar auto) | aom `threads` |
| metadata | APP1 Exif / XMP | eXIf / iTXt `XML:com.adobe.xmp` | VP8X EXIF / XMP chunks (needs libwebpmux) | container + `Exif` / `xml ` boxes | `heif_context_add_exif_metadata` / `_XMP_metadata` | same as HEIF |

Defaults stay as today: JPEG optimize off, 4:2:0, baseline; WebP method 4; JXL effort 7; AVIF uses the libheif aom
defaults (speed 6, 4:2:0, threads = hardware concurrency) with quality from the registry default.

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

## Decisions (2026-11-23)

1. Orientation: the `img->orientation` field. Default 1 (upright) for images created by the caller
   (`img2_image_create` and friends), so a caller-built image never gets a stale rotation written.
2. AVIF default quality and effort: measured in phase 1. For q in a sweep, record file size, PSNR against the source
   and encode time on several test images; the default quality is the one whose PSNR matches HEIF at q80, the default
   effort is libheif's speed 6 unless the encode time is unreasonable. The numbers go into this document.
3. PNG `effort`: mapped to the zlib level when `quality` is left at default (-1): level = round((e-1)*9/9) clamped
   0..9, i.e. effort 1 -> level 0 ... effort 10 -> level 9. An explicit `quality` wins (and `effort` then logs the usual
   no-effect warning).
4. Raw metadata cap: 1 MiB each for EXIF and XMP.
