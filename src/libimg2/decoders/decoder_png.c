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
#include "../core/log.h"

#ifdef IMG2_HAVE_EXIF_READER
#include "../metadata/exif_reader.h"
#endif

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

#ifdef IMG2_HAVE_EXIF_READER
/**
 * @brief Extract EXIF metadata from PNG using full libpng API
 *
 * Uses the full libpng API to access eXIf chunk data and parse it as TIFF.
 * PNG stores EXIF data in raw TIFF format in the eXIf chunk.
 *
 * @param data Raw PNG file data
 * @param len Length of data in bytes
 * @return Pointer to img2_exif_t structure, or NULL if no EXIF found or on error
 */
static img2_exif_t *extract_png_exif(const uint8_t *data, size_t len)
{
	if (data == NULL || len == 0) {
		return NULL;
	}

	/* Setup memory reader */
	png_mem_reader reader;
	reader.data = data;
	reader.size = len;
	reader.offset = 0;

	/* Create PNG read structures */
	png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (png_ptr == NULL) {
		return NULL;
	}

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL) {
		png_destroy_read_struct(&png_ptr, NULL, NULL);
		return NULL;
	}

	/* Set up error handling */
	if (setjmp(png_jmpbuf(png_ptr))) {
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return NULL;
	}

	/* Set custom read function */
	png_set_read_fn(png_ptr, &reader, png_read_func);

	/* Read PNG header and chunks */
	png_read_info(png_ptr, info_ptr);

	/* Try to get eXIf chunk */
	png_uint_32 exif_size = 0;
	png_bytep exif_data = NULL;

#if PNG_LIBPNG_VER >= 10617
	/* png_get_eXIf() available in libpng >= 1.6.17 */
	if (png_get_eXIf_1(png_ptr, info_ptr, &exif_size, &exif_data) != 0 && exif_data != NULL && exif_size > 0) {
		/* Allocate and initialize img2_exif_t */
		img2_exif_t *exif = malloc(sizeof(img2_exif_t));
		if (exif == NULL) {
			png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
			return NULL;
		}

		img2_exif_init(exif);

		/* Parse EXIF data (raw TIFF format) */
		if (img2_exif_parse_tiff(exif, exif_data, exif_size) != 0) {
			/* Parse failed */
			free(exif);
			exif = NULL;
		}

		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return exif;
	}
#endif

	/* No EXIF data found or libpng version too old */
	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
	return NULL;
}

/**
 * @brief Extract text metadata from PNG tEXt chunks and populate EXIF structure
 *
 * Reads PNG tEXt chunks and maps common keywords to EXIF fields:
 * - Title, Description -> description
 * - Author, Artist -> artist
 * - Copyright -> copyright
 * - Comment -> user_comment
 * - Software -> software
 *
 * @param exif EXIF structure to populate (must be initialized)
 * @param data Raw PNG file data
 * @param len Length of data in bytes
 * @return 0 on success, -1 on error
 */
