/**
 * @file decoder_gif.c
 * @brief GIF decoder implementation using giflib
 *
 * Decodes GIF images (both static and animated) to RGBA8888 format.
 * Handles transparency, disposal methods, and frame composition.
 */

/* clang-format off */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gif_lib.h>
/* clang-format on */

#include "decoder.h"
#include "../core/log.h"

/** Maximum number of GIF frames to decode (prevents DoS) */
#define MAX_GIF_FRAMES 200

/**
 * @struct gif_mem_reader
 * @brief Memory reader context for giflib
 *
 * giflib requires a custom input function to read from memory buffer.
 */
typedef struct {
	const uint8_t *data; /**< Pointer to GIF file data */
	size_t size; /**< Total size of data */
	size_t offset; /**< Current read offset */
} gif_mem_reader;

/**
 * @brief Custom read function for giflib (memory source)
 *
 * Called by giflib to read data from memory buffer instead of FILE*.
 *
 * @param gif GifFileType pointer
 * @param buf Output buffer
 * @param len Number of bytes to read
 * @return Number of bytes read, or 0 on EOF/error
 */
static int gif_read_func(GifFileType *gif, GifByteType *buf, int len)
{
	gif_mem_reader *reader = (gif_mem_reader *)gif->UserData;

	if (reader == NULL || reader->offset >= reader->size) {
		return 0; // EOF
	}

	size_t available = reader->size - reader->offset;
	size_t to_read = (size_t)len < available ? (size_t)len : available;

	memcpy(buf, reader->data + reader->offset, to_read);
	reader->offset += to_read;

	return (int)to_read;
}

/**
 * @brief Convert GIF indexed pixel to RGBA
 *
 * Looks up pixel color in color map and converts to RGBA format.
 * Handles transparency based on transparent_color index.
 *
 * @param color_map GIF color map
 * @param index Pixel index in color map
 * @param transparent_color Transparent color index (-1 if none)
 * @param out_rgba Output RGBA pixel [4 bytes]
 */
static inline void gif_index_to_rgba(const ColorMapObject *color_map, int index, int transparent_color, uint8_t *out_rgba)
{
	if (color_map == NULL || index < 0 || index >= color_map->ColorCount) {
		// Invalid index, use black
		out_rgba[0] = 0;
		out_rgba[1] = 0;
		out_rgba[2] = 0;
		out_rgba[3] = 0;
		return;
	}

	GifColorType color = color_map->Colors[index];
	out_rgba[0] = color.Red;
	out_rgba[1] = color.Green;
	out_rgba[2] = color.Blue;
	out_rgba[3] = (index == transparent_color) ? 0 : 255; // Transparent or opaque
}

/**
 * @brief Compose GIF frame onto accumulator canvas
 *
 * Composites a single GIF frame onto the accumulator canvas.
 * Handles transparency: pixels with alpha >= 128 overwrite accumulator,
 * transparent pixels (alpha < 128) leave accumulator unchanged.
 *
 * @param accumulator Accumulator canvas (modified in-place)
 * @param raster Frame raster data (indexed pixels)
 * @param color_map Color map for frame
 * @param transparent_color Transparent color index (-1 if none)
 * @param img_left Frame left offset on canvas
 * @param img_top Frame top offset on canvas
 * @param img_width Frame width
 * @param img_height Frame height
 * @param canvas_width Canvas width (for bounds checking)
 * @param canvas_height Canvas height (for bounds checking)
 */
static void frame_composition(img2_image_t *accumulator, const GifByteType *raster, const ColorMapObject *color_map, int transparent_color, uint32_t img_left, uint32_t img_top, uint32_t img_width, uint32_t img_height, uint32_t canvas_width, uint32_t canvas_height)
{
	if (accumulator == NULL || raster == NULL || color_map == NULL) {
		return;
	}

	for (uint32_t y = 0; y < img_height; y++) {
		for (uint32_t x = 0; x < img_width; x++) {
			int index = raster[y * img_width + x];

			// Skip transparent pixels (don't overwrite accumulator)
			if (index == transparent_color) {
				continue;
			}

			uint8_t rgba[4];
			gif_index_to_rgba(color_map, index, transparent_color, rgba);

			// Only composite if alpha >= 128 (opaque enough)
			if (rgba[3] >= 128) {
				// Composite onto accumulator (bounds check)
				uint32_t canvas_x = img_left + x;
				uint32_t canvas_y = img_top + y;
				if (canvas_x < canvas_width && canvas_y < canvas_height) {
					img2_image_set_pixel(accumulator, canvas_x, canvas_y, rgba[0], rgba[1], rgba[2], rgba[3]);
				}
			}
		}
	}
}

