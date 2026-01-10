/**
 * @file decoder_png.c
 * @brief PNG and APNG decoder implementation using libpng
 *
 * Decodes PNG and APNG images (RGB, RGBA, indexed, interlaced, animated) to RGBA8888 format.
 * Handles transparency, gamma correction, various PNG types, and APNG animations.
 * APNG support requires libpng >= 1.6.0 with PNG_APNG_SUPPORTED flag.
 */

#include <png.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decoder.h"

/**
 * @brief Maximum number of frames for APNG (DoS protection)
 */
#define MAX_PNG_FRAMES 200

/**
 * @brief Memory reader context for libpng
 */
typedef struct {
	const uint8_t *data; /**< Pointer to PNG file data */
	size_t size; /**< Total size of data */
	size_t offset; /**< Current read offset */
} png_mem_reader;

#ifdef PNG_APNG_SUPPORTED
/**
 * @brief Custom read function for libpng to read from memory
 *
 * Callback function used by libpng to read data from a memory buffer
 * instead of a file. Updates the reader offset as data is consumed.
 *
 * @param png_ptr PNG read structure
 * @param data Output buffer to write data to
 * @param length Number of bytes to read
 */
static void png_read_func(png_structp png_ptr, png_bytep data, size_t length)
{
	png_mem_reader *reader = (png_mem_reader *)png_get_io_ptr(png_ptr);

	if (reader == NULL || reader->offset >= reader->size) {
		png_error(png_ptr, "Read error: insufficient data");
		return;
	}

	size_t available = reader->size - reader->offset;
	if (length > available) {
		png_error(png_ptr, "Read error: requested more data than available");
		return;
	}

	memcpy(data, reader->data + reader->offset, length);
	reader->offset += length;
}

/**
 * @brief Check if PNG is animated (APNG format)
 *
 * Quickly checks if a PNG file contains animation control chunk (acTL)
 * without fully decoding the image data. Returns false if APNG support
 * is not available at compile time.
 *
 * @param data Raw PNG file data
 * @param size Size of data in bytes
 * @return true if PNG has more than one frame (APNG), false otherwise or on error
 *
 * @note Returns false if PNG_APNG_SUPPORTED not defined
 * @note Returns false if data is invalid or not a PNG
 */
bool png_is_animated(const uint8_t *data, size_t size)
{
	if (data == NULL || size == 0) {
		return false;
	}

	/* Setup memory reader */
	png_mem_reader reader;
	reader.data = data;
	reader.size = size;
	reader.offset = 0;

	/* Create PNG read structures */
	png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (png_ptr == NULL) {
		return false;
	}

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL) {
		png_destroy_read_struct(&png_ptr, NULL, NULL);
		return false;
	}

	/* Set up error handling */
	if (setjmp(png_jmpbuf(png_ptr))) {
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return false;
	}

	/* Set custom read function */
	png_set_read_fn(png_ptr, &reader, png_read_func);

	/* Read PNG header */
	png_read_info(png_ptr, info_ptr);

	/* Check for acTL chunk (animation control) */
	png_uint_32 num_frames = 0;
	png_uint_32 num_plays = 0;
	bool is_animated = false;

	if (png_get_acTL(png_ptr, info_ptr, &num_frames, &num_plays) != 0) {
		/* acTL chunk exists and was read successfully */
		is_animated = (num_frames > 1);
	}

	/* Cleanup */
	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

	return is_animated;
}

/**
 * @brief Composite a frame onto the accumulator canvas
 *
 * Composites a frame's pixels onto the accumulator canvas using the specified
 * blend operation. Supports both SOURCE (replace) and OVER (alpha blend) modes.
 *
 * @param accumulator Accumulator canvas to composite onto
 * @param frame_pixels Frame pixel data (RGBA8888 format)
 * @param x_offset X offset within canvas for frame placement
 * @param y_offset Y offset within canvas for frame placement
 * @param frame_width Width of the frame
 * @param frame_height Height of the frame
 * @param canvas_width Width of the canvas (for bounds checking)
 * @param canvas_height Height of the canvas (for bounds checking)
 * @param blend_op Blend operation: 0 = SOURCE (replace), 1 = OVER (alpha blend)
 */