static int extract_png_text_to_exif(img2_exif_t *exif, const uint8_t *data, size_t len)
{
	if (exif == NULL || data == NULL || len == 0) {
		return -1;
	}

	/* Setup memory reader */
	png_mem_reader reader;
	reader.data = data;
	reader.size = len;
	reader.offset = 0;

	/* Create PNG read structures */
	png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (png_ptr == NULL) {
		return -1;
	}

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL) {
		png_destroy_read_struct(&png_ptr, NULL, NULL);
		return -1;
	}

	/* Set up error handling */
	if (setjmp(png_jmpbuf(png_ptr))) {
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return -1;
	}

	/* Set custom read function */
	png_set_read_fn(png_ptr, &reader, png_read_func);

	/* Read PNG header and chunks */
	png_read_info(png_ptr, info_ptr);

	/* Try to get text chunks */
	png_textp text_ptr = NULL;
	int num_text = 0;

	if (png_get_text(png_ptr, info_ptr, &text_ptr, &num_text) > 0 && text_ptr != NULL) {
		/* Process each text chunk */
		for (int i = 0; i < num_text; i++) {
			if (text_ptr[i].key == NULL || text_ptr[i].text == NULL) {
				continue;
			}

			const char *key = text_ptr[i].key;
			const char *text = text_ptr[i].text;

			/* Map common PNG tEXt keywords to EXIF fields */
			if (strcmp(key, "Title") == 0 || strcmp(key, "Description") == 0) {
				strncpy(exif->description, text, sizeof(exif->description) - 1);
				exif->description[sizeof(exif->description) - 1] = 0;

			} else if (strcmp(key, "Author") == 0 || strcmp(key, "Artist") == 0) {
				strncpy(exif->artist, text, sizeof(exif->artist) - 1);
				exif->artist[sizeof(exif->artist) - 1] = 0;

			} else if (strcmp(key, "Copyright") == 0) {
				strncpy(exif->copyright, text, sizeof(exif->copyright) - 1);
				exif->copyright[sizeof(exif->copyright) - 1] = 0;

			} else if (strcmp(key, "Comment") == 0) {
				strncpy(exif->user_comment, text, sizeof(exif->user_comment) - 1);
				exif->user_comment[sizeof(exif->user_comment) - 1] = 0;

			} else if (strcmp(key, "Software") == 0) {
				strncpy(exif->software, text, sizeof(exif->software) - 1);
				exif->software[sizeof(exif->software) - 1] = 0;
			}
		}
	}

	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
	return 0;
}

/**
 * @brief Extract XMP metadata from PNG using full libpng API
 *
 * Uses the full libpng API to access iTXt chunks and search for XMP data.
 * PNG stores XMP data in iTXt chunks with keyword "XML:com.adobe.xmp".
 *
 * @param data Raw PNG file data
 * @param len Length of data in bytes
 * @return Pointer to img2_xmp_t structure, or NULL if no XMP found or on error
 */
static img2_xmp_t *extract_png_xmp(const uint8_t *data, size_t len)
{
	if (data == NULL || len == 0) {
		return NULL;
	}

	/* Setup memory reader */
	png_mem_reader reader;
	reader.data = data;
	reader.size = len;
	reader.offset = 0;

	/* Create PNG read structures */
	png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (png_ptr == NULL) {
		return NULL;
	}

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL) {
		png_destroy_read_struct(&png_ptr, NULL, NULL);
		return NULL;
	}

	/* Set up error handling */
	if (setjmp(png_jmpbuf(png_ptr))) {
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return NULL;
	}

	/* Set custom read function */
	png_set_read_fn(png_ptr, &reader, png_read_func);

	/* Read PNG header and chunks */
	png_read_info(png_ptr, info_ptr);

	/* Try to get text chunks */
	png_textp text_ptr = NULL;
	int num_text = 0;

	if (png_get_text(png_ptr, info_ptr, &text_ptr, &num_text) > 0 && text_ptr != NULL) {
		/* Search for XMP chunk with keyword "XML:com.adobe.xmp" */
		for (int i = 0; i < num_text; i++) {
			if (text_ptr[i].key != NULL && strcmp(text_ptr[i].key, "XML:com.adobe.xmp") == 0) {
				/* Found XMP data */
				if (text_ptr[i].text != NULL && text_ptr[i].text_length > 0) {
					/* Allocate and initialize img2_xmp_t */
					img2_xmp_t *xmp = malloc(sizeof(img2_xmp_t));
					if (xmp == NULL) {
						png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
						return NULL;
					}

					img2_xmp_init(xmp);

					/* Parse XMP data (raw XML string) */
					if (img2_xmp_parse_xml(xmp, text_ptr[i].text, text_ptr[i].text_length) != 0) {
						/* Parse failed */
						img2_xmp_clear(xmp);
						free(xmp);
						xmp = NULL;
					}

					png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
					return xmp;
				}
			}
		}
	}

	/* No XMP data found */
	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
	return NULL;
}
#endif /* IMG2_HAVE_EXIF_READER */