/**
 * @brief Decode animated GIF with all frames
 *
 * Decodes all frames of an animated GIF with proper frame composition.
 * Handles disposal methods (DISPOSE_DO_NOT, DISPOSE_BACKGROUND, DISPOSE_PREVIOUS).
 *
 * @param data Raw GIF file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded
 * @return Array of img2_image_t* frames, or NULL on error
 *
 * @note Maximum MAX_GIF_FRAMES frames (200) to prevent DoS
 * @note Implements proper GIF frame composition with disposal methods
 * @note Output format is RGBA8888
 */
static img2_image_t **decode_gif_animated_frames(const uint8_t *data, size_t len, int *frame_count, img2_status_t *st)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_gif_animated");
		return img2_decode_fail(st, IMG2_E_ARG);
	}

	// Initialize output
	*frame_count = 0;

	// Setup memory reader
	gif_mem_reader reader;
	reader.data = data;
	reader.size = len;
	reader.offset = 0;

	// Open GIF file from memory
	int error_code;
	GifFileType *gif = DGifOpen(&reader, gif_read_func, &error_code);
	if (gif == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to open GIF: %s", GifErrorString(error_code));
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Read GIF structure
	if (DGifSlurp(gif) != GIF_OK) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to read GIF structure: %s", GifErrorString(gif->Error));
		DGifCloseFile(gif, &error_code);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Check frame count
	int num_frames = gif->ImageCount;
	if (num_frames == 0) {
		img2_log(IMG2_LOG_ERROR, "Error: GIF has no images");
		DGifCloseFile(gif, &error_code);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	if (num_frames > MAX_GIF_FRAMES) {
		img2_log(IMG2_LOG_WARN, "Warning: GIF has %d frames, limiting to %d", num_frames, MAX_GIF_FRAMES);
		num_frames = MAX_GIF_FRAMES;
	}

	// Get canvas dimensions (screen width/height)
	uint32_t canvas_width = gif->SWidth;
	uint32_t canvas_height = gif->SHeight;

	// Allocate frames array
	img2_image_t **frames = (img2_image_t **)malloc(sizeof(img2_image_t *) * num_frames);
	if (frames == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate frames array");
		DGifCloseFile(gif, &error_code);
		return img2_decode_fail(st, IMG2_E_NOMEM);
	}

	// Initialize frames to NULL for cleanup
	for (int i = 0; i < num_frames; i++) {
		frames[i] = NULL;
	}

	// Create accumulator canvas for frame composition
	img2_image_t *accumulator = img2_image_alloc(canvas_width, canvas_height);
	if (accumulator == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create accumulator canvas");
		free(frames);
		DGifCloseFile(gif, &error_code);
		return img2_decode_fail(st, IMG2_E_NOMEM);
	}

	// Previous frame backup for DISPOSE_PREVIOUS
	img2_image_t *previous = NULL;

	// Decode each frame
	for (int frame_idx = 0; frame_idx < num_frames; frame_idx++) {
		SavedImage *saved_image = &gif->SavedImages[frame_idx];
		GifImageDesc *desc = &saved_image->ImageDesc;

		// Get color map
		ColorMapObject *color_map = desc->ColorMap;
		if (color_map == NULL) {
			color_map = gif->SColorMap;
		}

		if (color_map == NULL) {
			img2_log(IMG2_LOG_ERROR, "Error: Frame %d has no color map", frame_idx);
			*st = IMG2_E_CODEC;
			goto cleanup_error;
		}

		// Get graphics control block (disposal method, transparency)
		GraphicsControlBlock gcb;
		int transparent_color = -1;
		int disposal_mode = DISPOSE_DO_NOT;

		if (DGifSavedExtensionToGCB(gif, frame_idx, &gcb) == GIF_OK) {
			disposal_mode = gcb.DisposalMode;
			if (gcb.TransparentColor != NO_TRANSPARENT_COLOR) {
				transparent_color = gcb.TransparentColor;
			}
		}

		// Backup accumulator for DISPOSE_PREVIOUS
		if (disposal_mode == DISPOSE_PREVIOUS && previous == NULL) {
			previous = img2_image_alloc(canvas_width, canvas_height);
			if (previous == NULL) {
				img2_log(IMG2_LOG_ERROR, "Error: Failed to create previous frame backup");
				*st = IMG2_E_NOMEM;
				goto cleanup_error;
			}
		}

		if (disposal_mode == DISPOSE_PREVIOUS && previous != NULL) {
			memcpy(previous->pixels, accumulator->pixels, canvas_width * canvas_height * 4);
		}

		// Composite current frame onto accumulator
		GifByteType *raster = saved_image->RasterBits;
		uint32_t img_left = desc->Left;
		uint32_t img_top = desc->Top;
		uint32_t img_width = desc->Width;
		uint32_t img_height = desc->Height;

		frame_composition(accumulator, raster, color_map, transparent_color, img_left, img_top, img_width, img_height, canvas_width, canvas_height);

		// Copy composed frame to output
		frames[frame_idx] = img2_image_alloc(canvas_width, canvas_height);
		if (frames[frame_idx] == NULL) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to create output frame %d", frame_idx);
			*st = IMG2_E_NOMEM;
			goto cleanup_error;
		}

		memcpy(frames[frame_idx]->pixels, accumulator->pixels, canvas_width * canvas_height * 4);

		// Apply disposal method for next frame
		switch (disposal_mode) {
			case DISPOSE_DO_NOT:
				// Keep current frame as-is for next frame
				break;

			case DISPOSE_BACKGROUND:
				// Clear frame area to background (transparent black)
				for (uint32_t y = 0; y < img_height; y++) {
					for (uint32_t x = 0; x < img_width; x++) {
						uint32_t canvas_x = img_left + x;
						uint32_t canvas_y = img_top + y;
						if (canvas_x < canvas_width && canvas_y < canvas_height) {
							img2_image_set_pixel(accumulator, canvas_x, canvas_y, 0, 0, 0, 0);
						}
					}
				}
				break;

			case DISPOSE_PREVIOUS:
				// Restore to previous frame
				if (previous != NULL) {
					memcpy(accumulator->pixels, previous->pixels, canvas_width * canvas_height * 4);
				}
				break;

			default:
				// Unknown disposal mode, treat as DISPOSE_DO_NOT
				break;
		}
	}

	// Cleanup
	img2_image_destroy(accumulator);
	if (previous != NULL) {
		img2_image_destroy(previous);
	}
	DGifCloseFile(gif, &error_code);

	*frame_count = num_frames;
	// fprintf(stderr, "Animated GIF decoded: %d frames, %ux%u\n", num_frames, canvas_width, canvas_height);

	return frames;

cleanup_error:
	// Cleanup on error
	img2_image_destroy(accumulator);
	if (previous != NULL) {
		img2_image_destroy(previous);
	}
	for (int i = 0; i < num_frames; i++) {
		if (frames[i] != NULL) {
			img2_image_destroy(frames[i]);
		}
	}
	free(frames);
	DGifCloseFile(gif, &error_code);
	return NULL; /* *st set at the goto */
}

