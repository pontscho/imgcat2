/**
 * @file image.h
 * @brief Image data structure and memory management API
 *
 * Provides a unified RGBA8888 image representation in memory with safe
 * allocation, deallocation, and pixel access operations.
 */

#ifndef IMG2_INTERNAL_CORE_IMAGE_H
#define IMG2_INTERNAL_CORE_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "status.h"

/**
 * @defgroup ResourceLimits Image Resource Limits
 * @{
 */

/** Maximum width or height per axis (65536 pixels) */
#define IMG2_MAX_DIMENSION 65536

/** Maximum total pixels (200 megapixels) */
#define IMG2_MAX_PIXELS 200000000UL

/** Maximum file size for input (1GiB; uncompressed 200MP RGBA fits) */
#define IMG2_MAX_FILE_SIZE 1073741824UL

/** @} */

/* Forward declarations for EXIF/XMP metadata structures (defined in exif_reader.h) */
typedef struct img2_exif img2_exif_t;
typedef struct img2_xmp img2_xmp_t;

/**
 * @struct img2_image_t
 * @brief RGBA8888 image representation
 *
 * Memory layout: row-major, top-to-bottom
 * Pixel format: R, G, B, A (4 bytes per pixel)
 * Offset calculation: pixels[(y * width + x) * 4 + channel]
 */
typedef struct img2_image {
	uint32_t width, height;
	uint32_t stride;          /* bytes per row; == width * 4 today */
	uint8_t *pixels;          /* RGBA8888 */
	uint32_t delay_ms;        /* frame duration, 0 = unknown/not animated (filled in S11) */
	img2_exif_t *exif;
	img2_xmp_t *xmp;
} img2_image_t;

/**
 * @brief Decoded frame set (count >= 1 on success)
 */
typedef struct img2_frames { img2_image_t **frames; int count; int loop_count; } img2_frames_t;

/**
 * @brief Destroy all frames, free the array and zero *f (NULL-safe)
 */
void img2_frames_free(img2_frames_t *f);

/* [P3] S7 moves this enum verbatim into <img2/image.h> */
typedef enum img2_filter {
	IMG2_FILTER_DEFAULT = 0,  /* stb_image_resize2 sRGB default (imgcat2 "lanczos") */
	IMG2_FILTER_POINT, IMG2_FILTER_BOX, IMG2_FILTER_TRIANGLE, IMG2_FILTER_CUBIC, IMG2_FILTER_MITCHELL
} img2_filter_t;

/**
 * @brief Aspect-preserving fit of src_w x src_h into max_w x max_h
 *
 * @return IMG2_OK with 1 <= *out_w <= max_w and 1 <= *out_h <= max_h, or IMG2_E_ARG
 *         (zero argument, NULL output, or a degenerate result)
 */
img2_status_t img2_fit_size(uint32_t src_w, uint32_t src_h, uint32_t max_w, uint32_t max_h,
                            uint32_t *out_w, uint32_t *out_h);

/**
 * @brief Create a new image with specified dimensions
 *
 * Allocates an img2_image_t structure and pixel buffer with overflow checks.
 * The pixel buffer is initialized to zero (transparent black).
 *
 * @param width Image width in pixels (must be > 0 and <= IMG2_MAX_DIMENSION)
 * @param height Image height in pixels (must be > 0 and <= IMG2_MAX_DIMENSION)
 * @return Pointer to allocated img2_image_t, or NULL on failure
 *
 * @note Caller must free with img2_image_destroy()
 * @note Returns NULL if dimensions exceed limits or allocation fails
 */
img2_image_t *img2_image_alloc(uint32_t width, uint32_t height);

/**
 * @brief Destroy an image and free all resources
 *
 * Frees the pixel buffer, EXIF/XMP metadata and image structure. NULL-safe.
 *
 * @param img Image to destroy (can be NULL)
 */
void img2_image_destroy(img2_image_t *img);

/**
 * @brief Get pointer to pixel at specified coordinates
 *
 * Returns a pointer to the first byte (R channel) of the pixel at (x, y).
 * The pixel data is laid out as [R, G, B, A].
 *
 * @param img Image to access
 * @param x X coordinate (0-based)
 * @param y Y coordinate (0-based)
 * @return Pointer to pixel [R, G, B, A], or NULL if out of bounds
 *
 * @note Inline function for performance
 */
static inline uint8_t *img2_image_get_pixel(const img2_image_t *img, uint32_t x, uint32_t y)
{
	if (img == NULL || x >= img->width || y >= img->height) {
		return NULL;
	}
	return &img->pixels[(y * img->width + x) * 4];
}

/**
 * @brief Set pixel color at specified coordinates
 *
 * Sets the RGBA values of the pixel at (x, y).
 *
 * @param img Image to modify
 * @param x X coordinate (0-based)
 * @param y Y coordinate (0-based)
 * @param r Red channel (0-255)
 * @param g Green channel (0-255)
 * @param b Blue channel (0-255)
 * @param a Alpha channel (0-255, 0=transparent, 255=opaque)
 * @return true if successful, false if coordinates out of bounds
 */