#ifdef PNG_APNG_SUPPORTED

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
bool img2_png_is_animated(const uint8_t *data, size_t size)
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
static void apng_composite_frame(img2_image_t *accumulator, uint8_t *frame_pixels, uint32_t x_offset, uint32_t y_offset, uint32_t frame_width, uint32_t frame_height, uint32_t canvas_width, uint32_t canvas_height, uint8_t blend_op)
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
 * @return Array of img2_image_t* frames, or NULL on error
 *
 * @note Requires PNG_APNG_SUPPORTED to be defined
 * @note Output format is always RGBA8888
 * @note Frames are capped at MAX_PNG_FRAMES (200)
 */
static img2_image_t **decode_png_animated(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_png_animated");
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
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create PNG read struct");
		return NULL;
	}

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create PNG info struct");
		png_destroy_read_struct(&png_ptr, NULL, NULL);
		return NULL;
	}

	/* Set up error handling */
	if (setjmp(png_jmpbuf(png_ptr))) {
		img2_log(IMG2_LOG_ERROR, "Error: PNG decoding error");
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
		img2_log(IMG2_LOG_ERROR, "Error: PNG does not contain acTL chunk (not an APNG)");
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return NULL;
	}

	/* Apply frame limit (DoS protection) */
	if (num_frames > MAX_PNG_FRAMES) {
		img2_log(IMG2_LOG_WARN, "Warning: PNG has %u frames, limiting to %d", num_frames, MAX_PNG_FRAMES);
		num_frames = MAX_PNG_FRAMES;
	}

	if (num_frames == 0) {
		img2_log(IMG2_LOG_ERROR, "Error: APNG has no frames");
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
	img2_image_t **frames = (img2_image_t **)malloc(sizeof(img2_image_t *) * num_frames);
	if (frames == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate frames array");
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return NULL;
	}

	/* Initialize frames to NULL for cleanup */
	for (uint32_t i = 0; i < num_frames; i++) {
		frames[i] = NULL;
	}

	/* Create accumulator canvas for frame composition */
	img2_image_t *accumulator = img2_image_alloc(canvas_width, canvas_height);
	if (accumulator == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create accumulator canvas");
		free(frames);
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return NULL;
	}

	/* Previous frame backup for DISPOSE_PREVIOUS */
	img2_image_t *previous = NULL;

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
			img2_log(IMG2_LOG_ERROR, "Error: Frame %u dimensions exceed canvas bounds", frame_idx);
			goto cleanup_error;
		}

		/* Backup accumulator for DISPOSE_PREVIOUS */
		if (dispose_op == PNG_DISPOSE_OP_PREVIOUS && previous == NULL) {
			previous = img2_image_alloc(canvas_width, canvas_height);
			if (previous == NULL) {
				img2_log(IMG2_LOG_ERROR, "Error: Failed to create previous frame backup");
				goto cleanup_error;
			}
		}

		if (dispose_op == PNG_DISPOSE_OP_PREVIOUS && previous != NULL) {
			memcpy(previous->pixels, accumulator->pixels, canvas_width * canvas_height * 4);
		}

		/* Allocate row pointers for frame */
		png_bytep *row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * frame_height);
		if (row_pointers == NULL) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate row pointers");
			goto cleanup_error;
		}

		/* Allocate frame buffer */
		uint8_t *frame_buffer = (uint8_t *)malloc(frame_width * frame_height * 4);
		if (frame_buffer == NULL) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate frame buffer");
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
		frames[frame_idx] = img2_image_alloc(canvas_width, canvas_height);
		if (frames[frame_idx] == NULL) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to create output frame %u", frame_idx);
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
							img2_image_set_pixel(accumulator, canvas_x, canvas_y, 0, 0, 0, 0);
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
	img2_image_destroy(accumulator);
	if (previous != NULL) {
		img2_image_destroy(previous);
	}
	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

	*frame_count = num_frames;
	return frames;