/**
 * @brief Check if GIF is animated (has multiple frames)
 *
 * Quickly checks if a GIF file contains multiple frames without
 * fully decoding the image data. Useful for determining whether
 * to use native protocols or fallback rendering.
 *
 * @param data Raw GIF file data
 * @param size Size of data in bytes
 * @return true if GIF has more than one frame, false otherwise or on error
 *
 * @note Returns false if data is invalid or not a GIF
 * @note Does not validate that frames are decodable, only counts them
 */
bool img2_gif_is_animated(const uint8_t *data, size_t size)
{
	if (data == NULL || size == 0) {
		return false;
	}

	/* Setup memory reader */
	gif_mem_reader reader;
	reader.data = data;
	reader.size = size;
	reader.offset = 0;

	/* Open GIF */
	int error_code = 0;
	GifFileType *gif = DGifOpen(&reader, gif_read_func, &error_code);
	if (gif == NULL) {
		return false;
	}

	/* Read GIF metadata (does not decode pixel data) */
	if (DGifSlurp(gif) == GIF_ERROR) {
		DGifCloseFile(gif, &error_code);
		return false;
	}

	/* Check if animated (more than one frame) */
	bool is_animated = (gif->ImageCount > 1);

	/* Cleanup */
	DGifCloseFile(gif, &error_code);

	return is_animated;
}

img2_status_t img2_decode_gif_animated(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out)
{
	(void)opts;
	if (out == NULL) {
		return IMG2_E_ARG;
	}
	img2_status_t st = IMG2_E_INTERNAL;
	int count = 0;
	img2_image_t **frames = decode_gif_animated_frames(data, len, &count, &st);
	return img2_frames_adopt(out, frames, count, st);
}
