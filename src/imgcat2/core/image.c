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
#include "image.h"
#include "stb_image_resize2.h"

bool image_calculate_size(uint32_t width, uint32_t height, size_t *out_size)
{
	if (out_size == NULL) {
		return false;
	}

	/* Check individual dimension limits */
	if (width == 0 || height == 0) {
		return false;
	}
	if (width > IMAGE_MAX_DIMENSION || height > IMAGE_MAX_DIMENSION) {
		return false;
	}

	/* Check total pixel count with overflow protection */
	size_t pixel_count = (size_t)width * (size_t)height;
	if (pixel_count > IMAGE_MAX_PIXELS) {
		return false;
	}

	/* Check byte count with overflow protection */
	size_t byte_count = pixel_count * 4;
	if (byte_count / 4 != pixel_count) {
		/* Overflow occurred */
		return false;
	}

	*out_size = byte_count;
	return true;
}

image_t *image_create(uint32_t width, uint32_t height)
{
	/* Validate dimensions and calculate size */
	size_t byte_count;
	if (!image_calculate_size(width, height, &byte_count)) {
		fprintf(stderr, "image_create: invalid dimensions %u×%u\n", width, height);
		return NULL;
	}

	/* Allocate image structure */
	image_t *img = malloc(sizeof(image_t));
	if (img == NULL) {
		fprintf(stderr, "image_create: failed to allocate image_t\n");
		return NULL;
	}

	/* Allocate pixel buffer (initialized to zero - transparent black) */
	img->pixels = calloc(byte_count, 1);
	if (img->pixels == NULL) {
		fprintf(stderr, "image_create: failed to allocate %zu bytes for pixels\n", byte_count);
		free(img);
		return NULL;
	}

	/* Initialize fields */
	img->width = width;
	img->height = height;

	return img;
}

void image_destroy(image_t *img)
{
	if (img == NULL) {
		return;
	}

	/* Free pixel buffer */
	if (img->pixels != NULL) {
		free(img->pixels);
		img->pixels = NULL;
	}

	/* Free image structure */
	free(img);
}

image_t *image_scale_fit(const image_t *src, uint32_t target_width, uint32_t target_height)
{
	if (src == NULL || src->pixels == NULL) {
		fprintf(stderr, "image_scale_fit: invalid source image\n");
		return NULL;
	}

	if (target_width == 0 || target_height == 0) {
		fprintf(stderr, "image_scale_fit: invalid target dimensions %u×%u\n", target_width, target_height);
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
		fprintf(stderr, "image_scale_fit: calculated dimensions are invalid %u×%u\n", new_width, new_height);
		return NULL;
	}

	/* Create output image */
	image_t *dst = image_create(new_width, new_height);
	if (dst == NULL) {
		fprintf(stderr, "image_scale_fit: failed to create output image\n");
		return NULL;
	}

	/* Resize using stb_image_resize2 (SRGB colorspace for natural results) */
	if (!stbir_resize_uint8_srgb(src->pixels, src->width, src->height, 0, dst->pixels, new_width, new_height, 0, STBIR_RGBA)) {
		fprintf(stderr, "image_scale_fit: stbir_resize failed\n");
		image_destroy(dst);
		return NULL;
	}

	return dst;
}

image_t *image_scale_resize(const image_t *src, uint32_t target_width, uint32_t target_height)
{
	if (src == NULL || src->pixels == NULL) {
		fprintf(stderr, "image_scale_resize: invalid source image\n");
		return NULL;
	}

	if (target_width == 0 || target_height == 0) {
		fprintf(stderr, "image_scale_resize: invalid target dimensions %u×%u\n", target_width, target_height);
		return NULL;
	}

	/* Create output image with exact dimensions (no aspect ratio preservation) */
	image_t *dst = image_create(target_width, target_height);
	if (dst == NULL) {
		fprintf(stderr, "image_scale_resize: failed to create output image\n");
		return NULL;
	}

	/* Resize using stb_image_resize2 (SRGB colorspace) */
	if (!stbir_resize_uint8_srgb(src->pixels, src->width, src->height, 0, dst->pixels, target_width, target_height, 0, STBIR_RGBA)) {
		fprintf(stderr, "image_scale_resize: stbir_resize failed\n");
		image_destroy(dst);
		return NULL;
	}

	return dst;
}

image_t *convert_rgb_to_rgba(const uint8_t *rgb, uint32_t width, uint32_t height)
{
	if (rgb == NULL) {
		fprintf(stderr, "convert_rgb_to_rgba: invalid RGB data\n");
		return NULL;
	}

	/* Create RGBA image */
	image_t *img = image_create(width, height);
	if (img == NULL) {
		fprintf(stderr, "convert_rgb_to_rgba: failed to create image\n");
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

image_t *convert_grayscale_to_rgba(const uint8_t *gray, uint32_t width, uint32_t height)
{
	if (gray == NULL) {
		fprintf(stderr, "convert_grayscale_to_rgba: invalid grayscale data\n");
		return NULL;
	}

	/* Create RGBA image */
	image_t *img = image_create(width, height);
	if (img == NULL) {
		fprintf(stderr, "convert_grayscale_to_rgba: failed to create image\n");
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
