/**
 * @file image.h
 * @brief libimg2 image model: RGBA8888 images, frame sets, scaling, orientation
 */

#ifndef IMG2_IMAGE_H
#define IMG2_IMAGE_H

#include <img2/common.h>

IMG2_BEGIN_DECLS

/** Maximum width or height per axis (pixels) */
#define IMG2_MAX_DIMENSION 65536
/** Maximum total pixels per image (200 megapixels) */
#define IMG2_MAX_PIXELS 200000000UL
/** Maximum input buffer size accepted by img2_decode() (1 GiB) */
#define IMG2_MAX_FILE_SIZE 1073741824UL

/** EXIF metadata (defined in <img2/metadata.h>) */
typedef struct img2_exif img2_exif_t;
/** XMP metadata (defined in <img2/metadata.h>) */
typedef struct img2_xmp img2_xmp_t;

/**
 * @brief RGBA8888 image
 *
 * Row-major, top-to-bottom; pixel (x, y) starts at pixels + y * stride + x * 4.
 * In 1.0 the library only accepts packed rows: stride must equal width * 4
 * (any other stride is rejected with IMG2_E_ARG).
 */
typedef struct img2_image {
	uint32_t width;           /**< Width in pixels */
	uint32_t height;          /**< Height in pixels */
	uint32_t stride;          /**< Bytes per row; must be == width * 4 in 1.0 (others -> IMG2_E_ARG) */
	uint8_t *pixels;          /**< RGBA8888, non-premultiplied */
	uint32_t delay_ms;        /**< Frame duration; 0 = unknown / not animated */
	img2_exif_t *exif;        /**< EXIF metadata, NULL if none; owned by the image */
	img2_xmp_t *xmp;          /**< XMP metadata, NULL if none; owned by the image */
	/* Raw metadata (docs/libimg2.md "Metadata writing"); owned by the image, freed by img2_image_destroy() */
	uint8_t *exif_raw;        /**< EXIF block in TIFF-header form ("II*\0" / "MM\0*"), container prefixes stripped; NULL if none */
	size_t exif_raw_len;      /**< Length of exif_raw in bytes */
	char *xmp_raw;            /**< XMP packet, NUL-terminated; NULL if none */
	size_t xmp_raw_len;       /**< Length of xmp_raw in bytes (without the terminator) */
	uint8_t orientation;      /**< EXIF orientation still to be applied to the pixels, 1..8 (1 = upright) */
} img2_image_t;

/** Cap of each raw metadata block (img2_image_t.exif_raw / xmp_raw): 1 MiB */
#define IMG2_MAX_METADATA_SIZE 1048576UL

/**
 * @brief Decoded frame set (count >= 1 on success)
 *
 * Timing convention (filled by img2_decode for GIF, APNG, WebP, JPEG XL and
 * HEIF/AVIF sequence tracks): loop_count 0 = loop forever or unknown, N > 0 =
 * play N times (a GIF without a NETSCAPE2.0 extension reports 1); delay_ms 0 =
 * unknown or not animated. Single-frame images report delay_ms = 0 and
 * loop_count = 0. HEIF/AVIF top-level image lists report 0 for both.
 */
typedef struct img2_frames {
	img2_image_t **frames;    /**< Array of count frames, owned by the set */
	int count;                /**< Number of frames */
	int loop_count;           /**< 0 = loop forever / unknown; N > 0 = play N times */
} img2_frames_t;

/**
 * @brief Resampling filter for img2_scale() / img2_scale_fit()
 */
typedef enum img2_filter {
	IMG2_FILTER_DEFAULT = 0,  /**< stb_image_resize2 sRGB default (imgcat2 "lanczos") */
	IMG2_FILTER_POINT,        /**< Nearest neighbour */
	IMG2_FILTER_BOX,          /**< Box filter */
	IMG2_FILTER_TRIANGLE,     /**< Bilinear (triangle) filter */
	IMG2_FILTER_CUBIC,        /**< Catmull-Rom (interpolating cubic spline) */
	IMG2_FILTER_MITCHELL      /**< Mitchell-Netravali */
} img2_filter_t;

/**
 * @brief Create a zeroed (transparent black) RGBA image, stride = width * 4
 *
 * @param width  Width in pixels
 * @param height Height in pixels
 * @param out    Output image (free with img2_image_destroy()); *out = NULL on error
 * @return IMG2_OK; IMG2_E_ARG (out NULL, width or height 0); IMG2_E_LIMIT (> IMG2_MAX_DIMENSION
 *         or > IMG2_MAX_PIXELS); IMG2_E_NOMEM
 */
img2_status_t img2_image_create(uint32_t width, uint32_t height, img2_image_t **out);

/**
 * @brief Destroy an image: frees pixels, exif, xmp, the raw metadata blocks and the struct
 *
 * @param img Image (NULL-safe)
 */
void img2_image_destroy(img2_image_t *img);

