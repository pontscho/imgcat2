/**
 * @file image.h
 * @brief Image data structure and memory management API
 *
 * Provides a unified RGBA8888 image representation in memory with safe
 * allocation, deallocation, and pixel access operations.
 */

#ifndef IMGCAT2_IMAGE_H
#define IMGCAT2_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @defgroup ResourceLimits Image Resource Limits
 * @{
 */

/** Maximum width or height per axis (16384 pixels) */
#define IMAGE_MAX_DIMENSION 16384

/** Maximum total pixels (100 megapixels) */
#define IMAGE_MAX_PIXELS 100000000UL

/** Maximum file size for input (50MB) */
#define IMAGE_MAX_FILE_SIZE 52428800UL

/** @} */

/**
 * @struct image_t
 * @brief RGBA8888 image representation
 *
 * Memory layout: row-major, top-to-bottom
 * Pixel format: R, G, B, A (4 bytes per pixel)
 * Offset calculation: pixels[(y * width + x) * 4 + channel]
 */
typedef struct {
	uint32_t width; /**< Image width in pixels */
	uint32_t height; /**< Image height in pixels */
	uint8_t *pixels; /**< RGBA8888 pixel data: width × height × 4 bytes */
} image_t;

/**
 * @brief Create a new image with specified dimensions
 *
 * Allocates an image_t structure and pixel buffer with overflow checks.
 * The pixel buffer is initialized to zero (transparent black).
 *
 * @param width Image width in pixels (must be > 0 and <= IMAGE_MAX_DIMENSION)
 * @param height Image height in pixels (must be > 0 and <= IMAGE_MAX_DIMENSION)
 * @return Pointer to allocated image_t, or NULL on failure
 *
 * @note Caller must free with image_destroy()
 * @note Returns NULL if dimensions exceed limits or allocation fails
 */
image_t *image_create(uint32_t width, uint32_t height);

/**
 * @brief Destroy an image and free all resources
 *
 * Frees the pixel buffer and image structure. NULL-safe.
 *
 * @param img Image to destroy (can be NULL)
 */
void image_destroy(image_t *img);

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
static inline uint8_t *image_get_pixel(const image_t *img, uint32_t x, uint32_t y)
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
static inline bool image_set_pixel(image_t *img, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	uint8_t *pixel = image_get_pixel(img, x, y);
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
bool image_calculate_size(uint32_t width, uint32_t height, size_t *out_size);

#endif /* IMGCAT2_IMAGE_H */
