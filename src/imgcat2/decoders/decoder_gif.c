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
static void gif_index_to_rgba(const ColorMapObject *color_map, int index, int transparent_color, uint8_t *out_rgba)
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
 * @brief Decode GIF image (first frame only)
 *
 * Decodes only the first frame of a GIF image (static GIF).
 * For animated GIFs, use decode_gif_animated().
 *
 * @param data Raw GIF file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (first frame only)
 * @return Array with single image_t*, or NULL on error
 *
 * @note Returns only the first frame, even if GIF is animated
 * @note Output format is RGBA8888
 */
image_t **decode_gif(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_gif\n");
		return NULL;
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
		fprintf(stderr, "Error: Failed to open GIF: %s\n", GifErrorString(error_code));
		return NULL;
	}

	// Read GIF structure
	if (DGifSlurp(gif) != GIF_OK) {
		fprintf(stderr, "Error: Failed to read GIF structure: %s\n", GifErrorString(gif->Error));
		DGifCloseFile(gif, &error_code);
		return NULL;
	}

	// Check if GIF has at least one frame
	if (gif->ImageCount == 0) {
		fprintf(stderr, "Error: GIF has no images\n");
		DGifCloseFile(gif, &error_code);
		return NULL;
	}

	// Get first frame
	SavedImage *saved_image = &gif->SavedImages[0];

	// Get color map (use local if available, otherwise global)
	ColorMapObject *color_map = saved_image->ImageDesc.ColorMap;
	if (color_map == NULL) {
		color_map = gif->SColorMap;
	}

	if (color_map == NULL) {
		fprintf(stderr, "Error: GIF has no color map\n");
		DGifCloseFile(gif, &error_code);
		return NULL;
	}

	// Get transparent color index (if any)
	int transparent_color = -1;
	GraphicsControlBlock gcb;
	if (DGifSavedExtensionToGCB(gif, 0, &gcb) == GIF_OK) {
		if (gcb.TransparentColor != NO_TRANSPARENT_COLOR) {
			transparent_color = gcb.TransparentColor;
		}
	}

	// Get image dimensions
	uint32_t width = saved_image->ImageDesc.Width;
	uint32_t height = saved_image->ImageDesc.Height;

	// Create image_t structure
	image_t *img = image_create(width, height);
	if (img == NULL) {
		fprintf(stderr, "Error: Failed to create image_t structure\n");
		DGifCloseFile(gif, &error_code);
		return NULL;
	}

	// Decode indexed pixels to RGBA
	GifByteType *raster = saved_image->RasterBits;
	for (uint32_t y = 0; y < height; y++) {
		for (uint32_t x = 0; x < width; x++) {
			int index = raster[y * width + x];
			uint8_t rgba[4];
			gif_index_to_rgba(color_map, index, transparent_color, rgba);
			image_set_pixel(img, x, y, rgba[0], rgba[1], rgba[2], rgba[3]);
		}
	}

	// Cleanup
	DGifCloseFile(gif, &error_code);

	// Allocate frames array (single frame)
	image_t **frames = (image_t **)malloc(sizeof(image_t *));
	if (frames == NULL) {
		fprintf(stderr, "Error: Failed to allocate frames array\n");
		image_destroy(img);
		return NULL;
	}

	frames[0] = img;
	*frame_count = 1;

	fprintf(stderr, "GIF decoded (first frame): %ux%u\n", width, height);

	return frames;
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
static void frame_composition(
    image_t *accumulator, const GifByteType *raster, const ColorMapObject *color_map, int transparent_color, uint32_t img_left, uint32_t img_top, uint32_t img_width, uint32_t img_height,
    uint32_t canvas_width, uint32_t canvas_height
)
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
					image_set_pixel(accumulator, canvas_x, canvas_y, rgba[0], rgba[1], rgba[2], rgba[3]);
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
 * @return Array of image_t* frames, or NULL on error
 *
 * @note Maximum MAX_GIF_FRAMES frames (200) to prevent DoS
 * @note Implements proper GIF frame composition with disposal methods
 * @note Output format is RGBA8888
 */
image_t **decode_gif_animated(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_gif_animated\n");
		return NULL;
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
		fprintf(stderr, "Error: Failed to open GIF: %s\n", GifErrorString(error_code));
		return NULL;
	}

	// Read GIF structure
	if (DGifSlurp(gif) != GIF_OK) {
		fprintf(stderr, "Error: Failed to read GIF structure: %s\n", GifErrorString(gif->Error));
		DGifCloseFile(gif, &error_code);
		return NULL;
	}

	// Check frame count
	int num_frames = gif->ImageCount;
	if (num_frames == 0) {
		fprintf(stderr, "Error: GIF has no images\n");
		DGifCloseFile(gif, &error_code);
		return NULL;
	}

	if (num_frames > MAX_GIF_FRAMES) {
		fprintf(stderr, "Warning: GIF has %d frames, limiting to %d\n", num_frames, MAX_GIF_FRAMES);
		num_frames = MAX_GIF_FRAMES;
	}

	// Get canvas dimensions (screen width/height)
	uint32_t canvas_width = gif->SWidth;
	uint32_t canvas_height = gif->SHeight;

	// Allocate frames array
	image_t **frames = (image_t **)malloc(sizeof(image_t *) * num_frames);
	if (frames == NULL) {
		fprintf(stderr, "Error: Failed to allocate frames array\n");
		DGifCloseFile(gif, &error_code);
		return NULL;
	}

	// Initialize frames to NULL for cleanup
	for (int i = 0; i < num_frames; i++) {
		frames[i] = NULL;
	}

	// Create accumulator canvas for frame composition
	image_t *accumulator = image_create(canvas_width, canvas_height);
	if (accumulator == NULL) {
		fprintf(stderr, "Error: Failed to create accumulator canvas\n");
		free(frames);
		DGifCloseFile(gif, &error_code);
		return NULL;
	}

	// Previous frame backup for DISPOSE_PREVIOUS
	image_t *previous = NULL;

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
			fprintf(stderr, "Error: Frame %d has no color map\n", frame_idx);
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
			previous = image_create(canvas_width, canvas_height);
			if (previous == NULL) {
				fprintf(stderr, "Error: Failed to create previous frame backup\n");
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
		frames[frame_idx] = image_create(canvas_width, canvas_height);
		if (frames[frame_idx] == NULL) {
			fprintf(stderr, "Error: Failed to create output frame %d\n", frame_idx);
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
							image_set_pixel(accumulator, canvas_x, canvas_y, 0, 0, 0, 0);
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
	image_destroy(accumulator);
	if (previous != NULL) {
		image_destroy(previous);
	}
	DGifCloseFile(gif, &error_code);

	*frame_count = num_frames;
	fprintf(stderr, "Animated GIF decoded: %d frames, %ux%u\n", num_frames, canvas_width, canvas_height);

	return frames;

cleanup_error:
	// Cleanup on error
	image_destroy(accumulator);
	if (previous != NULL) {
		image_destroy(previous);
	}
	for (int i = 0; i < num_frames; i++) {
		if (frames[i] != NULL) {
			image_destroy(frames[i]);
		}
	}
	free(frames);
	DGifCloseFile(gif, &error_code);
	return NULL;
}
