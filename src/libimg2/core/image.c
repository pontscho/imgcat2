/**
 * @file image.c
 * @brief Image data structure implementation
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* STB image resize implementation */
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_STATIC /* TU-local: no stbir_* globals leak from libimg2 */
#define STBIR_ASSERT(x) ((void)0)
#include "image.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stb_image_resize2.h"
#pragma GCC diagnostic pop
#include "log.h"

/* EXIF/XMP metadata support */
#ifdef IMG2_HAVE_EXIF_READER
#include "../metadata/exif_reader.h"
#endif

bool img2_image_calc_size(uint32_t width, uint32_t height, size_t *out_size)
{
	size_t pixel_count = (size_t)width * (size_t)height;
	size_t byte_count = pixel_count * 4;

	if (out_size == NULL) {
		return false;
	} else if (width == 0 || height == 0) {
		return false;
	} else if (width > IMG2_MAX_DIMENSION || height > IMG2_MAX_DIMENSION) {
		return false;
	} else if (pixel_count > IMG2_MAX_PIXELS) {
		return false;
	} else if (byte_count / 4 != pixel_count) {
		return false;
	}

	*out_size = byte_count;
	return true;
}

img2_image_t *img2_image_alloc(uint32_t width, uint32_t height)
{
	/* Validate dimensions and calculate size */
	size_t byte_count;
	if (!img2_image_calc_size(width, height, &byte_count)) {
		img2_log(IMG2_LOG_ERROR, "image_create: invalid dimensions %u×%u", width, height);
		return NULL;
	}

	/* Allocate image structure */
	img2_image_t *img = malloc(sizeof(img2_image_t));
	if (img == NULL) {
		img2_log(IMG2_LOG_ERROR, "image_create: failed to allocate image_t");
		return NULL;
	}

	/* Allocate pixel buffer (initialized to zero - transparent black) */
	img->pixels = calloc(byte_count, 1);
	if (img->pixels == NULL) {
		img2_log(IMG2_LOG_ERROR, "image_create: failed to allocate %zu bytes for pixels", byte_count);
		free(img);
		return NULL;
	}

	/* Initialize fields */
	img->width = width;
	img->height = height;
	img->exif = NULL;
	img->xmp = NULL;

	return img;
}

void img2_image_destroy(img2_image_t *img)
{
	if (img == NULL) {
		return;
	}

	/* Free pixel buffer */
	if (img->pixels != NULL) {
		free(img->pixels);
		img->pixels = NULL;
	}

#ifdef IMG2_HAVE_EXIF_READER
	/* Free EXIF metadata */
	if (img->exif != NULL) {
		img2_exif_clear(img->exif);
		free(img->exif);
		img->exif = NULL;
	}

	/* Free XMP metadata */
	if (img->xmp != NULL) {
		img2_xmp_clear(img->xmp);
		free(img->xmp);
		img->xmp = NULL;
	}
#endif

	/* Free image structure */
	free(img);
}

