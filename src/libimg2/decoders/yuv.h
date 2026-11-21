/**
 * @file yuv.h
 * @brief Raw (headerless) 8-bit YUV input: format descriptors, spec parsing, decoding
 *
 * Raw YUV has no magic bytes, so it is never auto-detected. The caller must
 * supply the frame geometry explicitly (CLI: --yuv format:w:h[:y_stride][:uv_stride]).
 *
 * Memory layout (single frame, planes stored back to back, no gaps beyond strides):
 *   planar      (i420, i422, i444): Y plane, U plane, V plane
 *   planar      (yv12)            : Y plane, V plane, U plane
 *   semi-planar (nv12, nv21)      : Y plane, interleaved UV (nv12) / VU (nv21) plane
 *   packed      (yuyv, uyvy)      : one plane of 4-byte macropixels (2 pixels each)
 *   gray        (gray/y8/y800)    : Y plane only
 *
 * Each plane occupies stride * rows bytes, except the very last row of the
 * last plane, of which only the visible bytes must be present (lenient size).
 *
 * Conversion: BT.601 limited range (Y 16-235, C 16-240), fixed-point integer
 * math, nearest-neighbour (replicated) chroma upsampling, alpha = 255.
 */

#ifndef IMGCAT2_YUV_H
#define IMGCAT2_YUV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../core/image.h"

/**
 * @enum yuv_format_t
 * @brief Supported raw 8-bit YUV pixel layouts
 */
typedef enum {
	YUV_FMT_NONE = 0, /**< Not set / invalid */
	YUV_FMT_I420, /**< Planar 4:2:0, Y + U + V (aka yuv420p) */
	YUV_FMT_YV12, /**< Planar 4:2:0, Y + V + U */
	YUV_FMT_NV12, /**< Semi-planar 4:2:0, Y + interleaved UV */
	YUV_FMT_NV21, /**< Semi-planar 4:2:0, Y + interleaved VU */
	YUV_FMT_I422, /**< Planar 4:2:2, Y + U + V (aka yuv422p) */
	YUV_FMT_I444, /**< Planar 4:4:4, Y + U + V (aka yuv444p) */
	YUV_FMT_YUYV, /**< Packed 4:2:2, Y0 U Y1 V (aka yuy2) */
	YUV_FMT_UYVY, /**< Packed 4:2:2, U Y0 V Y1 */
	YUV_FMT_GRAY, /**< Luma only (aka y8, y800) */
} yuv_format_t;

/**
 * @struct yuv_params_t
 * @brief Geometry of a raw YUV frame
 *
 * A stride of 0 means "minimal" (tightly packed) for that plane.
 */
typedef struct {
	yuv_format_t format; /**< Pixel layout */
	uint32_t width; /**< Width in pixels (> 0) */
	uint32_t height; /**< Height in pixels (> 0) */
	size_t y_stride; /**< Bytes per luma row (per packed row for yuyv/uyvy); 0 = minimal */
	size_t uv_stride; /**< Bytes per chroma row (planar / semi-planar only); 0 = minimal */
} yuv_params_t;

/**
 * @brief Look up a format by name (case-insensitive, aliases accepted)
 *
 * @param name Format name, e.g. "i420", "YUV420P", "nv12", "y800"
 * @return Format, or YUV_FMT_NONE if unknown
 */
yuv_format_t yuv_format_from_name(const char *name);

/**
 * @brief Canonical lower-case name of a format
 *
 * @param fmt Format
 * @return Canonical name (e.g. "i420"), or "unknown"
 */
const char *yuv_format_name(yuv_format_t fmt);

/**
 * @brief Parse a "format:w:h[:y_stride][:uv_stride]" specification
 *
 * Validates everything that can be validated without data: known format,
 * non-zero dimensions within IMAGE_MAX_DIMENSION / IMAGE_MAX_PIXELS, digits-only
 * numbers without overflow, strides not below the format minimum, uv_stride
 * only for formats that have chroma planes. Empty optional fields mean default.
 * On success the strides in @p out are resolved to explicit byte values.
 *
 * @param spec    Specification string
 * @param out     Output parameters (written only on success)
 * @param err     Optional buffer for a human-readable error message
 * @param err_len Size of @p err
 * @return 0 on success, -1 on error
 */
int yuv_parse_spec(const char *spec, yuv_params_t *out, char *err, size_t err_len);

/**
 * @brief Number of bytes required for one frame (lenient: last row may be short)
 *
 * @param params   Frame geometry
 * @param out_size Output: required byte count
 * @return true on success, false if params are invalid or the size overflows
 */
bool yuv_frame_size(const yuv_params_t *params, size_t *out_size);

/**
 * @brief Decode the first raw YUV frame in @p data to RGBA8888
 *
 * Bytes beyond the first frame are ignored (frame_count is always 1).
 *
 * @param data        Raw frame data
 * @param len         Length of data in bytes
 * @param params      Frame geometry
 * @param frame_count Output: 1 on success, 0 on error
 * @return Array with one image_t*, or NULL on error (short input, bad params)
 */
image_t **decode_yuv(const uint8_t *data, size_t len, const yuv_params_t *params, int *frame_count);

#endif /* IMGCAT2_YUV_H */