cleanup_error:
	/* Cleanup on error */
	img2_image_destroy(accumulator);
	if (previous != NULL) {
		img2_image_destroy(previous);
	}
	for (uint32_t i = 0; i < num_frames; i++) {
		if (frames[i] != NULL) {
			img2_image_destroy(frames[i]);
		}
	}
	free(frames);
	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
	return NULL;
}
#endif /* PNG_APNG_SUPPORTED */

/**
 * @brief Error message buffer for the static PNG decode path
 */
typedef struct {
	char message[64]; /**< Last libpng error message (truncated) */
} png_static_error_ctx;

/**
 * @brief libpng error callback for the static decode path
 *
 * Records the message for the caller's diagnostic and unwinds to the
 * setjmp point in decode_png_static(). Never returns.
 *
 * @param png_ptr PNG read structure
 * @param message libpng error message
 */
static void png_static_error_fn(png_structp png_ptr, png_const_charp message)
{
	png_static_error_ctx *ctx = (png_static_error_ctx *)png_get_error_ptr(png_ptr);

	if (ctx != NULL && message != NULL) {
		snprintf(ctx->message, sizeof(ctx->message), "%s", message);
	}

	png_longjmp(png_ptr, 1);
}

/**
 * @brief libpng warning callback for the static decode path
 *
 * Warnings are ignored, as the simplified png_image API did before.
 *
 * @param png_ptr PNG read structure (unused)
 * @param message libpng warning message (unused)
 */
static void png_static_warning_fn(png_structp png_ptr, png_const_charp message)
{
	(void)png_ptr;
	(void)message;
}

/**
 * @brief Decode static PNG image using the libpng low-level API
 *
 * Rows are decoded straight into the img2_image_t RGBA8888 pixel buffer, with no
 * intermediate buffer or copy. The transform set replicates exactly what the
 * simplified png_image API does for PNG_FORMAT_RGBA (png_image_read_direct in
 * libpng's pngread.c), so the output is byte-identical to it:
 * - Palette, low-bit gray and tRNS expanded to 8-bit RGB(A) (png_set_expand)
 * - Grayscale converted to RGB
 * - Opaque alpha (0xFF) added when there is no alpha channel or tRNS
 * - Gamma: input defaults to sRGB (8-bit) or linear (16-bit) unless the file
 *   says otherwise (gAMA/sRGB/cICP/iCCP), output is sRGB; libpng only does the
 *   conversion when the two differ significantly
 * - 16-bit samples scaled (rounded) to 8 bits
 * - Interlaced PNGs (Adam7)
 *
 * @param data Raw PNG file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (PNG is static)
 * @return Array with single img2_image_t*, or NULL on error
 *
 * @note This function handles only static PNG files
 * @note Output format is always RGBA8888
 */