img2_image_t *img2_image_scale_fit(const img2_image_t *src, uint32_t target_width, uint32_t target_height)
{
	if (src == NULL || src->pixels == NULL) {
		img2_log(IMG2_LOG_ERROR, "image_scale_fit: invalid source image");
		return NULL;

	} else if (target_width == 0 || target_height == 0) {
		img2_log(IMG2_LOG_ERROR, "image_scale_fit: invalid target dimensions %u×%u", target_width, target_height);
		return NULL;
	}

	/* Calculate aspect ratio */
	float src_aspect = (float)src->width / (float)src->height;
	float target_aspect = (float)target_width / (float)target_height;

	/* Calculate fit dimensions (maintain aspect ratio) */
	uint32_t new_width, new_height;

	if (src_aspect > target_aspect) {
		/* Source is wider - fit to width */
		new_width = target_width;
		new_height = (uint32_t)roundf((float)new_width / src_aspect);
		if (new_height > target_height) {
			new_height = target_height;
			new_width = (uint32_t)roundf((float)new_height * src_aspect);
		}

	} else {
		/* Source is taller - fit to height */
		new_height = target_height;
		new_width = (uint32_t)roundf((float)new_height * src_aspect);
		if (new_width > target_width) {
			new_width = target_width;
			new_height = (uint32_t)roundf((float)new_width / src_aspect);
		}
	}

	/* Validate new dimensions */
	if (new_width == 0 || new_height == 0) {
		img2_log(IMG2_LOG_ERROR, "image_scale_fit: calculated dimensions are invalid %u×%u", new_width, new_height);
		return NULL;
	}

	/* Create output image */
	img2_image_t *dst = img2_image_alloc(new_width, new_height);
	if (dst == NULL) {
		img2_log(IMG2_LOG_ERROR, "image_scale_fit: failed to create output image");
		return NULL;
	}

	/* Resize using stb_image_resize2 (SRGB colorspace for natural results) */
	if (!stbir_resize_uint8_srgb(src->pixels, src->width, src->height, 0, dst->pixels, new_width, new_height, 0, STBIR_RGBA)) {
		img2_log(IMG2_LOG_ERROR, "image_scale_fit: stbir_resize failed");
		img2_image_destroy(dst);
		return NULL;
	}

	return dst;
}

img2_image_t *img2_image_scale_resize(const img2_image_t *src, uint32_t target_width, uint32_t target_height)
{
	if (src == NULL || src->pixels == NULL) {
		img2_log(IMG2_LOG_ERROR, "image_scale_resize: invalid source image");
		return NULL;
	}

	if (target_width == 0 || target_height == 0) {
		img2_log(IMG2_LOG_ERROR, "image_scale_resize: invalid target dimensions %u×%u", target_width, target_height);
		return NULL;
	}

	/* Create output image with exact dimensions (no aspect ratio preservation) */
	img2_image_t *dst = img2_image_alloc(target_width, target_height);
	if (dst == NULL) {
		img2_log(IMG2_LOG_ERROR, "image_scale_resize: failed to create output image");
		return NULL;
	}

	/* Resize using stb_image_resize2 (SRGB colorspace) */
	if (!stbir_resize_uint8_srgb(src->pixels, src->width, src->height, 0, dst->pixels, target_width, target_height, 0, STBIR_RGBA)) {
		img2_log(IMG2_LOG_ERROR, "image_scale_resize: stbir_resize failed");
		img2_image_destroy(dst);
		return NULL;
	}

	return dst;
}

img2_image_t *img2_convert_rgb_to_rgba(const uint8_t *rgb, uint32_t width, uint32_t height)
{
	if (rgb == NULL) {
		img2_log(IMG2_LOG_ERROR, "convert_rgb_to_rgba: invalid RGB data");
		return NULL;
	}

	/* Create RGBA image */
	img2_image_t *img = img2_image_alloc(width, height);
	if (img == NULL) {
		img2_log(IMG2_LOG_ERROR, "convert_rgb_to_rgba: failed to create image");
		return NULL;
	}

	/* Convert RGB to RGBA (add alpha=255) */
	size_t pixel_count = (size_t)width * (size_t)height;
	for (size_t i = 0; i < pixel_count; i++) {
		img->pixels[i * 4 + 0] = rgb[i * 3 + 0]; /* R */
		img->pixels[i * 4 + 1] = rgb[i * 3 + 1]; /* G */
		img->pixels[i * 4 + 2] = rgb[i * 3 + 2]; /* B */
		img->pixels[i * 4 + 3] = 255; /* A (opaque) */
	}

	return img;
}

