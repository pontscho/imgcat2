/**
 * @file encoder_png.c
 * @brief PNG encoder implementation using libpng
 *
 * Encodes RGBA8888 images to PNG format using libpng with compression control.
 * Preserves alpha channel (PNG supports transparency).
 */

#include <png.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../encoders/encoder.h"

/**
 * @brief PNG memory write state structure
 *
 * Used by png_memory_write_func() to accumulate PNG output data in memory.
 */
typedef struct {
	uint8_t *buffer; /**< Output buffer (dynamically resized) */
	size_t size; /**< Current size of valid data in buffer */
	size_t capacity; /**< Total allocated capacity of buffer */
} png_memory_write_state;

/**
 * @brief PNG memory write callback
 *
 * Custom write function for libpng that writes PNG data to a memory buffer
 * instead of a file. Automatically resizes the buffer as needed.
 *
 * @param png_ptr PNG write structure
 * @param data Data to write
 * @param length Number of bytes to write
 */
static void png_memory_write_func(png_structp png_ptr, png_bytep data, size_t length)
{
	png_memory_write_state *state = (png_memory_write_state *)png_get_io_ptr(png_ptr);

	if (state == NULL) {
		png_error(png_ptr, "Write error: NULL state pointer");
		return;
	}

	/* Check if we need to resize buffer */
	if (state->size + length > state->capacity) {
		/* Double capacity until it fits */
		size_t new_capacity = state->capacity;
		if (new_capacity == 0) {
			new_capacity = 4096; /* Initial capacity: 4KB */
		}
		while (new_capacity < state->size + length) {
			new_capacity *= 2;
		}

		/* Reallocate buffer */
		uint8_t *new_buffer = (uint8_t *)realloc(state->buffer, new_capacity);
		if (new_buffer == NULL) {
			png_error(png_ptr, "Write error: Failed to reallocate buffer");
			return;
		}

		state->buffer = new_buffer;
		state->capacity = new_capacity;
	}

	/* Append data to buffer */
	memcpy(state->buffer + state->size, data, length);
	state->size += length;
}

/**
 * @brief PNG memory flush callback
 *
 * No-op flush function for memory write (data is always in memory).
 *
 * @param png_ptr PNG write structure
 */
static void png_memory_flush_func(png_structp png_ptr)
{
	(void)png_ptr; /* Unused */
	/* No-op for memory write */
}

/**
 * @brief PNG error handler
 *
 * Called by libpng when a fatal error occurs. Uses longjmp to
 * return control to encode_png() instead of calling abort().
 *
 * @param png_ptr PNG write structure
 * @param error_msg Error message
 */
static void png_error_handler(png_structp png_ptr, png_const_charp error_msg)
{
	fprintf(stderr, "Error: libpng error: %s\n", error_msg);

	/* Jump back to encode_png() */
	jmp_buf *jmpbuf = (jmp_buf *)png_get_error_ptr(png_ptr);
	if (jmpbuf != NULL) {
		longjmp(*jmpbuf, 1);
	}
}

/**
 * @brief PNG warning handler
 *
 * Called by libpng when a non-fatal warning occurs.
 *
 * @param png_ptr PNG write structure
 * @param warning_msg Warning message
 */
static void png_warning_handler(png_structp png_ptr, png_const_charp warning_msg)
{
	(void)png_ptr; /* Unused */
	fprintf(stderr, "Warning: libpng warning: %s\n", warning_msg);
}

/**
 * @brief Encode RGBA image to PNG format
 *
 * Encodes RGBA8888 image to PNG format with compression control.
 * Preserves alpha channel (PNG supports transparency).
 *
 * @param img Input image in RGBA8888 format
 * @param quality PNG compression level (0-9, higher = better compression, slower)
 * @param out_data Output parameter for encoded data (caller must free)
 * @param out_size Output parameter for encoded data size in bytes
 * @return 0 on success, -1 on error
 *
 * @note PNG preserves alpha channel (supports transparency)
 * @note Caller must free *out_data with free() when done
 */
