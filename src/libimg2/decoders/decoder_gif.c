/**
 * @file decoder_gif.c
 * @brief GIF decoder implementation using giflib
 *
 * Decodes GIF images (both static and animated) to RGBA8888 format.
 * Handles transparency, disposal methods, and frame composition.
 */

/* clang-format off */
#include <limits.h>
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
 * @brief Track the NETSCAPE2.0 loop count over the extension blocks of a record walk
 *
 * Same rule as scanning DGifSlurp's per-image extension block lists in file order: a block
 * (Function APPLICATION_EXT_FUNC_CODE, >= 11 bytes, "NETSCAPE2.0") directly followed, in the same
 * list, by a block with Function CONTINUE_EXT_FUNC_CODE, >= 3 bytes and Bytes[0] == 1; the first
 * match in file order wins. A list ends at each image descriptor (the caller resets *prev_app).
 *
 * @param function Block function (extension code for the first block, CONTINUE_EXT_FUNC_CODE after)
 * @param ext giflib extension buffer (ext[0] = byte count, data from ext[1])
 * @param prev_app In/out: the previous block of the current list was a NETSCAPE2.0 application block
 * @param loop_out In/out: -1 until found, then the 16-bit loop count (0 = forever)
 */
static void gif_track_netscape_loop(int function, const GifByteType *ext, bool *prev_app, int *loop_out)
{
	int count = ext[0];
	const GifByteType *bytes = &ext[1];
	if (*prev_app && *loop_out < 0 && function == CONTINUE_EXT_FUNC_CODE && count >= 3 && bytes[0] == 1) {
		*loop_out = (int)bytes[1] | ((int)bytes[2] << 8);
	}
	*prev_app = function == APPLICATION_EXT_FUNC_CODE && count >= 11 && memcmp(bytes, "NETSCAPE2.0", 11) == 0;
}

/**
 * @brief Walk the GIF records from a fresh memory reader without LZW-decoding any raster data [F16]
 *
 * Reads the logical screen, then record by record: image descriptors (DGifGetImageDesc) with their
 * raster sub-blocks skipped via DGifGetCode/DGifGetCodeNext, extensions via
 * DGifGetExtension/DGifGetExtensionNext, until the trailer.
 *
 * @param data Raw GIF file data
 * @param len Length of data in bytes
 * @param stop_after Stop as soon as this many image descriptors were seen (0 = walk to the trailer)
 * @param screen_width Output: logical screen width (may be NULL)
 * @param screen_height Output: logical screen height (may be NULL)
 * @param image_count Output: number of image descriptors seen (also valid after a walk error)
 * @param loop_out Output: NETSCAPE2.0 loop count of the records walked, -1 if none (may be NULL)
 * @param limit_images Check the raster size of the first limit_images image descriptors (0 = no checks)
 * @param error_code Output: giflib error code on failure
 * @return 0 on success (also when stopped early), -1 if DGifOpen failed, -2 on a giflib error while
 *         walking (a walk to the trailer without any image is an error: D_GIF_ERR_NO_IMAG_DSCR, as in DGifSlurp),
 *         -3 (logged) if a checked descriptor is over IMG2_MAX_DIMENSION / IMG2_MAX_PIXELS or the checked
 *         descriptors together are over IMG2_MAX_TOTAL_PIXELS [F16]
 */
