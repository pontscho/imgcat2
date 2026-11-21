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

/* EXIF/XMP metadata (always compiled; freed unconditionally) */
#include "../metadata/exif_reader.h"

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
	img->stride = width * 4;
	img->delay_ms = 0;
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

	/* Free image structure */
	free(img);
}

void img2_frames_free(img2_frames_t *f)
{
	if (f == NULL) {
		return;
	}
	if (f->frames != NULL) {
		for (int i = 0; i < f->count; i++) {
			img2_image_destroy(f->frames[i]);
		}
		free(f->frames);
	}
	f->frames = NULL;
	f->count = 0;
	f->loop_count = 0;
}

/** @brief [F6] clamp a rounded fit dimension to [1, max] in double before the uint32 conversion (no float->uint32 UB) */
static uint32_t fit_round_clamp(double v, uint32_t max)
{
	if (!(v >= 1.0)) { /* also NaN */
		return 1;
	} else if (v >= (double)max) {
		return max;
	}
	return (uint32_t)v;
}

img2_status_t img2_fit_size(uint32_t src_w, uint32_t src_h, uint32_t max_w, uint32_t max_h, uint32_t *out_w, uint32_t *out_h)
{
	if (out_w == NULL || out_h == NULL) {
		return IMG2_E_ARG;
	}
	*out_w = 0;
	*out_h = 0;

	/* [SM2] zero arguments are rejected before the division (the fit wrappers log the legacy line) */
	if (src_w == 0 || src_h == 0 || max_w == 0 || max_h == 0) {
		return IMG2_E_ARG;
	}

	/* Calculate aspect ratio */
	float src_aspect = (float)src_w / (float)src_h;
	float target_aspect = (float)max_w / (float)max_h;

	/* Calculate fit dimensions (maintain aspect ratio) */
	uint32_t new_width, new_height;

	if (src_aspect > target_aspect) {
		/* Source is wider - fit to width */
		new_width = max_w;
		new_height = fit_round_clamp((double)roundf((float)new_width / src_aspect), max_h);
		if (new_height > max_h) {
			new_height = max_h;
			new_width = fit_round_clamp((double)roundf((float)new_height * src_aspect), max_w);
		}

	} else {
		/* Source is taller - fit to height */
		new_height = max_h;
		new_width = fit_round_clamp((double)roundf((float)new_height * src_aspect), max_w);
		if (new_width > max_w) {
			new_width = max_w;
			new_height = fit_round_clamp((double)roundf((float)new_width / src_aspect), max_h);
		}
	}

	/* Validate new dimensions */
	if (new_width == 0 || new_height == 0) {
		img2_log(IMG2_LOG_ERROR, "image_scale_fit: calculated dimensions are invalid %u×%u", new_width, new_height);
		return IMG2_E_ARG;
	}

	*out_w = new_width;
	*out_h = new_height;
	return IMG2_OK;
}

/* [R6] tag is "image_scale_fit" or "image_scale_resize"; all texts are the legacy ones. */
static img2_status_t scale_impl(const char *tag, const img2_image_t *src, uint32_t w, uint32_t h, img2_filter_t filter, img2_image_t **out)
{
	*out = NULL;

	if (src == NULL || src->pixels == NULL) {
		img2_log(IMG2_LOG_ERROR, "%s: invalid source image", tag);
		return IMG2_E_ARG;
	}

	if (w == 0 || h == 0) {
		img2_log(IMG2_LOG_ERROR, "%s: invalid target dimensions %u×%u", tag, w, h);
		return IMG2_E_ARG;
	}

	size_t byte_count;
	bool size_ok = img2_image_calc_size(w, h, &byte_count);

	/* Create output image */
	img2_image_t *dst = img2_image_alloc(w, h);
	if (dst == NULL) {
		img2_log(IMG2_LOG_ERROR, "%s: failed to create output image", tag);
		return size_ok ? IMG2_E_NOMEM : IMG2_E_LIMIT;
	}

	/* Resize using stb_image_resize2 (SRGB colorspace for natural results) */
	int ok;
	if (filter == IMG2_FILTER_DEFAULT) {
		ok = stbir_resize_uint8_srgb(src->pixels, src->width, src->height, (int)src->stride, dst->pixels, w, h, (int)dst->stride, STBIR_RGBA) != NULL;
	} else {
		stbir_filter f;
		switch (filter) {
			case IMG2_FILTER_POINT:    f = STBIR_FILTER_POINT_SAMPLE; break;
			case IMG2_FILTER_BOX:      f = STBIR_FILTER_BOX; break;
			case IMG2_FILTER_TRIANGLE: f = STBIR_FILTER_TRIANGLE; break;
			case IMG2_FILTER_CUBIC:    f = STBIR_FILTER_CATMULLROM; break;
			case IMG2_FILTER_MITCHELL: f = STBIR_FILTER_MITCHELL; break;
			default:
				img2_image_destroy(dst);
				return IMG2_E_ARG;
		}
		ok = stbir_resize(src->pixels, (int)src->width, (int)src->height, (int)src->stride, dst->pixels, (int)w, (int)h, (int)dst->stride, STBIR_RGBA, STBIR_TYPE_UINT8_SRGB, STBIR_EDGE_CLAMP, f) != NULL;
	}
	if (!ok) {
		img2_log(IMG2_LOG_ERROR, "%s: stbir_resize failed", tag);
		img2_image_destroy(dst);
		return IMG2_E_CODEC;
	}

	*out = dst;
	return IMG2_OK;
}