img2_image_t *img2_convert_gray_to_rgba(const uint8_t *gray, uint32_t width, uint32_t height)
{
	if (gray == NULL) {
		img2_log(IMG2_LOG_ERROR, "convert_grayscale_to_rgba: invalid grayscale data");
		return NULL;
	}

	/* Create RGBA image */
	img2_image_t *img = img2_image_alloc(width, height);
	if (img == NULL) {
		img2_log(IMG2_LOG_ERROR, "convert_grayscale_to_rgba: failed to create image");
		return NULL;
	}

	/* Convert grayscale to RGBA (replicate gray to R,G,B; alpha=255) */
	size_t pixel_count = (size_t)width * (size_t)height;
	for (size_t i = 0; i < pixel_count; i++) {
		uint8_t gray_value = gray[i];
		img->pixels[i * 4 + 0] = gray_value; /* R */
		img->pixels[i * 4 + 1] = gray_value; /* G */
		img->pixels[i * 4 + 2] = gray_value; /* B */
		img->pixels[i * 4 + 3] = 255; /* A (opaque) */
	}

	return img;
}

/**
 * @brief Transform image by flipping horizontally
 *
 * Creates a new buffer with pixels flipped horizontally (mirror image).
 * For each row, pixels are reversed: dest[y][x] = src[y][width-1-x]
 *
 * @param src Source pixel data (RGBA8888 format)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @return New pixel buffer, or NULL on allocation failure
 *
 * @note Caller must free returned buffer with free()
 */
static uint8_t *transform_flip_horizontal(const uint8_t *src, uint32_t width, uint32_t height)
{
	if (src == NULL) {
		img2_log(IMG2_LOG_ERROR, "transform_flip_horizontal: invalid source data");
		return NULL;
	}

	/* Allocate new buffer for transformed pixels */
	size_t buffer_size = (size_t)width * (size_t)height * 4;
	uint8_t *dest = malloc(buffer_size);
	if (dest == NULL) {
		img2_log(IMG2_LOG_ERROR, "transform_flip_horizontal: failed to allocate buffer");
		return NULL;
	}

	/* Flip horizontally: reverse pixel order in each row */
	for (uint32_t y = 0; y < height; y++) {
		for (uint32_t x = 0; x < width; x++) {
			size_t src_offset = ((y * width) + (width - 1 - x)) * 4;
			size_t dest_offset = ((y * width) + x) * 4;
			memcpy(&dest[dest_offset], &src[src_offset], 4);
		}
	}

	return dest;
}

/**
 * @brief Transform image by flipping vertically
 *
 * Creates a new buffer with pixels flipped vertically (upside down).
 * Rows are reversed: dest[y][x] = src[height-1-y][x]
 *
 * @param src Source pixel data (RGBA8888 format)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @return New pixel buffer, or NULL on allocation failure
 *
 * @note Caller must free returned buffer with free()
 */
static uint8_t *transform_flip_vertical(const uint8_t *src, uint32_t width, uint32_t height)
{
	if (src == NULL) {
		img2_log(IMG2_LOG_ERROR, "transform_flip_vertical: invalid source data");
		return NULL;
	}

	/* Allocate new buffer for transformed pixels */
	size_t buffer_size = (size_t)width * (size_t)height * 4;
	uint8_t *dest = malloc(buffer_size);
	if (dest == NULL) {
		img2_log(IMG2_LOG_ERROR, "transform_flip_vertical: failed to allocate buffer");
		return NULL;
	}

	/* Flip vertically: reverse row order */
	for (uint32_t y = 0; y < height; y++) {
		for (uint32_t x = 0; x < width; x++) {
			size_t src_offset = (((height - 1 - y) * width) + x) * 4;
			size_t dest_offset = ((y * width) + x) * 4;
			memcpy(&dest[dest_offset], &src[src_offset], 4);
		}
	}

	return dest;
}

