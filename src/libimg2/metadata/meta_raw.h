/**
 * @file meta_raw.h
 * @brief Internal: raw EXIF / XMP blocks of an image (retention at decode, copy, orientation patch for encoders)
 *
 * img2_image_t carries the raw blocks next to the parsed structs (docs/encoder-api.md "Metadata passthrough"):
 * exif_raw in TIFF-header form ("II*\0" / "MM\0*", container prefixes stripped), xmp_raw NUL-terminated (the
 * terminator is not counted in xmp_raw_len), each capped at IMG2_META_RAW_CAP. orientation is the EXIF
 * orientation still to be applied to the pixels (1 = upright).
 */

#ifndef IMG2_INTERNAL_METADATA_META_RAW_H
#define IMG2_INTERNAL_METADATA_META_RAW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <img2/image.h>
#include <img2/metadata.h>

#include "../core/status.h"

/** Cap of each raw block (decision 4: 1 MiB) */
#define IMG2_META_RAW_CAP ((size_t)IMG2_MAX_METADATA_SIZE)

/** @brief true if p holds at least 8 bytes starting with a TIFF header ("II*\0" or "MM\0*") */
bool img2_meta_is_tiff(const uint8_t *p, size_t len);

/**
 * @brief Decoder side: keep a copy of an EXIF block (TIFF-header form) in img->exif_raw, replacing any earlier one
 *
 * A block over IMG2_META_RAW_CAP is dropped with one WARN naming @p fmt; a block that is not in TIFF-header form is
 * dropped silently, as is a block whose copy cannot be allocated (metadata is best-effort: the decode succeeds).
 */
void img2_meta_keep_exif(img2_image_t *img, const char *fmt, const uint8_t *tiff, size_t len);

/** @brief Decoder side: keep a NUL-terminated copy of an XMP packet in img->xmp_raw (same cap / WARN rules; empty: dropped) */
void img2_meta_keep_xmp(img2_image_t *img, const char *fmt, const uint8_t *xml, size_t len);

/** @brief Free the raw blocks of img and set them to NULL / 0 (orientation untouched) */
void img2_meta_clear(img2_image_t *img);

/** @brief EXIF orientation 1..8 of a parsed struct; 1 when exif is NULL or the value is outside 1..8 */
uint8_t img2_meta_orientation(const img2_exif_t *exif);

/**
 * @brief Copy the raw blocks and the orientation of src to dst (dst's blocks are replaced)
 *
 * @return IMG2_OK, or IMG2_E_NOMEM (dst unchanged)
 */
img2_status_t img2_meta_copy(img2_image_t *dst, const img2_image_t *src);

/**
 * @brief Rewrite the SHORT Orientation tag (0x0112) of IFD0 in place (both byte orders)
 *
 * @return true if IFD0 has the tag (with type SHORT and count 1) and it now holds @p orientation; false if the
 *         block has no such tag or is malformed (the block is not changed: the IFD is never grown)
 */
bool img2_meta_patch_orientation(uint8_t *tiff, size_t len, uint16_t orientation);

/**
 * @brief Encoder side: the EXIF block to write for img, a copy with IFD0 Orientation set to img->orientation
 *
 * img->orientation outside 1..8 (e.g. 0 in a zero-initialised caller-built struct) is written as 1. A block without
 * an Orientation tag is copied unchanged.
 *
 * @param out Output copy (free()), NULL if img has no EXIF block
 * @param len Output length (0 if none)
 * @return IMG2_OK (also when there is no block), or IMG2_E_NOMEM
 */
img2_status_t img2_meta_exif_for_write(const img2_image_t *img, uint8_t **out, size_t *len);

#endif /* IMG2_INTERNAL_METADATA_META_RAW_H */