/* Legacy fit path (pre-checks + img2_fit_size + scale_impl), shared by img2_image_scale_fit and img2_scale_fit */
static img2_status_t scale_fit_impl(const img2_image_t *src, uint32_t target_width, uint32_t target_height, img2_filter_t filter, img2_image_t **out)
{
	*out = NULL;

	if (src == NULL || src->pixels == NULL) {
		img2_log(IMG2_LOG_ERROR, "image_scale_fit: invalid source image");
		return IMG2_E_ARG;

	} else if (target_width == 0 || target_height == 0) {
		img2_log(IMG2_LOG_ERROR, "image_scale_fit: invalid target dimensions %u×%u", target_width, target_height);
		return IMG2_E_ARG;
	}

	uint32_t new_width, new_height;
	img2_status_t st = img2_fit_size(src->width, src->height, target_width, target_height, &new_width, &new_height);
	if (st != IMG2_OK) {
		return st;
	}

	return scale_impl("image_scale_fit", src, new_width, new_height, filter, out);
}

img2_status_t img2_image_scale_fit(const img2_image_t *src, uint32_t target_width, uint32_t target_height, img2_image_t **out)
{
	if (out == NULL) {
		return IMG2_E_ARG;
	}
	return scale_fit_impl(src, target_width, target_height, IMG2_FILTER_DEFAULT, out);
}

img2_status_t img2_image_scale_resize(const img2_image_t *src, uint32_t target_width, uint32_t target_height, img2_image_t **out)
{
	if (out == NULL) {
		return IMG2_E_ARG;
	}
	return scale_impl("image_scale_resize", src, target_width, target_height, IMG2_FILTER_DEFAULT, out);
}

img2_status_t img2_image_validate(const img2_image_t *img)
{
	if (img == NULL || img->pixels == NULL) {
		return IMG2_E_ARG;
	}
	if (img->width == 0 || img->height == 0 || img->width > IMG2_MAX_DIMENSION || img->height > IMG2_MAX_DIMENSION) {
		return IMG2_E_ARG;
	}
	size_t byte_count;
	if (!img2_image_calc_size(img->width, img->height, &byte_count)) {
		return IMG2_E_ARG;
	}
	size_t row = (size_t)img->width * 4;
	if ((size_t)img->stride < row) {
		return IMG2_E_ARG;
	}
	if ((size_t)img->stride > SIZE_MAX / img->height) {
		return IMG2_E_ARG;
	}
	/* 1.0: decoders, encoders and stbir assume packed rows, and a padded buffer cannot be bounds-checked */
	if ((size_t)img->stride != row) {
		return IMG2_E_ARG;
	}
	return IMG2_OK;
}

img2_status_t img2_frames_validate(const img2_frames_t *f)
{
	if (f == NULL || f->count < 0 || (f->count > 0 && f->frames == NULL)) {
		return IMG2_E_ARG;
	}
	for (int i = 0; i < f->count; i++) {
		if (img2_image_validate(f->frames[i]) != IMG2_OK) {
			return IMG2_E_ARG;
		}
	}
	return IMG2_OK;
}

img2_status_t img2_image_create(uint32_t width, uint32_t height, img2_image_t **out)
{
	if (out == NULL) {
		return IMG2_E_ARG;
	}
	*out = NULL;
	if (width == 0 || height == 0) {
		return IMG2_E_ARG;
	}
	size_t byte_count;
	if (!img2_image_calc_size(width, height, &byte_count)) {
		return IMG2_E_LIMIT; /* checked here, so img2_image_alloc's ERROR line is not emitted for parameter errors */
	}
	*out = img2_image_alloc(width, height);
	return *out != NULL ? IMG2_OK : IMG2_E_NOMEM;
}

/* [SM2] public prologue of img2_scale / img2_scale_fit; the legacy ERROR line only for NULL source/pixels */
static img2_status_t scale_prologue(const char *tag, const img2_image_t *src, img2_filter_t filter)
{
	if (src == NULL || src->pixels == NULL) {
		img2_log(IMG2_LOG_ERROR, "%s: invalid source image", tag);
		return IMG2_E_ARG;
	}
	if (img2_image_validate(src) != IMG2_OK) {
		img2_log(IMG2_LOG_DEBUG, "%s: source image rejected (dimensions or stride)", tag);
		return IMG2_E_ARG;
	}
	if ((unsigned)filter > (unsigned)IMG2_FILTER_MITCHELL) {
		img2_log(IMG2_LOG_DEBUG, "%s: invalid filter %d", tag, (int)filter);
		return IMG2_E_ARG;
	}
	return IMG2_OK;
}