static inline bool img2_image_set_pixel(img2_image_t *img, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	uint8_t *pixel = img2_image_get_pixel(img, x, y);
	if (pixel == NULL) {
		return false;
	}
	pixel[0] = r;
	pixel[1] = g;
	pixel[2] = b;
	pixel[3] = a;
	return true;
}

/**
 * @brief Calculate total byte size for image pixel data
 *
 * Computes width × height × 4 with overflow checks.
 *
 * @param width Image width
 * @param height Image height
 * @param out_size Output parameter for computed size
 * @return true if calculation succeeded, false on overflow
 */
bool img2_image_calc_size(uint32_t width, uint32_t height, size_t *out_size);

/**
 * @brief Scale image to fit within target dimensions (maintain aspect ratio)
 *
 * Scales the image to fit within the specified dimensions while maintaining
 * the original aspect ratio. The resulting image will be no larger than
 * target_width × target_height, but may be smaller on one axis.
 *
 * @param src Source image to scale
 * @param target_width Maximum width of scaled image
 * @param target_height Maximum height of scaled image
 * @param out Output: scaled image (NULL on error)
 * @return IMG2_OK, IMG2_E_ARG, IMG2_E_LIMIT, IMG2_E_NOMEM or IMG2_E_CODEC
 *
 * @note Caller must free with img2_image_destroy()
 * @note Uses high-quality resampling (Mitchell filter for downsampling)
 */
img2_status_t img2_image_scale_fit(const img2_image_t *src, uint32_t target_width, uint32_t target_height, img2_image_t **out);

/**
 * @brief Scale image to exact dimensions (may distort aspect ratio)
 *
 * Scales the image to exactly the specified dimensions, stretching or
 * squashing the image if necessary. Aspect ratio is not preserved.
 *
 * @param src Source image to scale
 * @param target_width Exact width of scaled image
 * @param target_height Exact height of scaled image
 * @param out Output: scaled image (NULL on error)
 * @return IMG2_OK, IMG2_E_ARG, IMG2_E_LIMIT, IMG2_E_NOMEM or IMG2_E_CODEC
 *
 * @note Caller must free with img2_image_destroy()
 * @note Uses high-quality resampling
 */
img2_status_t img2_image_scale_resize(const img2_image_t *src, uint32_t target_width, uint32_t target_height, img2_image_t **out);

/**
 * @brief Apply EXIF orientation transformation to image
 *
 * Transforms the image according to EXIF orientation tag (values 1-8).
 * The image is modified in-place, with pixel buffer replaced and dimensions
 * updated as needed. Orientation values correspond to:
 *   1: Normal (no transformation)
 *   2: Flip horizontal
 *   3: Rotate 180°
 *   4: Flip vertical
 *   5: Transpose (flip horizontal + rotate 270° CW)
 *   6: Rotate 90° clockwise
 *   7: Transverse (flip horizontal + rotate 90° CW)
 *   8: Rotate 270° clockwise
 *
 * @param img Image to transform (modified in-place)
 * @param orientation EXIF orientation value (1-8)
 * @return IMG2_OK, or IMG2_E_ARG / IMG2_E_NOMEM (all < 0)
 *
 * @note Orientation value 1 returns immediately without transformation
 * @note Dimensions are swapped for 90°/270° rotations (orientations 5,6,7,8)
 * @note Invalid orientation values (not 1-8) return -1
 * @note NULL img or NULL img->pixels return -1
 */
img2_status_t img2_image_orient(img2_image_t *img, uint16_t orientation);

/**
 * @brief Convert RGB pixel data to RGBA
 *
 * Converts RGB (3 bytes per pixel) to RGBA8888 (4 bytes per pixel) by
 * adding an opaque alpha channel (255).
 *
 * @param rgb Source RGB pixel data (width × height × 3 bytes)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param out Output: new RGBA image (NULL on error)
 * @return IMG2_OK, IMG2_E_ARG, IMG2_E_LIMIT or IMG2_E_NOMEM
 *
 * @note Caller must free with img2_image_destroy()
 * @note Alpha channel is set to 255 (opaque) for all pixels
 */
img2_status_t img2_convert_rgb_to_rgba(const uint8_t *rgb, uint32_t width, uint32_t height, img2_image_t **out);

/**
 * @brief Convert grayscale pixel data to RGBA
 *
 * Converts grayscale (1 byte per pixel) to RGBA8888 (4 bytes per pixel) by
 * replicating the gray value to R, G, B and setting alpha to 255.
 *
 * @param gray Source grayscale pixel data (width × height bytes)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param out Output: new RGBA image (NULL on error)
 * @return IMG2_OK, IMG2_E_ARG, IMG2_E_LIMIT or IMG2_E_NOMEM
 *
 * @note Caller must free with img2_image_destroy()
 * @note Alpha channel is set to 255 (opaque) for all pixels
 */
img2_status_t img2_convert_gray_to_rgba(const uint8_t *gray, uint32_t width, uint32_t height, img2_image_t **out);

#endif /* IMG2_INTERNAL_CORE_IMAGE_H */
