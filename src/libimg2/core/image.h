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

#include <img2/image.h>

#include "status.h"

/* Public: limits, img2_image_t, img2_frames_t, img2_filter_t, img2_image_destroy, img2_frames_free,
 * img2_fit_size, img2_scale*, img2_apply_orientation, img2_image_create, pixel accessors (<img2/image.h>).
 * Internal below. */

/**
 * @brief Internal [SM2]: validate a (possibly caller-built) image
 *
 * @return IMG2_OK, or IMG2_E_ARG unless img and pixels are non-NULL, 0 < width/height <= IMG2_MAX_DIMENSION,
 *         width * height <= IMG2_MAX_PIXELS, stride >= width * 4 without overflow of stride * height,
 *         and (1.0) stride == width * 4. Silent.
 */
img2_status_t img2_image_validate(const img2_image_t *img);

/**
 * @brief Internal [SM2]: validate a frame set (f non-NULL, count >= 0, frames non-NULL when count > 0,
 *        every frame passes img2_image_validate). Silent.
 */
img2_status_t img2_frames_validate(const img2_frames_t *f);

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