img2_status_t img2_scale(const img2_image_t *src, uint32_t width, uint32_t height, img2_filter_t filter, img2_image_t **out)
{
	if (out == NULL) {
		return IMG2_E_ARG;
	}
	*out = NULL;
	img2_status_t st = scale_prologue("image_scale_resize", src, filter);
	if (st != IMG2_OK) {
		return st;
	}
	return scale_impl("image_scale_resize", src, width, height, filter, out);
}

img2_status_t img2_scale_fit(const img2_image_t *src, uint32_t max_w, uint32_t max_h, img2_filter_t filter, img2_image_t **out)
{
	if (out == NULL) {
		return IMG2_E_ARG;
	}
	*out = NULL;
	img2_status_t st = scale_prologue("image_scale_fit", src, filter);
	if (st != IMG2_OK) {
		return st;
	}
	return scale_fit_impl(src, max_w, max_h, filter, out);
}

img2_status_t img2_convert_rgb_to_rgba(const uint8_t *rgb, uint32_t width, uint32_t height, img2_image_t **out)
{
	if (out == NULL) {
		return IMG2_E_ARG;
	}
	*out = NULL;

	if (rgb == NULL) {
		img2_log(IMG2_LOG_ERROR, "convert_rgb_to_rgba: invalid RGB data");
		return IMG2_E_ARG;
	}

	/* Create RGBA image */
	size_t byte_count;
	bool size_ok = img2_image_calc_size(width, height, &byte_count);
	img2_image_t *img = img2_image_alloc(width, height);
	if (img == NULL) {
		img2_log(IMG2_LOG_ERROR, "convert_rgb_to_rgba: failed to create image");
		return size_ok ? IMG2_E_NOMEM : IMG2_E_LIMIT;
	}

	/* Convert RGB to RGBA (add alpha=255) */
	size_t pixel_count = (size_t)width * (size_t)height;
	for (size_t i = 0; i < pixel_count; i++) {
		img->pixels[i * 4 + 0] = rgb[i * 3 + 0]; /* R */
		img->pixels[i * 4 + 1] = rgb[i * 3 + 1]; /* G */
		img->pixels[i * 4 + 2] = rgb[i * 3 + 2]; /* B */
		img->pixels[i * 4 + 3] = 255; /* A (opaque) */
	}

	*out = img;
	return IMG2_OK;
}

img2_status_t img2_convert_gray_to_rgba(const uint8_t *gray, uint32_t width, uint32_t height, img2_image_t **out)
{
	if (out == NULL) {
		return IMG2_E_ARG;
	}
	*out = NULL;

	if (gray == NULL) {
		img2_log(IMG2_LOG_ERROR, "convert_grayscale_to_rgba: invalid grayscale data");
		return IMG2_E_ARG;
	}

	/* Create RGBA image */
	size_t byte_count;
	bool size_ok = img2_image_calc_size(width, height, &byte_count);
	img2_image_t *img = img2_image_alloc(width, height);
	if (img == NULL) {
		img2_log(IMG2_LOG_ERROR, "convert_grayscale_to_rgba: failed to create image");
		return size_ok ? IMG2_E_NOMEM : IMG2_E_LIMIT;
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

	*out = img;
	return IMG2_OK;
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

img2_status_t img2_image_orient(img2_image_t *img, uint16_t orientation)
{
	/* Validate input parameters */
	if (img == NULL) {
		img2_log(IMG2_LOG_ERROR, "image_apply_orientation: NULL image pointer");
		return IMG2_E_ARG;
	}

	if (img->pixels == NULL) {
		img2_log(IMG2_LOG_ERROR, "image_apply_orientation: NULL pixel buffer");
		return IMG2_E_ARG;
	}

	/* Validate orientation range */
	if (orientation < 1 || orientation > 8) {
		img2_log(IMG2_LOG_ERROR, "image_apply_orientation: invalid orientation %u (must be 1-8)", orientation);
		return IMG2_E_ARG;
	}

	/* Orientation 1 = normal, no transformation needed */
	if (orientation == 1) {
		return IMG2_OK;
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
					return IMG2_E_NOMEM;
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
					return IMG2_E_NOMEM;
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

		default: img2_log(IMG2_LOG_ERROR, "image_apply_orientation: unhandled orientation %u", orientation); return IMG2_E_ARG;
	}

	/* Check if transformation succeeded */
	if (new_pixels == NULL) {
		img2_log(IMG2_LOG_ERROR, "image_apply_orientation: transformation failed for orientation %u", orientation);
		return IMG2_E_NOMEM;
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
		img->stride = new_width * 4;
	}

	return IMG2_OK;
}