static void apng_composite_frame(image_t *accumulator, uint8_t *frame_pixels, uint32_t x_offset, uint32_t y_offset, uint32_t frame_width, uint32_t frame_height, uint32_t canvas_width, uint32_t canvas_height, uint8_t blend_op)
{
	if (accumulator == NULL || frame_pixels == NULL) {
		return;
	}

	for (uint32_t y = 0; y < frame_height; y++) {
		for (uint32_t x = 0; x < frame_width; x++) {
			uint32_t canvas_x = x_offset + x;
			uint32_t canvas_y = y_offset + y;

			/* Bounds check */
			if (canvas_x >= canvas_width || canvas_y >= canvas_height) {
				continue;
			}

			/* Get source pixel from frame */
			uint32_t frame_idx = (y * frame_width + x) * 4;
			uint8_t src_r = frame_pixels[frame_idx + 0];
			uint8_t src_g = frame_pixels[frame_idx + 1];
			uint8_t src_b = frame_pixels[frame_idx + 2];
			uint8_t src_a = frame_pixels[frame_idx + 3];

			/* Get destination pixel from accumulator */
			uint32_t canvas_idx = (canvas_y * canvas_width + canvas_x) * 4;
			uint8_t *dst_pixel = &accumulator->pixels[canvas_idx];

			if (blend_op == PNG_BLEND_OP_SOURCE) {
				/* BLEND_OP_SOURCE: Replace pixels (no blending) */
				dst_pixel[0] = src_r;
				dst_pixel[1] = src_g;
				dst_pixel[2] = src_b;
				dst_pixel[3] = src_a;
			} else {
				/* BLEND_OP_OVER: Alpha blending */
				uint8_t dst_a = dst_pixel[3];

				/* Calculate output alpha */
				uint16_t out_a = src_a + ((dst_a * (255 - src_a)) / 255);

				if (out_a > 0) {
					/* Blend RGB channels using pre-multiplied alpha formula */
					for (int c = 0; c < 3; c++) {
						uint8_t src_c = (c == 0) ? src_r : ((c == 1) ? src_g : src_b);
						uint8_t dst_c = dst_pixel[c];
						dst_pixel[c] = (uint8_t)(((uint16_t)src_c * src_a + (uint16_t)dst_c * dst_a * (255 - src_a) / 255) / out_a);
					}
					dst_pixel[3] = (uint8_t)out_a;
				}
			}
		}
	}
}

/**
 * @brief Decode animated PNG (APNG) image using libpng
 *
 * Decodes APNG files with full support for:
 * - Frame composition with accumulator canvas
 * - Disposal operations (NONE, BACKGROUND, PREVIOUS)
 * - Blend operations (SOURCE, OVER with alpha blending)
 * - Frame offsets (partial frames)
 * - Frame limit (MAX_PNG_FRAMES for DoS protection)
 *
 * @param data Raw PNG file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded
 * @return Array of image_t* frames, or NULL on error
 *
 * @note Requires PNG_APNG_SUPPORTED to be defined
 * @note Output format is always RGBA8888
 * @note Frames are capped at MAX_PNG_FRAMES (200)
 */