static int gif_walk_records(const uint8_t *data, size_t len, int stop_after, uint32_t *screen_width, uint32_t *screen_height, int *image_count, int *loop_out, int limit_images, int *error_code)
{
	gif_mem_reader reader;
	reader.data = data;
	reader.size = len;
	reader.offset = 0;

	*image_count = 0;
	if (loop_out != NULL) {
		*loop_out = -1;
	}
	GifFileType *gif = DGifOpen(&reader, gif_read_func, error_code);
	if (gif == NULL) {
		return -1;
	}
	if (screen_width != NULL) {
		*screen_width = (uint32_t)gif->SWidth;
	}
	if (screen_height != NULL) {
		*screen_height = (uint32_t)gif->SHeight;
	}

	bool ok = true;
	bool over_limit = false;
	bool prev_app = false;
	uint64_t raster_pixels = 0; /* [F16] pixels of the checked descriptors' rasters (gif_slurp_capped allocates them) */
	GifRecordType record_type = UNDEFINED_RECORD_TYPE;
	do {
		if (DGifGetRecordType(gif, &record_type) == GIF_ERROR) {
			ok = false;
			break;
		}

		if (record_type == IMAGE_DESC_RECORD_TYPE) {
			int code_size;
			GifByteType *block = NULL;
			if (DGifGetImageDesc(gif) == GIF_ERROR) {
				ok = false;
				break;
			}
			(*image_count)++;
			prev_app = false; /* the extension block list of this image ends here */
			/* [F16] the raster of a descriptor is allocated at its own size (not clipped to the screen): check it
			 * before the slurp allocates; a size <= 0 is left to the slurp's own check (CODEC, as giflib) */
			if (*image_count <= limit_images && (img2_decode_check_header_size("GIF image", gif->Image.Width, gif->Image.Height) != IMG2_OK || img2_decode_add_frame_pixels(&raster_pixels, (uint32_t)gif->Image.Width, (uint32_t)gif->Image.Height) != IMG2_OK)) {
				over_limit = true;
				break;
			}
			if (stop_after > 0 && *image_count >= stop_after) {
				break;
			}
			/* Skip the raster sub-blocks as they are (no LZW decoding) */
			if (DGifGetCode(gif, &code_size, &block) == GIF_ERROR) {
				ok = false;
				break;
			}
			while (block != NULL) {
				if (DGifGetCodeNext(gif, &block) == GIF_ERROR) {
					ok = false;
					break;
				}
			}
		} else if (record_type == EXTENSION_RECORD_TYPE) {
			int ext_code;
			GifByteType *ext = NULL;
			if (DGifGetExtension(gif, &ext_code, &ext) == GIF_ERROR) {
				ok = false;
				break;
			}
			int function = ext_code;
			while (ext != NULL) {
				if (loop_out != NULL) {
					gif_track_netscape_loop(function, ext, &prev_app, loop_out);
				}
				function = CONTINUE_EXT_FUNC_CODE;
				if (DGifGetExtensionNext(gif, &ext) == GIF_ERROR) {
					ok = false;
					break;
				}
			}
		}
	} while (ok && record_type != TERMINATE_RECORD_TYPE);

	if (!ok) {
		*error_code = gif->Error;
	} else if (*image_count == 0) { /* (over_limit implies an image) */
		*error_code = D_GIF_ERR_NO_IMAG_DSCR;
		ok = false;
	}

	int close_error;
	DGifCloseFile(gif, &close_error);
	return over_limit ? -3 : (ok ? 0 : -2);
}

/**
 * @brief DGifSlurp that stops after max_images complete images [F16]
 *
 * Same record loop as giflib's DGifSlurp (giflib 6.1): image descriptors get their raster
 * LZW-decoded into SavedImages[i].RasterBits (four passes for interlaced images), extension
 * blocks are collected with GifAddExtensionBlock and handed to the following image, so
 * DGifSavedExtensionToGCB works unchanged. Once max_images images are stored it returns
 * without reading any further record, so images beyond the cap are never decoded (or read).
 * On error the partially filled image stays in SavedImages; DGifCloseFile frees it.
 * Two deviations from DGifSlurp: an extension without data sub-blocks is complete after DGifGetExtension
 * (DGifSlurp reads one more block length and swallows the next record), and gif->Error is set on every
 * error return (DGifSlurp leaves it 0 for bad image dimensions and a failed raster allocation).
 *
 * @param gif Opened giflib handle
 * @param max_images Stop after this many images (> 0)
 * @return GIF_OK or GIF_ERROR (gif->Error set where giflib sets it)
 */
