/**
 * @file yuv.h
 * @brief libimg2 raw (headerless) 8-bit YUV input: formats, geometry, spec parsing
 *
 * Raw YUV has no magic bytes, so it is never auto-detected: decode it with
 * img2_decode_opts_t.force_mime = IMG2_MIME_YUV and img2_decode_opts_t.yuv set.
 *
 * Memory layout (single frame, planes stored back to back, no gaps beyond strides):
 *   planar      (i420, i422, i444): Y plane, U plane, V plane
 *   planar      (yv12)            : Y plane, V plane, U plane
 *   semi-planar (nv12, nv21)      : Y plane, interleaved UV (nv12) / VU (nv21) plane
 *   packed      (yuyv, uyvy)      : one plane of 4-byte macropixels (2 pixels each)
 *   gray        (gray/y8/y800)    : Y plane only
 *
 * Conversion: BT.601 limited range, nearest-neighbour chroma upsampling, alpha = 255.
 */

#ifndef IMG2_YUV_H
#define IMG2_YUV_H

#include <img2/common.h>

IMG2_BEGIN_DECLS

/**
 * @brief Supported raw 8-bit YUV pixel layouts
 */
typedef enum img2_yuv_format {
	IMG2_YUV_FMT_NONE = 0,    /**< Not set / invalid */
	IMG2_YUV_FMT_I420,        /**< Planar 4:2:0, Y + U + V (aka yuv420p) */
	IMG2_YUV_FMT_YV12,        /**< Planar 4:2:0, Y + V + U */
	IMG2_YUV_FMT_NV12,        /**< Semi-planar 4:2:0, Y + interleaved UV */
	IMG2_YUV_FMT_NV21,        /**< Semi-planar 4:2:0, Y + interleaved VU */
	IMG2_YUV_FMT_I422,        /**< Planar 4:2:2, Y + U + V (aka yuv422p) */
	IMG2_YUV_FMT_I444,        /**< Planar 4:4:4, Y + U + V (aka yuv444p) */
	IMG2_YUV_FMT_YUYV,        /**< Packed 4:2:2, Y0 U Y1 V (aka yuy2) */
	IMG2_YUV_FMT_UYVY,        /**< Packed 4:2:2, U Y0 V Y1 */
	IMG2_YUV_FMT_GRAY         /**< Luma only (aka y8, y800) */
} img2_yuv_format_t;

/**
 * @brief Geometry of a raw YUV frame (a stride of 0 means "minimal" for that plane)
 */
typedef struct img2_yuv_params {
	img2_yuv_format_t format;  /**< Pixel layout */
	uint32_t width;            /**< Width in pixels (> 0) */
	uint32_t height;           /**< Height in pixels (> 0) */
	size_t y_stride;           /**< Bytes per luma row (per packed row for yuyv/uyvy); 0 = minimal */
	size_t uv_stride;          /**< Bytes per chroma row (planar / semi-planar only); 0 = minimal */
} img2_yuv_params_t;

/**
 * @brief Look up a format by name (case-insensitive, aliases accepted)
 *
 * @param name Format name, e.g. "i420", "YUV420P", "nv12", "y800"
 * @return Format, or IMG2_YUV_FMT_NONE if unknown
 */
img2_yuv_format_t img2_yuv_format_from_name(const char *name);

/**
 * @brief Canonical lower-case name of a format
 *
 * @param fmt Format
 * @return Canonical name (e.g. "i420"), or "unknown"
 */
const char *img2_yuv_format_name(img2_yuv_format_t fmt);

/**
 * @brief Parse a "format:w:h[:y_stride][:uv_stride]" specification
 *
 * Validates everything that can be validated without data: known format,
 * non-zero dimensions within IMG2_MAX_DIMENSION / IMG2_MAX_PIXELS, digits-only
 * numbers without overflow, strides not below the format minimum, uv_stride
 * only for formats that have chroma planes. Empty optional fields mean default.
 * On success the strides in @p out are resolved to explicit byte values.
 *
 * @param spec    Specification string
 * @param out     Output parameters (written only on success)
 * @param err     Optional buffer for a human-readable error message
 * @param err_len Size of @p err
 * @return IMG2_OK on success, IMG2_E_ARG on error
 */
img2_status_t img2_yuv_parse_spec(const char *spec, img2_yuv_params_t *out, char *err, size_t err_len);

/**
 * @brief Number of bytes required for one frame (lenient: the last row may be short)
 *
 * @param params   Frame geometry
 * @param out_size Output: required byte count
 * @return true on success, false if params are invalid or the size overflows
 */
bool img2_yuv_frame_size(const img2_yuv_params_t *params, size_t *out_size);

IMG2_END_DECLS

#endif /* IMG2_YUV_H */