/**
 * @brief Replace the raw metadata blocks of an image (copies; the parsed exif / xmp structs and the orientation
 *        field are not touched)
 *
 * Images from img2_image_create() start with no raw blocks and orientation 1; decoded images carry the blocks the
 * decoder found. img2_encode() writes them when img2_encode_opts_t.metadata asks for them.
 *
 * @param img      Image
 * @param exif     EXIF block in TIFF-header form (starts with "II*\0" or "MM\0*", at least 8 bytes), or NULL to remove
 *                 the EXIF block (exif_len is then ignored)
 * @param exif_len Length of exif in bytes
 * @param xmp      XMP packet (need not be NUL-terminated; a copy with a terminator is stored), or NULL to remove the
 *                 XMP block (xmp_len is then ignored)
 * @param xmp_len  Length of xmp in bytes (> 0)
 * @return IMG2_OK; IMG2_E_ARG (img NULL, EXIF not in TIFF-header form, xmp_len 0); IMG2_E_LIMIT (a block over
 *         IMG2_MAX_METADATA_SIZE); IMG2_E_NOMEM. On error the image is unchanged.
 */
img2_status_t img2_image_set_metadata(img2_image_t *img, const uint8_t *exif, size_t exif_len, const char *xmp, size_t xmp_len);

/**
 * @brief Destroy all frames of a set, free the array and zero *frames
 *
 * @param frames Frame set (NULL-safe)
 */
void img2_frames_free(img2_frames_t *frames);

/**
 * @brief Scale an image to exact dimensions (aspect ratio not preserved)
 *
 * @param src    Source image (stride must be width * 4)
 * @param width  Target width (> 0)
 * @param height Target height (> 0)
 * @param filter Resampling filter
 * @param out    Output image (free with img2_image_destroy()); *out = NULL on error
 * @return IMG2_OK; IMG2_E_ARG (invalid source, zero target, unknown filter value); IMG2_E_LIMIT
 *         (target over the limits); IMG2_E_NOMEM; IMG2_E_CODEC (stb_image_resize2 failed)
 */
img2_status_t img2_scale(const img2_image_t *src, uint32_t width, uint32_t height, img2_filter_t filter, img2_image_t **out);

/**
 * @brief Aspect-preserving scale into max_w x max_h (img2_fit_size() + scale)
 *
 * @param src    Source image (stride must be width * 4)
 * @param max_w  Maximum target width (> 0)
 * @param max_h  Maximum target height (> 0)
 * @param filter Resampling filter
 * @param out    Output image (free with img2_image_destroy()); *out = NULL on error
 * @return Same codes as img2_scale()
 */
img2_status_t img2_scale_fit(const img2_image_t *src, uint32_t max_w, uint32_t max_h, img2_filter_t filter, img2_image_t **out);

/**
 * @brief Aspect-preserving fit of src_w x src_h into max_w x max_h
 *
 * @param src_w Source width
 * @param src_h Source height
 * @param max_w Maximum width
 * @param max_h Maximum height
 * @param out_w Output width (1 <= *out_w <= max_w on success)
 * @param out_h Output height (1 <= *out_h <= max_h on success)
 * @return IMG2_OK, or IMG2_E_ARG (zero argument or NULL output). A side that rounds to 0 is clamped to 1.
 */
img2_status_t img2_fit_size(uint32_t src_w, uint32_t src_h, uint32_t max_w, uint32_t max_h, uint32_t *out_w, uint32_t *out_h);

/**
 * @brief Apply (*img)->orientation to the pixels and set it to 1 (no-op if it is already 1)
 *
 * May replace the pixel buffer and swap width/height. Decoders set the field to the EXIF orientation for JPEG,
 * PNG, WebP and RAW, and to 1 for TIFF/HEIF/AVIF/JPEG XL, whose codecs orient the pixels themselves (JPEG XL: by
 * the codestream orientation; an Exif box orientation is not authoritative there). (*img)->exif->orientation keeps
 * the file's value. A field outside 1..8 (e.g. 0 in a zero-initialised caller-built struct) counts as 1. The raw
 * metadata blocks stay with the image; the encoders write Orientation = the field into their copy of the EXIF block.
 *
 * @param img Image to transform in place
 * @return IMG2_OK; IMG2_E_ARG (NULL or invalid image); IMG2_E_NOMEM (image unchanged)
 */
img2_status_t img2_apply_orientation(img2_image_t **img);

/**
 * @brief Pointer to pixel (x, y) ([R, G, B, A])
 *
 * @param img Image
 * @param x   Column (0-based)
 * @param y   Row (0-based)
 * @return Pointer to the pixel, or NULL if img/pixels is NULL or (x, y) is out of bounds
 */
static inline uint8_t *img2_image_get_pixel(const img2_image_t *img, uint32_t x, uint32_t y)
{
	if (img == NULL || img->pixels == NULL || x >= img->width || y >= img->height) {
		return NULL;
	}
	return img->pixels + (size_t)y * img->stride + (size_t)x * 4;
}

/**
 * @brief Set pixel (x, y)
 *
 * @param img Image
 * @param x   Column (0-based)
 * @param y   Row (0-based)
 * @param r   Red
 * @param g   Green
 * @param b   Blue
 * @param a   Alpha (0 = transparent, 255 = opaque)
 * @return true on success, false if (x, y) is out of bounds or the image is invalid
 */
static inline bool img2_image_set_pixel(img2_image_t *img, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	uint8_t *p = img2_image_get_pixel(img, x, y);
	if (p == NULL) {
		return false;
	}
	p[0] = r;
	p[1] = g;
	p[2] = b;
	p[3] = a;
	return true;
}

IMG2_END_DECLS

#endif /* IMG2_IMAGE_H */