int encode_png(const image_t *img, int quality, uint8_t **out_data, size_t *out_size)
{
	if (img == NULL || out_data == NULL || out_size == NULL) {
		fprintf(stderr, "Error: Invalid parameters to encode_png\n");
		return -1;
	}

	/* Initialize outputs */
	*out_data = NULL;
	*out_size = 0;

	/* Clamp compression to valid range [0, 9] */
	if (quality < 0) {
		// fprintf(stderr, "Warning: PNG compression %d < 0, clamping to 0\n", quality);
		quality = 0;
	}
	if (quality > 9) {
		// fprintf(stderr, "Warning: PNG compression %d > 9, clamping to 9\n", quality);
		quality = 9;
	}

	/* Initialize memory write state */
	png_memory_write_state write_state;
	write_state.buffer = NULL;
	write_state.size = 0;
	write_state.capacity = 0;

	/* Create PNG write structure */
	jmp_buf jmpbuf;
	png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, &jmpbuf, png_error_handler, png_warning_handler);
	if (png_ptr == NULL) {
		fprintf(stderr, "Error: Failed to create PNG write struct\n");
		return -1;
	}

	/* Create PNG info structure */
	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL) {
		fprintf(stderr, "Error: Failed to create PNG info struct\n");
		png_destroy_write_struct(&png_ptr, NULL);
		return -1;
	}

	/* Setup error handling with longjmp */
	if (setjmp(jmpbuf)) {
		/* longjmp returns here if error occurs */
		png_destroy_write_struct(&png_ptr, &info_ptr);
		if (write_state.buffer != NULL) {
			free(write_state.buffer);
		}
		return -1;
	}

	/* Set custom write function */
	png_set_write_fn(png_ptr, &write_state, png_memory_write_func, png_memory_flush_func);

	/* Set compression level */
	png_set_compression_level(png_ptr, quality);

	/* Set IHDR (image header) */
	png_set_IHDR(
	    png_ptr,
	    info_ptr,
	    img->width,
	    img->height,
	    8, /* bit depth */
	    PNG_COLOR_TYPE_RGBA, /* color type with alpha */
	    PNG_INTERLACE_NONE,
	    PNG_COMPRESSION_TYPE_DEFAULT,
	    PNG_FILTER_TYPE_DEFAULT);

	/* Write info */
	png_write_info(png_ptr, info_ptr);

	/* Prepare row pointers (each row points to pixel data in img->pixels) */
	png_bytep *row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * img->height);
	if (row_pointers == NULL) {
		fprintf(stderr, "Error: Failed to allocate row pointers\n");
		png_destroy_write_struct(&png_ptr, &info_ptr);
		if (write_state.buffer != NULL) {
			free(write_state.buffer);
		}
		return -1;
	}

	/* Setup row pointers to point to each row in the image */
	for (uint32_t y = 0; y < img->height; y++) {
		row_pointers[y] = img->pixels + (y * img->width * 4); /* RGBA: 4 bytes per pixel */
	}

	/* Write image data */
	png_write_image(png_ptr, row_pointers);

	/* Write end */
	png_write_end(png_ptr, NULL);

	/* Free row pointers */
	free(row_pointers);

	/* Cleanup PNG structures */
	png_destroy_write_struct(&png_ptr, &info_ptr);

	/* Allocate and copy output data */
	if (write_state.buffer == NULL || write_state.size == 0) {
		fprintf(stderr, "Error: PNG encoder produced no output\n");
		if (write_state.buffer != NULL) {
			free(write_state.buffer);
		}
		return -1;
	}

	*out_data = (uint8_t *)malloc(write_state.size);
	if (*out_data == NULL) {
		fprintf(stderr, "Error: Failed to allocate output buffer (%zu bytes)\n", write_state.size);
		free(write_state.buffer);
		return -1;
	}

	memcpy(*out_data, write_state.buffer, write_state.size);
	*out_size = write_state.size;

	/* Free write state buffer */
	free(write_state.buffer);

	return 0;
}