/**
 * @brief Transform image by rotating 90 degrees clockwise
 *
 * Creates a new buffer with image rotated 90° CW (transpose + flip horizontal).
 * Algorithm: dest[x][height-1-y] = src[y][x]
 * IMPORTANT: Output dimensions are swapped (width×height → height×width)
 *
 * @param src Source pixel data (RGBA8888 format)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @return New pixel buffer, or NULL on allocation failure
 *
 * @note Caller must free returned buffer with free()
 * @note Output buffer size is height × width × 4 (dimensions swapped)
 */
static uint8_t *transform_rotate_90cw(const uint8_t *src, uint32_t width, uint32_t height)
{
	if (src == NULL) {
		img2_log(IMG2_LOG_ERROR, "transform_rotate_90cw: invalid source data");
		return NULL;
	}

	/* Allocate new buffer with swapped dimensions */
	size_t buffer_size = (size_t)height * (size_t)width * 4;
	uint8_t *dest = malloc(buffer_size);
	if (dest == NULL) {
		img2_log(IMG2_LOG_ERROR, "transform_rotate_90cw: failed to allocate buffer");
		return NULL;
	}

	/* Rotate 90° clockwise: dest[x][height-1-y] = src[y][x] */
	for (uint32_t y = 0; y < height; y++) {
		for (uint32_t x = 0; x < width; x++) {
			size_t src_offset = ((y * width) + x) * 4;
			size_t dest_offset = ((x * height) + (height - 1 - y)) * 4;
			memcpy(&dest[dest_offset], &src[src_offset], 4);
		}
	}

	return dest;
}

/**
 * @brief Transform image by rotating 180 degrees
 *
 * Creates a new buffer with image rotated 180° (flip both horizontally and vertically).
 * Algorithm: dest[y][x] = src[height-1-y][width-1-x]
 *
 * @param src Source pixel data (RGBA8888 format)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @return New pixel buffer, or NULL on allocation failure
 *
 * @note Caller must free returned buffer with free()
 */
static uint8_t *transform_rotate_180(const uint8_t *src, uint32_t width, uint32_t height)
{
	if (src == NULL) {
		img2_log(IMG2_LOG_ERROR, "transform_rotate_180: invalid source data");
		return NULL;
	}

	/* Allocate new buffer for transformed pixels */
	size_t buffer_size = (size_t)width * (size_t)height * 4;
	uint8_t *dest = malloc(buffer_size);
	if (dest == NULL) {
		img2_log(IMG2_LOG_ERROR, "transform_rotate_180: failed to allocate buffer");
		return NULL;
	}

	/* Rotate 180°: reverse both row and column */
	for (uint32_t y = 0; y < height; y++) {
		for (uint32_t x = 0; x < width; x++) {
			size_t src_offset = (((height - 1 - y) * width) + (width - 1 - x)) * 4;
			size_t dest_offset = ((y * width) + x) * 4;
			memcpy(&dest[dest_offset], &src[src_offset], 4);
		}
	}

	return dest;
}

/**
 * @brief Transform image by rotating 270 degrees clockwise
 *
 * Creates a new buffer with image rotated 270° CW (transpose + flip vertical).
 * Algorithm: dest[width-1-x][y] = src[y][x]
 * IMPORTANT: Output dimensions are swapped (width×height → height×width)
 *
 * @param src Source pixel data (RGBA8888 format)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @return New pixel buffer, or NULL on allocation failure
 *
 * @note Caller must free returned buffer with free()
 * @note Output buffer size is height × width × 4 (dimensions swapped)
 */
static uint8_t *transform_rotate_270cw(const uint8_t *src, uint32_t width, uint32_t height)
{
	if (src == NULL) {
		img2_log(IMG2_LOG_ERROR, "transform_rotate_270cw: invalid source data");
		return NULL;
	}

	/* Allocate new buffer with swapped dimensions */
	size_t buffer_size = (size_t)height * (size_t)width * 4;
	uint8_t *dest = malloc(buffer_size);
	if (dest == NULL) {
		img2_log(IMG2_LOG_ERROR, "transform_rotate_270cw: failed to allocate buffer");
		return NULL;
	}

	/* Rotate 270° clockwise: dest[width-1-x][y] = src[y][x] */
	for (uint32_t y = 0; y < height; y++) {
		for (uint32_t x = 0; x < width; x++) {
			size_t src_offset = ((y * width) + x) * 4;
			size_t dest_offset = (((width - 1 - x) * height) + y) * 4;
			memcpy(&dest[dest_offset], &src[src_offset], 4);
		}
	}

	return dest;
}