static int gif_slurp_capped(GifFileType *gif, int max_images)
{
	GifRecordType record_type;

	gif->ExtensionBlocks = NULL;
	gif->ExtensionBlockCount = 0;

	do {
		if (DGifGetRecordType(gif, &record_type) == GIF_ERROR) {
			return GIF_ERROR;
		}

		if (record_type == IMAGE_DESC_RECORD_TYPE) {
			if (DGifGetImageDesc(gif) == GIF_ERROR) {
				return GIF_ERROR;
			}
			SavedImage *sp = &gif->SavedImages[gif->ImageCount - 1];
			int width = sp->ImageDesc.Width;
			int height = sp->ImageDesc.Height;
			/* width * height <= IMG2_MAX_PIXELS here: gif_walk_records() checked the first MAX_GIF_FRAMES descriptors [F16] */
			if (width <= 0 || height <= 0 || width > (INT_MAX / height)) {
				gif->Error = D_GIF_ERR_DATA_TOO_BIG; /* giflib leaves Error unset here (GifErrorString(0) is NULL) */
				return GIF_ERROR;
			}
			sp->RasterBits = (GifByteType *)malloc((size_t)width * (size_t)height * sizeof(GifPixelType));
			if (sp->RasterBits == NULL) {
				gif->Error = D_GIF_ERR_NOT_ENOUGH_MEM;
				return GIF_ERROR;
			}
			if (sp->ImageDesc.Interlace) {
				const int interlaced_offset[] = { 0, 4, 2, 1 };
				const int interlaced_jumps[] = { 8, 8, 4, 2 };
				for (int pass = 0; pass < 4; pass++) {
					for (int row = interlaced_offset[pass]; row < height; row += interlaced_jumps[pass]) {
						if (DGifGetLine(gif, sp->RasterBits + (size_t)row * (size_t)width, width) == GIF_ERROR) {
							return GIF_ERROR;
						}
					}
				}
			} else if (DGifGetLine(gif, sp->RasterBits, width * height) == GIF_ERROR) {
				return GIF_ERROR;
			}
			if (gif->ExtensionBlocks != NULL) {
				sp->ExtensionBlocks = gif->ExtensionBlocks;
				sp->ExtensionBlockCount = gif->ExtensionBlockCount;
				gif->ExtensionBlocks = NULL;
				gif->ExtensionBlockCount = 0;
			}
			if (gif->ImageCount >= max_images) {
				return GIF_OK; /* frame cap reached: the rest of the file is not read */
			}
		} else if (record_type == EXTENSION_RECORD_TYPE) {
			int ext_function;
			GifByteType *ext_data = NULL;
			if (DGifGetExtension(gif, &ext_function, &ext_data) == GIF_ERROR) {
				return GIF_ERROR;
			}
			/* An extension without data sub-blocks has already been read up to its terminator (ext_data NULL):
			 * unlike DGifSlurp, do not read another block length (it would consume the next record) */
			int function = ext_function;
			while (ext_data != NULL) {
				if (GifAddExtensionBlock(&gif->ExtensionBlockCount, &gif->ExtensionBlocks, function, ext_data[0], &ext_data[1]) == GIF_ERROR) {
					return GIF_ERROR;
				}
				function = CONTINUE_EXT_FUNC_CODE;
				if (DGifGetExtensionNext(gif, &ext_data) == GIF_ERROR) {
					return GIF_ERROR;
				}
			}
		}
	} while (record_type != TERMINATE_RECORD_TYPE);

	if (gif->ImageCount == 0) {
		gif->Error = D_GIF_ERR_NO_IMAG_DSCR;
		return GIF_ERROR;
	}
	return GIF_OK;
}

/**
 * @brief Decode animated GIF with all frames
 *
 * Decodes all frames of an animated GIF with proper frame composition.
 * Handles disposal methods (DISPOSE_DO_NOT, DISPOSE_BACKGROUND, DISPOSE_PREVIOUS).
 * Multi-frame GIFs get per-frame delay_ms (GCB DelayTime * 10) and a loop count;
 * single-frame GIFs report delay_ms = 0 and loop_count = 0.
 *
 * @param data Raw GIF file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded
 * @param loop_count Output: loop count (0 = forever / not animated)
 * @return Array of img2_image_t* frames, or NULL on error
 *
 * @note Maximum MAX_GIF_FRAMES frames (200) to prevent DoS
 * @note Implements proper GIF frame composition with disposal methods
 * @note Output format is RGBA8888
 */