static image_t **decode_png_animated(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_png_animated\n");
		return NULL;
	}

	/* Initialize output */
	*frame_count = 0;

	/* Setup memory reader */
	png_mem_reader reader;
	reader.data = data;
	reader.size = len;
	reader.offset = 0;

	/* Create PNG read structures */
	png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (png_ptr == NULL) {
		fprintf(stderr, "Error: Failed to create PNG read struct\n");
		return NULL;
	}

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL) {
		fprintf(stderr, "Error: Failed to create PNG info struct\n");
		png_destroy_read_struct(&png_ptr, NULL, NULL);
		return NULL;
	}

	/* Set up error handling */
	if (setjmp(png_jmpbuf(png_ptr))) {
		fprintf(stderr, "Error: PNG decoding error\n");
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return NULL;
	}

	/* Set custom read function */
	png_set_read_fn(png_ptr, &reader, png_read_func);

	/* Read PNG header */
	png_read_info(png_ptr, info_ptr);

	/* Get acTL chunk (animation control) */
	png_uint_32 num_frames = 0;
	png_uint_32 num_plays = 0;
	if (png_get_acTL(png_ptr, info_ptr, &num_frames, &num_plays) == 0) {
		fprintf(stderr, "Error: PNG does not contain acTL chunk (not an APNG)\n");
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return NULL;
	}

	/* Apply frame limit (DoS protection) */
	if (num_frames > MAX_PNG_FRAMES) {
		fprintf(stderr, "Warning: PNG has %u frames, limiting to %d\n", num_frames, MAX_PNG_FRAMES);
		num_frames = MAX_PNG_FRAMES;
	}

	if (num_frames == 0) {
		fprintf(stderr, "Error: APNG has no frames\n");
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return NULL;
	}

	/* Get canvas dimensions */
	uint32_t canvas_width = png_get_image_width(png_ptr, info_ptr);
	uint32_t canvas_height = png_get_image_height(png_ptr, info_ptr);
	int color_type = png_get_color_type(png_ptr, info_ptr);
	int bit_depth = png_get_bit_depth(png_ptr, info_ptr);

	/* Apply color transformations to normalize to RGBA8888 */
	if (color_type == PNG_COLOR_TYPE_PALETTE) {
		png_set_palette_to_rgb(png_ptr);
	}
	if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
		png_set_gray_to_rgb(png_ptr);
	}
	if (!(color_type & PNG_COLOR_MASK_ALPHA)) {
		png_set_add_alpha(png_ptr, 0xFF, PNG_FILLER_AFTER);
	}
	if (bit_depth == 16) {
		png_set_strip_16(png_ptr);
	}
	if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
		png_set_tRNS_to_alpha(png_ptr);
	}
	png_read_update_info(png_ptr, info_ptr);

	/* Allocate frames array */
	image_t **frames = (image_t **)malloc(sizeof(image_t *) * num_frames);
	if (frames == NULL) {
		fprintf(stderr, "Error: Failed to allocate frames array\n");
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return NULL;
	}

	/* Initialize frames to NULL for cleanup */
	for (uint32_t i = 0; i < num_frames; i++) {
		frames[i] = NULL;
	}

	/* Create accumulator canvas for frame composition */
	image_t *accumulator = image_create(canvas_width, canvas_height);
	if (accumulator == NULL) {
		fprintf(stderr, "Error: Failed to create accumulator canvas\n");
		free(frames);
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return NULL;
	}

	/* Previous frame backup for DISPOSE_PREVIOUS */
	image_t *previous = NULL;

	/* Process each frame */
	for (uint32_t frame_idx = 0; frame_idx < num_frames; frame_idx++) {
		/* Read fcTL chunk (frame control) */
		png_uint_32 frame_width, frame_height;
		png_uint_32 x_offset, y_offset;
		png_uint_16 delay_num, delay_den;
		png_byte dispose_op, blend_op;

		png_read_frame_head(png_ptr, info_ptr);

		if (png_get_valid(png_ptr, info_ptr, PNG_INFO_fcTL)) {
			png_get_next_frame_fcTL(png_ptr, info_ptr, &frame_width, &frame_height, &x_offset, &y_offset, &delay_num, &delay_den, &dispose_op, &blend_op);
		} else {
			/* First frame might not have fcTL, use canvas dimensions */
			frame_width = canvas_width;
			frame_height = canvas_height;
			x_offset = 0;
			y_offset = 0;
			dispose_op = PNG_DISPOSE_OP_NONE;
			blend_op = PNG_BLEND_OP_SOURCE;
		}

		/* Validate frame dimensions and offsets */
		if (x_offset + frame_width > canvas_width || y_offset + frame_height > canvas_height) {
			fprintf(stderr, "Error: Frame %u dimensions exceed canvas bounds\n", frame_idx);
			goto cleanup_error;
		}

		/* Backup accumulator for DISPOSE_PREVIOUS */
		if (dispose_op == PNG_DISPOSE_OP_PREVIOUS && previous == NULL) {
			previous = image_create(canvas_width, canvas_height);
			if (previous == NULL) {
				fprintf(stderr, "Error: Failed to create previous frame backup\n");
				goto cleanup_error;
			}
		}

		if (dispose_op == PNG_DISPOSE_OP_PREVIOUS && previous != NULL) {
			memcpy(previous->pixels, accumulator->pixels, canvas_width * canvas_height * 4);
		}

		/* Allocate row pointers for frame */
		png_bytep *row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * frame_height);
		if (row_pointers == NULL) {
			fprintf(stderr, "Error: Failed to allocate row pointers\n");
			goto cleanup_error;
		}

		/* Allocate frame buffer */
		uint8_t *frame_buffer = (uint8_t *)malloc(frame_width * frame_height * 4);
		if (frame_buffer == NULL) {
			fprintf(stderr, "Error: Failed to allocate frame buffer\n");
			free(row_pointers);
			goto cleanup_error;
		}

		/* Setup row pointers */
		for (uint32_t y = 0; y < frame_height; y++) {
			row_pointers[y] = frame_buffer + (y * frame_width * 4);
		}

		/* Read frame image data */
		png_read_image(png_ptr, row_pointers);

		/* Composite frame onto accumulator */
		apng_composite_frame(accumulator, frame_buffer, x_offset, y_offset, frame_width, frame_height, canvas_width, canvas_height, blend_op);

		/* Copy accumulator to output frame */
		frames[frame_idx] = image_create(canvas_width, canvas_height);
		if (frames[frame_idx] == NULL) {
			fprintf(stderr, "Error: Failed to create output frame %u\n", frame_idx);
			free(frame_buffer);
			free(row_pointers);
			goto cleanup_error;
		}

		memcpy(frames[frame_idx]->pixels, accumulator->pixels, canvas_width * canvas_height * 4);

		/* Free temporary buffers */
		free(frame_buffer);
		free(row_pointers);

		/* Apply disposal method for next frame */
		switch (dispose_op) {
			case PNG_DISPOSE_OP_NONE:
				/* Keep current frame as-is for next frame */
				break;

			case PNG_DISPOSE_OP_BACKGROUND:
				/* Clear frame area to transparent black */
				for (uint32_t y = 0; y < frame_height; y++) {
					for (uint32_t x = 0; x < frame_width; x++) {
						uint32_t canvas_x = x_offset + x;
						uint32_t canvas_y = y_offset + y;
						if (canvas_x < canvas_width && canvas_y < canvas_height) {
							image_set_pixel(accumulator, canvas_x, canvas_y, 0, 0, 0, 0);
						}
					}
				}
				break;

			case PNG_DISPOSE_OP_PREVIOUS:
				/* Restore to previous frame */
				if (previous != NULL) {
					memcpy(accumulator->pixels, previous->pixels, canvas_width * canvas_height * 4);
				}
				break;

			default:
				/* Unknown disposal mode, treat as NONE */
				break;
		}
	}

	/* Cleanup */
	image_destroy(accumulator);
	if (previous != NULL) {
		image_destroy(previous);
	}
	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

	*frame_count = num_frames;
	return frames;