int img2_image_orient(img2_image_t *img, uint16_t orientation)
{
	/* Validate input parameters */
	if (img == NULL) {
		img2_log(IMG2_LOG_ERROR, "image_apply_orientation: NULL image pointer");
		return -1;
	}

	if (img->pixels == NULL) {
		img2_log(IMG2_LOG_ERROR, "image_apply_orientation: NULL pixel buffer");
		return -1;
	}

	/* Validate orientation range */
	if (orientation < 1 || orientation > 8) {
		img2_log(IMG2_LOG_ERROR, "image_apply_orientation: invalid orientation %u (must be 1-8)", orientation);
		return -1;
	}

	/* Orientation 1 = normal, no transformation needed */
	if (orientation == 1) {
		return 0;
	}

	/* Apply transformation based on orientation value */
	uint8_t *new_pixels = NULL;
	uint32_t new_width = img->width;
	uint32_t new_height = img->height;
	bool dimensions_swapped = false;

	switch (orientation) {
		case 2:
			/* Flip horizontal */
			new_pixels = transform_flip_horizontal(img->pixels, img->width, img->height);
			break;

		case 3:
			/* Rotate 180° */
			new_pixels = transform_rotate_180(img->pixels, img->width, img->height);
			break;

		case 4:
			/* Flip vertical */
			new_pixels = transform_flip_vertical(img->pixels, img->width, img->height);
			break;

		case 5:
			{
				/* Transpose = flip horizontal + rotate 270° CW */
				uint8_t *temp = transform_flip_horizontal(img->pixels, img->width, img->height);
				if (temp == NULL) {
					img2_log(IMG2_LOG_ERROR, "image_apply_orientation: failed to flip horizontal for orientation 5");
					return -1;
				}
				new_pixels = transform_rotate_270cw(temp, img->width, img->height);
				free(temp);
				dimensions_swapped = true;
				break;
			}

		case 6:
			/* Rotate 90° CW */
			new_pixels = transform_rotate_90cw(img->pixels, img->width, img->height);
			dimensions_swapped = true;
			break;

		case 7:
			{
				/* Transverse = flip horizontal + rotate 90° CW */
				uint8_t *temp = transform_flip_horizontal(img->pixels, img->width, img->height);
				if (temp == NULL) {
					img2_log(IMG2_LOG_ERROR, "image_apply_orientation: failed to flip horizontal for orientation 7");
					return -1;
				}
				new_pixels = transform_rotate_90cw(temp, img->width, img->height);
				free(temp);
				dimensions_swapped = true;
				break;
			}

		case 8:
			/* Rotate 270° CW */
			new_pixels = transform_rotate_270cw(img->pixels, img->width, img->height);
			dimensions_swapped = true;
			break;

		default: img2_log(IMG2_LOG_ERROR, "image_apply_orientation: unhandled orientation %u", orientation); return -1;
	}

	/* Check if transformation succeeded */
	if (new_pixels == NULL) {
		img2_log(IMG2_LOG_ERROR, "image_apply_orientation: transformation failed for orientation %u", orientation);
		return -1;
	}

	/* Replace old pixel buffer with new one */
	free(img->pixels);
	img->pixels = new_pixels;

	/* Update dimensions if swapped */
	if (dimensions_swapped) {
		new_width = img->height;
		new_height = img->width;
		img->width = new_width;
		img->height = new_height;
	}

	return 0;
}
