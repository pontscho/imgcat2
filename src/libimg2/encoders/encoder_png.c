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

#include "encoder.h"
#include "../core/log.h"

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
 * return control to img2_encode_png() instead of calling abort().
 *
 * @param png_ptr PNG write structure
 * @param error_msg Error message
 */
static void png_error_handler(png_structp png_ptr, png_const_charp error_msg)
{
	img2_log(IMG2_LOG_ERROR, "Error: libpng error: %s", error_msg);

	/* Jump back to img2_encode_png() */
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
	img2_log(IMG2_LOG_WARN, "Warning: libpng warning: %s", warning_msg);
}

/**
 * @brief Run libpng over prepared rows, appending the PNG stream to *state
 *
 * The only function with a setjmp(): everything that must survive a libpng error (output buffer, row
 * pointers) is owned by the caller, because automatic objects changed between setjmp() and longjmp() are
 * indeterminate afterwards. On error *state may hold a partial buffer; the caller frees it.
 *
 * @return IMG2_OK, or IMG2_E_CODEC (libpng error, including a failed write-buffer allocation)
 */
static img2_status_t encode_png_rows(const img2_image_t *img, int compression, bool interlace, png_bytep *row_pointers, png_memory_write_state *state)
{
	/* Create PNG write structure */
	jmp_buf jmpbuf;
	png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, &jmpbuf, png_error_handler, png_warning_handler);
	if (png_ptr == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create PNG write struct");
		return IMG2_E_CODEC;
	}

	/* Create PNG info structure */
	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create PNG info struct");
		png_destroy_write_struct(&png_ptr, NULL);
		return IMG2_E_CODEC;
	}

	/* Setup error handling with longjmp (png_ptr / info_ptr are not changed after this point) */
	if (setjmp(jmpbuf)) {
		/* longjmp returns here if error occurs */
		png_destroy_write_struct(&png_ptr, &info_ptr);
		return IMG2_E_CODEC;
	}

	/* Set custom write function */
	png_set_write_fn(png_ptr, state, png_memory_write_func, png_memory_flush_func);

	/* Set compression level */
	png_set_compression_level(png_ptr, compression);

	/* Set IHDR (image header) */
	png_set_IHDR(
	    png_ptr,
	    info_ptr,
	    img->width,
	    img->height,
	    8, /* bit depth */
	    PNG_COLOR_TYPE_RGBA, /* color type with alpha */
	    interlace ? PNG_INTERLACE_ADAM7 : PNG_INTERLACE_NONE, /* png_write_image() runs the Adam7 passes */
	    PNG_COMPRESSION_TYPE_DEFAULT,
	    PNG_FILTER_TYPE_DEFAULT);

	/* Write info, image data, end */
	png_write_info(png_ptr, info_ptr);
	png_write_image(png_ptr, row_pointers);
	png_write_end(png_ptr, NULL);

	/* Cleanup PNG structures */
	png_destroy_write_struct(&png_ptr, &info_ptr);
	return IMG2_OK;
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
img2_status_t img2_encode_png(const img2_image_t *img, int quality, uint8_t **out_data, size_t *out_size)
{
	return img2_encode_png_opts(img, quality, NULL, out_data, out_size);
}

/**
 * @brief img2_encode_png() with encode options (NULL = defaults)
 *
 * progressive: Adam7 interlace; effort e: zlib level e-1 when opts->quality is the default (-1), else a WARN
 * (an explicit quality selects the level, docs/encoder-api.md decision 3); lossless: no-op (always lossless).
 * chroma, alpha_quality and threads have no PNG equivalent (one WARN each).
 */
img2_status_t img2_encode_png_opts(const img2_image_t *img, int quality, const img2_encode_opts_t *opts, uint8_t **out_data, size_t *out_size)
{
	if (img == NULL || out_data == NULL || out_size == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to encode_png");
		return IMG2_E_ARG;
	}

	/* Initialize outputs */
	*out_data = NULL;
	*out_size = 0;

	const img2_encode_opts_t *o = img2_encode_opts_or_default(opts);
	if (o->effort > 0) {
		if (o->quality < 0) {
			quality = o->effort - 1;
		} else {
			img2_encode_warn_ignored("effort", "PNG", "an explicit quality selects the zlib level");
		}
	}
	if (o->chroma != IMG2_CHROMA_DEFAULT) {
		char option[16];
		snprintf(option, sizeof(option), "chroma %s", img2_chroma_name(o->chroma));
		img2_encode_warn_ignored(option, "PNG", "always RGB");
	}
	if (o->alpha_quality > 0) {
		img2_encode_warn_ignored("alpha_quality", "PNG", "always lossless");
	}
	if (o->threads > 0) {
		img2_encode_warn_ignored("threads", "PNG", "single-threaded encoder");
	}

	/* Clamp compression to valid range [0, 9] */
	int png_compression = quality;
	if (png_compression < 0) {
		png_compression = 0;
	}
	if (png_compression > 9) {
		png_compression = 9;
	}

	/* Prepare row pointers (each row points to pixel data in img->pixels) */
	png_bytep *row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * img->height);
	if (row_pointers == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate row pointers");
		return IMG2_E_NOMEM;
	}
	for (uint32_t y = 0; y < img->height; y++) {
		row_pointers[y] = img->pixels + (y * img->width * 4); /* RGBA: 4 bytes per pixel */
	}

	/* Encode into the memory write state */
	png_memory_write_state write_state;
	write_state.buffer = NULL;
	write_state.size = 0;
	write_state.capacity = 0;
	img2_status_t st = encode_png_rows(img, png_compression, o->progressive, row_pointers, &write_state);
	free(row_pointers);
	if (st != IMG2_OK) {
		free(write_state.buffer);
		return st;
	}

	/* Allocate and copy output data */
	if (write_state.buffer == NULL || write_state.size == 0) {
		img2_log(IMG2_LOG_ERROR, "Error: PNG encoder produced no output");
		if (write_state.buffer != NULL) {
			free(write_state.buffer);
		}
		return IMG2_E_CODEC;
	}

	*out_data = (uint8_t *)malloc(write_state.size);
	if (*out_data == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate output buffer (%zu bytes)", write_state.size);
		free(write_state.buffer);
		return IMG2_E_NOMEM;
	}

	memcpy(*out_data, write_state.buffer, write_state.size);
	*out_size = write_state.size;

	/* Free write state buffer */
	free(write_state.buffer);

	return IMG2_OK;
}