static img2_image_t **decode_gif_animated_frames(const uint8_t *data, size_t len, int *frame_count, int *loop_count, img2_status_t *st)
{
	if (data == NULL || len == 0 || frame_count == NULL || loop_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_gif_animated");
		return img2_decode_fail(st, IMG2_E_ARG);
	}

	// Initialize output
	*frame_count = 0;
	*loop_count = 0;

	// [F16] count and validate before any raster is LZW-decoded
	int error_code;
	int total_images = 0; /* image descriptors in the file (walk) */
	int walk_loop = -1;   /* NETSCAPE2.0 loop count over the whole walk, -1 = none */
	{
		uint32_t screen_width = 0;
		uint32_t screen_height = 0;
		int walk = gif_walk_records(data, len, 0, &screen_width, &screen_height, &total_images, &walk_loop, MAX_GIF_FRAMES, &error_code);
		if (walk == -1) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to open GIF: %s", GifErrorString(error_code));
			return img2_decode_fail(st, IMG2_E_CODEC);
		}
		if (walk == -3) {
			return img2_decode_fail(st, IMG2_E_LIMIT); /* [F16] a decoded raster would be over the limits */
		}
		/* A defect after the first MAX_GIF_FRAMES + 1 image descriptors is beyond the frame cap:
		 * the capped decode below never reads that far, so it is not an error */
		if (walk != 0 && total_images <= MAX_GIF_FRAMES) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to read GIF structure: %s", GifErrorString(error_code));
			return img2_decode_fail(st, IMG2_E_CODEC);
		}
		int walk_frames = total_images;
		if (walk_frames > MAX_GIF_FRAMES) {
			walk_frames = MAX_GIF_FRAMES; // capped below with a warning, as before
		}
		if (walk_frames > 1 && img2_decode_check_total_pixels((uint64_t)walk_frames, screen_width, screen_height) != IMG2_OK) {
			return img2_decode_fail(st, IMG2_E_LIMIT);
		}
		size_t canvas_bytes;
		if (!img2_image_calc_size(screen_width, screen_height, &canvas_bytes)) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to create accumulator canvas");
			return img2_decode_fail(st, IMG2_E_LIMIT);
		}
	}

	// Setup memory reader
	gif_mem_reader reader;
	reader.data = data;
	reader.size = len;
	reader.offset = 0;

	// Open GIF file from memory
	GifFileType *gif = DGifOpen(&reader, gif_read_func, &error_code);
	if (gif == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to open GIF: %s", GifErrorString(error_code));
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Read GIF structure, decoding at most MAX_GIF_FRAMES images [F16]
	if (gif_slurp_capped(gif, MAX_GIF_FRAMES) != GIF_OK) {
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

	if (total_images > MAX_GIF_FRAMES) {
		img2_log(IMG2_LOG_WARN, "Warning: GIF has %d frames, limiting to %d", total_images, MAX_GIF_FRAMES);
	}
	if (num_frames > MAX_GIF_FRAMES) {
		num_frames = MAX_GIF_FRAMES; /* not reached: gif_slurp_capped stops at the cap */
	}

	// Get canvas dimensions (screen width/height)
	uint32_t canvas_width = gif->SWidth;
	uint32_t canvas_height = gif->SHeight;

	// [F14] every output frame is a full canvas: reject before allocating anything
	if (num_frames > 1 && img2_decode_check_total_pixels((uint64_t)num_frames, canvas_width, canvas_height) != IMG2_OK) {
		DGifCloseFile(gif, &error_code);
		return img2_decode_fail(st, IMG2_E_LIMIT);
	}

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
	img2_image_t *accumulator = NULL;
	img2_status_t alloc_st = img2_decode_image_alloc(canvas_width, canvas_height, &accumulator);
	if (alloc_st != IMG2_OK) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create accumulator canvas");
		free(frames);
		DGifCloseFile(gif, &error_code);
		return img2_decode_fail(st, alloc_st);
	}

	// Previous frame backup for DISPOSE_PREVIOUS
	img2_image_t *previous = NULL;

	// [F14] running pixel total of the output frames
	uint64_t total_pixels = 0;

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
		uint32_t delay_ms = 0;

		if (DGifSavedExtensionToGCB(gif, frame_idx, &gcb) == GIF_OK) {
			disposal_mode = gcb.DisposalMode;
			if (gcb.DelayTime > 0) {
				delay_ms = (uint32_t)gcb.DelayTime * 10u;
			}
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
		if (img2_decode_add_frame_pixels(&total_pixels, canvas_width, canvas_height) != IMG2_OK) {
			*st = IMG2_E_LIMIT;
			goto cleanup_error;
		}
		frames[frame_idx] = img2_image_alloc(canvas_width, canvas_height);
		if (frames[frame_idx] == NULL) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to create output frame %d", frame_idx);
			*st = IMG2_E_NOMEM;
			goto cleanup_error;
		}

		memcpy(frames[frame_idx]->pixels, accumulator->pixels, canvas_width * canvas_height * 4);
		if (num_frames > 1) {
			frames[frame_idx]->delay_ms = delay_ms;
		}

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
	if (num_frames > 1) {
		/* NETSCAPE2.0 from the whole file (also past the frame cap), as with DGifSlurp; 1 without it */
		*loop_count = (walk_loop >= 0) ? walk_loop : 1;
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
 * Walks the GIF records until the second image descriptor without
 * decoding any raster data (gif_walk_records). Useful for determining
 * whether to use native protocols or fallback rendering.
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

	/* Record walk only: raster data is skipped, not LZW-decoded; stop at the 2nd image descriptor */
	int error_code = 0;
	int image_count = 0;
	if (gif_walk_records(data, size, 2, NULL, NULL, &image_count, NULL, 0, &error_code) != 0) {
		return false;
	}

	return image_count > 1;
}

img2_status_t img2_decode_gif_animated(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out)
{
	(void)opts;
	if (out == NULL) {
		return IMG2_E_ARG;
	}
	img2_status_t st = IMG2_E_INTERNAL;
	int count = 0;
	int loop_count = 0;
	img2_image_t **frames = decode_gif_animated_frames(data, len, &count, &loop_count, &st);
	st = img2_frames_adopt(out, frames, count, st);
	if (st == IMG2_OK) {
		out->loop_count = loop_count;
	}
	return st;
}
