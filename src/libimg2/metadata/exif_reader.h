/**
 * @file exif_reader.h
 * @brief Pure C EXIF/XMP metadata parser (TinyEXIF port)
 *
 * Extracts EXIF and XMP metadata from multiple image formats (JPEG, PNG, WebP, HEIF, JXL, TIFF, RAW).
 * Read-only implementation with zero external dependencies.
 *
 * Public (<img2/metadata.h>): struct img2_exif / img2_xmp, img2_exif_init/clear, img2_xmp_init/clear,
 * img2_exif_parse_jpeg/tiff, img2_xmp_parse_jpeg/xml. Internal aliases below.
 */

#ifndef IMG2_INTERNAL_METADATA_EXIF_READER_H
#define IMG2_INTERNAL_METADATA_EXIF_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <img2/metadata.h>

#include "../core/status.h"

/**
 * @brief Parse EXIF metadata from JPEG data (internal alias of img2_exif_parse_jpeg())
 *
 * @param exif Output structure for EXIF data
 * @param jpeg_data JPEG file data buffer
 * @param len Length of jpeg_data in bytes
 * @return IMG2_OK on success; IMG2_E_ARG (NULL argument), IMG2_E_NOMEM, or IMG2_E_FORMAT on malformed data or no EXIF data found
 */
img2_status_t img2_exif_parse(img2_exif_t *exif, const uint8_t *jpeg_data, size_t len);

/**
 * @brief Parse XMP metadata from JPEG data (internal alias of img2_xmp_parse_jpeg())
 *
 * @param xmp Output structure for XMP data
 * @param jpeg_data JPEG file data buffer
 * @param len Length of jpeg_data in bytes
 * @return IMG2_OK on success; IMG2_E_ARG (NULL argument), IMG2_E_NOMEM, or IMG2_E_FORMAT on malformed data or no XMP data found
 */
img2_status_t img2_xmp_parse(img2_xmp_t *xmp, const uint8_t *jpeg_data, size_t len);

#endif /* IMG2_INTERNAL_METADATA_EXIF_READER_H */