cleanup_error:
	/* Cleanup on error */
	image_destroy(accumulator);
	if (previous != NULL) {
		image_destroy(previous);
	}
	for (uint32_t i = 0; i < num_frames; i++) {
		if (frames[i] != NULL) {
			image_destroy(frames[i]);
		}
	}
	free(frames);
	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
	return NULL;
}
#endif /* PNG_APNG_SUPPORTED */

/**
 * @brief Decode static PNG image using libpng simplified API
 *
 * Uses the simplified libpng API (png_image) for easier decoding.
 * Automatically handles:
 * - Color type conversion (RGB, RGBA, indexed, grayscale)
 * - Interlaced PNGs (Adam7)
 * - Transparency (tRNS chunk)
 * - Gamma correction
 *
 * @param data Raw PNG file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (PNG is static)
 * @return Array with single image_t*, or NULL on error
 *
 * @note This function handles only static PNG files
 * @note Output format is always RGBA8888
 */
static image_t **decode_png_static(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_png\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Initialize png_image structure
	png_image image;
	memset(&image, 0, sizeof(png_image));
	image.version = PNG_IMAGE_VERSION;

	// Begin reading from memory buffer
	if (png_image_begin_read_from_memory(&image, data, len) == 0) {
		fprintf(stderr, "Error: libpng failed to read PNG header: %s\n", image.message);
		png_image_free(&image);
		return NULL;
	}

	// Force RGBA8888 output format (8 bits per channel, 4 channels)
	image.format = PNG_FORMAT_RGBA;

	// Calculate required buffer size
	size_t buffer_size = PNG_IMAGE_SIZE(image);
	if (buffer_size == 0) {
		fprintf(stderr, "Error: libpng returned zero buffer size\n");
		png_image_free(&image);
		return NULL;
	}

	// Allocate temporary buffer for decoded pixels
	uint8_t *buffer = (uint8_t *)malloc(buffer_size);
	if (buffer == NULL) {
		fprintf(stderr, "Error: Failed to allocate PNG decode buffer (%zu bytes)\n", buffer_size);
		png_image_free(&image);
		return NULL;
	}

	// Finish reading and decode
	// Parameters:
	// - image: png_image structure
	// - NULL: no background color
	// - buffer: output buffer
	// - 0: row stride (0 = automatic)
	// - NULL: no colormap
	if (png_image_finish_read(&image, NULL, buffer, 0, NULL) == 0) {
		fprintf(stderr, "Error: libpng failed to decode PNG: %s\n", image.message);
		free(buffer);
		png_image_free(&image);
		return NULL;
	}

	// Create image_t structure
	image_t *img = image_create(image.width, image.height);
	if (img == NULL) {
		fprintf(stderr, "Error: Failed to create image_t structure\n");
		free(buffer);
		png_image_free(&image);
		return NULL;
	}

	// Copy pixel data to image_t
	size_t pixel_count = (size_t)image.width * (size_t)image.height * 4;
	memcpy(img->pixels, buffer, pixel_count);

	// Free temporary buffer
	free(buffer);

	// Cleanup png_image structure
	png_image_free(&image);

	// Allocate frames array (single frame for PNG)
	image_t **frames = (image_t **)malloc(sizeof(image_t *));
	if (frames == NULL) {
		fprintf(stderr, "Error: Failed to allocate frames array\n");
		image_destroy(img);
		return NULL;
	}

	frames[0] = img;
	*frame_count = 1;

	// fprintf(stderr, "PNG decoded: %ux%u, %u-bit RGBA\n", image.width, image.height, PNG_IMAGE_SAMPLE_SIZE(image.format) * 8);

	return frames;
}

/**
 * @brief Decode PNG image (router function for static/animated)
 *
 * Main entry point for PNG decoding. Automatically detects whether the
 * PNG is static or animated (APNG) and routes to the appropriate decoder.
 *
 * @param data Raw PNG file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded (1 for static, N for APNG)
 * @return Array of image_t* frames, or NULL on error
 *
 * @note For static PNG: returns 1 frame
 * @note For APNG: returns N frames (capped at MAX_PNG_FRAMES)
 * @note Output format is always RGBA8888
 */
image_t **decode_png(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_png\n");
		return NULL;
	}

	/* Initialize output */
	*frame_count = 0;

	/* Check if animated */
#ifdef PNG_APNG_SUPPORTED
	if (png_is_animated(data, len)) {
		return decode_png_animated(data, len, frame_count);
	}
#endif /* PNG_APNG_SUPPORTED */

	return decode_png_static(data, len, frame_count);
}
