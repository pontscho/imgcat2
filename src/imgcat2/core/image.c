/**
 * @file image.c
 * @brief Image data structure implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "image.h"

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