static img2_image_t **decode_png_static(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_png");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Setup memory reader
	png_mem_reader reader;
	reader.data = data;
	reader.size = len;
	reader.offset = 0;

	png_static_error_ctx error_ctx;
	error_ctx.message[0] = '\0';

	// Create PNG read structures (warnings silenced, errors recorded)
	png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, &error_ctx, png_static_error_fn, png_static_warning_fn);
	if (png_ptr == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create PNG read struct");
		return NULL;
	}

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create PNG info struct");
		png_destroy_read_struct(&png_ptr, NULL, NULL);
		return NULL;
	}

	// Assigned after setjmp and used in the error path: must be volatile
	img2_image_t *volatile img = NULL;
	png_bytep *volatile row_pointers = NULL;

	// Set up error handling
	if (setjmp(png_jmpbuf(png_ptr))) {
		img2_log(IMG2_LOG_ERROR, "Error: libpng failed to decode PNG: %s", error_ctx.message);
		free(row_pointers);
		img2_image_destroy(img);
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return NULL;
	}

	// Same leniency as the simplified API: benign errors are warnings
	png_set_benign_errors(png_ptr, 1);

	// Set custom read function
	png_set_read_fn(png_ptr, &reader, png_read_func);

	// Read PNG header and all chunks before IDAT
	png_read_info(png_ptr, info_ptr);

	uint32_t width = png_get_image_width(png_ptr, info_ptr);
	uint32_t height = png_get_image_height(png_ptr, info_ptr);
	int color_type = png_get_color_type(png_ptr, info_ptr);
	int bit_depth = png_get_bit_depth(png_ptr, info_ptr);
	bool has_alpha = (color_type & PNG_COLOR_MASK_ALPHA) != 0 || png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS) != 0;

	// Transforms to RGBA8888; order and arguments mirror png_image_read_direct()
	png_set_expand(png_ptr);
	if ((color_type & PNG_COLOR_MASK_COLOR) == 0) {
		png_set_gray_to_rgb(png_ptr);
	}
	// Default input gamma when the file has none: linear for 16-bit, else sRGB
	png_set_alpha_mode_fixed(png_ptr, PNG_ALPHA_PNG, bit_depth == 16 ? PNG_GAMMA_LINEAR : PNG_DEFAULT_sRGB);
	if (bit_depth == 16) {
		png_set_scale_16(png_ptr);
	}
	if (!has_alpha) {
		png_set_add_alpha(png_ptr, 0xFF, PNG_FILLER_AFTER);
	}
	// Output gamma: sRGB
	png_set_alpha_mode_fixed(png_ptr, PNG_ALPHA_PNG, PNG_DEFAULT_sRGB);
	png_set_interlace_handling(png_ptr);
	png_read_update_info(png_ptr, info_ptr);

	if (png_get_rowbytes(png_ptr, info_ptr) != (size_t)width * 4) {
		png_error(png_ptr, "unexpected row size after RGBA8888 transforms");
	}

	// Create img2_image_t structure and decode straight into its pixel buffer
	img = img2_image_alloc(width, height);
	if (img == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create image_t structure");
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return NULL;
	}

	row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * height);
	if (row_pointers == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate row pointers");
		img2_image_destroy(img);
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return NULL;
	}

	for (uint32_t y = 0; y < height; y++) {
		row_pointers[y] = img->pixels + (size_t)y * width * 4;
	}

	png_read_image(png_ptr, row_pointers);

	free(row_pointers);
	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

	// Parse EXIF/XMP metadata
#ifdef IMG2_HAVE_EXIF_READER
	// Extract EXIF from eXIf chunk using full libpng API
	img2_exif_t *exif = extract_png_exif(data, len);

	// If no EXIF from eXIf chunk, allocate empty structure for tEXt data
	if (exif == NULL) {
		exif = malloc(sizeof(img2_exif_t));
		if (exif != NULL) {
			img2_exif_init(exif);
		}
	}

	// Extract text metadata from tEXt chunks and populate EXIF fields
	if (exif != NULL) {
		extract_png_text_to_exif(exif, data, len);
	}

	// Extract XMP from iTXt chunks using full libpng API
	img2_xmp_t *xmp = extract_png_xmp(data, len);

	// Store metadata in image structure
	img->exif = exif;
	img->xmp = xmp;
#endif

	// Allocate frames array (single frame for PNG)
	img2_image_t **frames = (img2_image_t **)malloc(sizeof(img2_image_t *));
	if (frames == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate frames array");
		img2_image_destroy(img);
		return NULL;
	}

	frames[0] = img;
	*frame_count = 1;

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
 * @return Array of img2_image_t* frames, or NULL on error
 *
 * @note For static PNG: returns 1 frame
 * @note For APNG: returns N frames (capped at MAX_PNG_FRAMES)
 * @note Output format is always RGBA8888
 */
img2_image_t **img2_decode_png(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_png");
		return NULL;
	}

	/* Initialize output */
	*frame_count = 0;

	/* Check if animated */
#ifdef PNG_APNG_SUPPORTED
	if (img2_png_is_animated(data, len)) {
		return decode_png_animated(data, len, frame_count);
	}
#endif /* PNG_APNG_SUPPORTED */

	return decode_png_static(data, len, frame_count);
}
