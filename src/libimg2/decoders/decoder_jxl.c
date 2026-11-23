/**
 * @file decoder_jxl.c
 * @brief JXL decoder implementation using libjxl
 *
 * Decodes JPEG XL images (both static and animated) to RGBA8888 format.
 * Supports both bare codestream and ISOBMFF container formats.
 */

/* clang-format off */
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jxl/decode.h>
/* clang-format on */

#include "decoder.h"
#include "../core/log.h"

#ifdef IMG2_HAVE_EXIF_READER
#include "../metadata/exif_reader.h"
#include "../metadata/meta_raw.h"
#endif

/** Maximum number of JXL frames to decode (prevents DoS) */
#define MAX_JXL_FRAMES 200

/**
 * @brief Get basic info from JXL image (dimensions and frame count)
 *
 * Walks the frame headers only (no pixel decoding). Stops after MAX_JXL_FRAMES + 1 frames:
 * more than MAX_JXL_FRAMES frames are clamped to MAX_JXL_FRAMES with a warning.
 *
 * @param data Raw JXL file data
 * @param len Length of data in bytes
 * @param width Output: image width
 * @param height Output: image height
 * @param num_frames Output: number of frames (at most MAX_JXL_FRAMES)
 * @return IMG2_OK; IMG2_E_LIMIT if the canvas fails img2_image_calc_size() (> IMG2_MAX_DIMENSION,
 *         > IMG2_MAX_PIXELS); IMG2_E_CODEC on any other error
 */
static img2_status_t jxl_get_info(const uint8_t *data, size_t len, uint32_t *width, uint32_t *height, int *num_frames);

/**
 * @brief Decode static (single-frame) JXL image
 *
 * @param data Raw JXL file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded (always 1)
 * @return Array of 1 img2_image_t*, or NULL on error
 */
static img2_image_t **decode_jxl_static(const uint8_t *data, size_t len, int *frame_count, img2_status_t *st);

/**
 * @brief Decode animated (multi-frame) JXL image
 *
 * @param data Raw JXL file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded
 * @param num_frames Expected number of frames (from jxl_get_info, at most MAX_JXL_FRAMES; with the cap
 *        reached, decoding stops before the next frame)
 * @param loop_count Output: JxlAnimationHeader.num_loops (0 = forever)
 * @return Array of img2_image_t* frames, or NULL on error
 *
 * Each frame's delay_ms = duration * 1000 * tps_denominator / tps_numerator.
 */
static img2_image_t **decode_jxl_animated(const uint8_t *data, size_t len, int *frame_count, int num_frames, int *loop_count, img2_status_t *st);

#ifdef IMG2_HAVE_EXIF_READER
/**
 * @brief Extract EXIF and XMP metadata from JXL boxes
 *
 * @param data Raw JXL file data
 * @param len Length of data in bytes
 * @param exif_out Output: EXIF metadata (allocated on success, NULL on failure)
 * @param xmp_out Output: XMP metadata (allocated on success, NULL on failure)
 * @param img Image that keeps copies of the raw blocks (Exif box without its 4-byte offset, xml box)
 */
static void jxl_extract_metadata(const uint8_t *data, size_t len, img2_exif_t **exif_out, img2_xmp_t **xmp_out, img2_image_t *img);
#endif

static img2_status_t jxl_get_info(const uint8_t *data, size_t len, uint32_t *width, uint32_t *height, int *num_frames)
{
	if (data == NULL || len == 0 || width == NULL || height == NULL || num_frames == NULL) {
		return IMG2_E_CODEC;
	}

	*num_frames = 0;

	// Create decoder
	JxlDecoder *dec = JxlDecoderCreate(NULL);
	if (dec == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create JXL decoder");
		return IMG2_E_CODEC;
	}

	// Subscribe to events
	if (JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_FRAME) != JXL_DEC_SUCCESS) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to subscribe to JXL decoder events");
		JxlDecoderDestroy(dec);
		return IMG2_E_CODEC;
	}

	// Set input data
	if (JxlDecoderSetInput(dec, data, len) != JXL_DEC_SUCCESS) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to set JXL decoder input");
		JxlDecoderDestroy(dec);
		return IMG2_E_CODEC;
	}

	// Close input to signal all data is available
	JxlDecoderCloseInput(dec);

	bool basic_info_received = false;
	int frame_count = 0;

	// Event loop
	while (true) {
		JxlDecoderStatus status = JxlDecoderProcessInput(dec);

		if (status == JXL_DEC_BASIC_INFO) {
			// Get basic info
			JxlBasicInfo info;
			if (JxlDecoderGetBasicInfo(dec, &info) != JXL_DEC_SUCCESS) {
				img2_log(IMG2_LOG_ERROR, "Error: Failed to get JXL basic info");
				JxlDecoderDestroy(dec);
				return IMG2_E_CODEC;
			}

			*width = info.xsize;
			*height = info.ysize;

			// Validate dimensions
			if (*width > IMG2_MAX_DIMENSION || *height > IMG2_MAX_DIMENSION) {
				img2_log(IMG2_LOG_ERROR, "Error: JXL dimensions exceed maximum: %ux%u", *width, *height);
				JxlDecoderDestroy(dec);
				return IMG2_E_LIMIT;
			}

			if (*width == 0 || *height == 0) {
				img2_log(IMG2_LOG_ERROR, "Error: JXL has invalid dimensions: %ux%u", *width, *height);
				JxlDecoderDestroy(dec);
				return IMG2_E_CODEC;
			}

			// Every decoded frame is a full canvas: reject a canvas over IMG2_MAX_PIXELS before walking the frames
			size_t byte_count;
			if (!img2_image_calc_size(*width, *height, &byte_count)) {
				img2_log(IMG2_LOG_ERROR, "Error: JXL dimensions exceed the pixel limit: %ux%u", *width, *height);
				JxlDecoderDestroy(dec);
				return IMG2_E_LIMIT;
			}

			basic_info_received = true;
		} else if (status == JXL_DEC_FRAME) {
			// Count frames; stop at the first frame past the cap (the rest is never read)
			if (frame_count == MAX_JXL_FRAMES) {
				img2_log(IMG2_LOG_WARN, "Warning: JXL has more than %d frames, limiting to %d", MAX_JXL_FRAMES, MAX_JXL_FRAMES);
				break;
			}
			frame_count++;
		} else if (status == JXL_DEC_SUCCESS) {
			// All frames processed
			break;
		} else if (status == JXL_DEC_ERROR) {
			// Decoding error
			img2_log(IMG2_LOG_ERROR, "Error: JXL decoder error");
			JxlDecoderDestroy(dec);
			return IMG2_E_CODEC;
		} else {
			// Unexpected status
			img2_log(IMG2_LOG_ERROR, "Error: Unexpected JXL decoder status: %d", status);
			JxlDecoderDestroy(dec);
			return IMG2_E_CODEC;
		}
	}

	// Cleanup
	JxlDecoderDestroy(dec);

	if (!basic_info_received || frame_count == 0) {
		img2_log(IMG2_LOG_ERROR, "Error: No frames found in JXL image");
		return IMG2_E_CODEC;
	}

	*num_frames = frame_count;
	return IMG2_OK;
}

static img2_image_t **decode_jxl_static(const uint8_t *data, size_t len, int *frame_count, img2_status_t *st)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_jxl_static");
		return img2_decode_fail(st, IMG2_E_ARG);
	}

	// Initialize output
	*frame_count = 0;

	// Create decoder
	JxlDecoder *dec = JxlDecoderCreate(NULL);
	if (dec == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create JXL decoder");
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Subscribe to events (NEED_IMAGE_OUT_BUFFER is a status, not an event to subscribe to)
	if (JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to subscribe to JXL decoder events");
		JxlDecoderDestroy(dec);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Set input data
	if (JxlDecoderSetInput(dec, data, len) != JXL_DEC_SUCCESS) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to set JXL decoder input");
		JxlDecoderDestroy(dec);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Close input
	JxlDecoderCloseInput(dec);

	// Define pixel format: RGBA8888
	JxlPixelFormat format = {
		.num_channels = 4,
		.data_type = JXL_TYPE_UINT8,
		.endianness = JXL_NATIVE_ENDIAN,
		.align = 0,
	};

	img2_image_t *output = NULL;
	uint32_t width = 0;
	uint32_t height = 0;

	// Event loop
	while (true) {
		JxlDecoderStatus status = JxlDecoderProcessInput(dec);

		if (status == JXL_DEC_BASIC_INFO) {
			// Get dimensions
			JxlBasicInfo info;
			if (JxlDecoderGetBasicInfo(dec, &info) != JXL_DEC_SUCCESS) {
				img2_log(IMG2_LOG_ERROR, "Error: Failed to get JXL basic info");
				*st = IMG2_E_CODEC;
				goto cleanup_error;
			}

			width = info.xsize;
			height = info.ysize;

			// Validate dimensions
			if (width > IMG2_MAX_DIMENSION || height > IMG2_MAX_DIMENSION) {
				img2_log(IMG2_LOG_ERROR, "Error: JXL dimensions exceed maximum: %ux%u", width, height);
				*st = IMG2_E_LIMIT;
				goto cleanup_error;
			}

			if (width == 0 || height == 0) {
				img2_log(IMG2_LOG_ERROR, "Error: JXL has invalid dimensions: %ux%u", width, height);
				*st = IMG2_E_CODEC;
				goto cleanup_error;
			}

			// Create img2_image_t (LIMIT over IMG2_MAX_PIXELS, NOMEM otherwise)
			img2_status_t alloc_st = img2_decode_image_alloc(width, height, &output);
			if (alloc_st != IMG2_OK) {
				img2_log(IMG2_LOG_ERROR, "Error: Failed to create image_t structure");
				*st = alloc_st;
				goto cleanup_error;
			}
		} else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
			// Ensure we have basic info first
			if (output == NULL) {
				img2_log(IMG2_LOG_ERROR, "Error: JXL buffer requested before basic info");
				*st = IMG2_E_CODEC;
				goto cleanup_error;
			}

			// Query buffer size
			size_t buffer_size;
			if (JxlDecoderImageOutBufferSize(dec, &format, &buffer_size) != JXL_DEC_SUCCESS) {
				img2_log(IMG2_LOG_ERROR, "Error: Failed to query JXL buffer size");
				*st = IMG2_E_CODEC;
				goto cleanup_error;
			}

			// Verify buffer size matches our allocation
			if (buffer_size != (size_t)width * height * 4) {
				img2_log(IMG2_LOG_ERROR, "Error: JXL buffer size mismatch: expected %zu, got %zu", (size_t)width * height * 4, buffer_size);
				*st = IMG2_E_CODEC;
				goto cleanup_error;
			}

			// Set output buffer
			if (JxlDecoderSetImageOutBuffer(dec, &format, output->pixels, buffer_size) != JXL_DEC_SUCCESS) {
				img2_log(IMG2_LOG_ERROR, "Error: Failed to set JXL output buffer");
				*st = IMG2_E_CODEC;
				goto cleanup_error;
			}
		} else if (status == JXL_DEC_FULL_IMAGE) {
			// Pixels are ready
			break;
		} else if (status == JXL_DEC_SUCCESS) {
			// All done
			break;
		} else if (status == JXL_DEC_ERROR) {
			img2_log(IMG2_LOG_ERROR, "Error: JXL decoder error");
			*st = IMG2_E_CODEC;
			goto cleanup_error;
		} else {
			img2_log(IMG2_LOG_ERROR, "Error: Unexpected JXL decoder status: %d", status);
			*st = IMG2_E_CODEC;
			goto cleanup_error;
		}
	}

	// Cleanup decoder
	JxlDecoderDestroy(dec);

	// Allocate frames array (single frame)
	img2_image_t **frames = (img2_image_t **)malloc(sizeof(img2_image_t *));
	if (frames == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate frames array");
		img2_image_destroy(output);
		return img2_decode_fail(st, IMG2_E_NOMEM);
	}

	frames[0] = output;
	*frame_count = 1;

	return frames;

cleanup_error:
	JxlDecoderDestroy(dec);
	if (output != NULL) {
		img2_image_destroy(output);
	}
	return NULL; /* *st set at the goto */
}

static img2_image_t **decode_jxl_animated(const uint8_t *data, size_t len, int *frame_count, int num_frames, int *loop_count, img2_status_t *st)
{
	if (data == NULL || len == 0 || frame_count == NULL || loop_count == NULL || num_frames <= 0) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_jxl_animated");
		return img2_decode_fail(st, IMG2_E_ARG);
	}

	// Initialize output
	*frame_count = 0;
	*loop_count = 0;

	// Allocate frames array
	img2_image_t **frames = (img2_image_t **)malloc(sizeof(img2_image_t *) * num_frames);
	if (frames == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate frames array");
		return img2_decode_fail(st, IMG2_E_NOMEM);
	}

	// Initialize frames to NULL for cleanup
	for (int i = 0; i < num_frames; i++) {
		frames[i] = NULL;
	}

	// Create decoder
	JxlDecoder *dec = JxlDecoderCreate(NULL);
	if (dec == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create JXL decoder");
		free(frames);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Subscribe to events (NEED_IMAGE_OUT_BUFFER is a status, not an event to subscribe to)
	if (JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_FRAME | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to subscribe to JXL decoder events");
		JxlDecoderDestroy(dec);
		free(frames);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Set input data
	if (JxlDecoderSetInput(dec, data, len) != JXL_DEC_SUCCESS) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to set JXL decoder input");
		JxlDecoderDestroy(dec);
		free(frames);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Close input
	JxlDecoderCloseInput(dec);

	// Define pixel format: RGBA8888
	JxlPixelFormat format = {
		.num_channels = 4,
		.data_type = JXL_TYPE_UINT8,
		.endianness = JXL_NATIVE_ENDIAN,
		.align = 0,
	};

	uint32_t width = 0;
	uint32_t height = 0;
	int current_frame = 0;
	bool basic_info_received = false;
	// Animation timing (ticks per second) and the pending frame's duration
	uint32_t tps_num = 0;
	uint32_t tps_den = 0;
	uint32_t num_loops = 0;
	uint32_t frame_delay_ms = 0;
	// [F14] running pixel total of the output frames
	uint64_t total_pixels = 0;

	// Event loop
	while (true) {
		JxlDecoderStatus status = JxlDecoderProcessInput(dec);

		if (status == JXL_DEC_BASIC_INFO) {
			// Get dimensions (only once)
			JxlBasicInfo info;
			if (JxlDecoderGetBasicInfo(dec, &info) != JXL_DEC_SUCCESS) {
				img2_log(IMG2_LOG_ERROR, "Error: Failed to get JXL basic info");
				*st = IMG2_E_CODEC;
				goto cleanup_error;
			}

			width = info.xsize;
			height = info.ysize;

			// Validate dimensions
			if (width > IMG2_MAX_DIMENSION || height > IMG2_MAX_DIMENSION) {
				img2_log(IMG2_LOG_ERROR, "Error: JXL dimensions exceed maximum: %ux%u", width, height);
				*st = IMG2_E_LIMIT;
				goto cleanup_error;
			}

			if (width == 0 || height == 0) {
				img2_log(IMG2_LOG_ERROR, "Error: JXL has invalid dimensions: %ux%u", width, height);
				*st = IMG2_E_CODEC;
				goto cleanup_error;
			}

			if (info.have_animation) {
				tps_num = info.animation.tps_numerator;
				tps_den = info.animation.tps_denominator;
				num_loops = info.animation.num_loops;
			}

			basic_info_received = true;
		} else if (status == JXL_DEC_FRAME) {
			// Start of new frame
			if (current_frame >= num_frames) {
				if (num_frames == MAX_JXL_FRAMES) {
					break; // clamped by jxl_get_info (warning printed there): stop before decoding frame 201
				}
				img2_log(IMG2_LOG_ERROR, "Error: JXL frame count mismatch: expected %d, got more", num_frames);
				*st = IMG2_E_CODEC;
				goto cleanup_error;
			}

			frame_delay_ms = 0;
			JxlFrameHeader frame_header;
			if (tps_num != 0 && JxlDecoderGetFrameHeader(dec, &frame_header) == JXL_DEC_SUCCESS) {
				frame_delay_ms = img2_decode_ticks_to_ms(frame_header.duration, tps_num, tps_den);
			}
		} else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
			// Ensure we have basic info
			if (!basic_info_received) {
				img2_log(IMG2_LOG_ERROR, "Error: JXL buffer requested before basic info");
				*st = IMG2_E_CODEC;
				goto cleanup_error;
			}
			// [F1] one buffer per frame, inside the frames array (JXL_DEC_FRAME checked the index)
			if (current_frame >= num_frames || frames[current_frame] != NULL) {
				img2_log(IMG2_LOG_ERROR, "Error: JXL buffer requested for unexpected frame %d", current_frame);
				*st = IMG2_E_CODEC;
				goto cleanup_error;
			}

			// Allocate img2_image_t for current frame ([F14] within the running pixel total)
			if (img2_decode_add_frame_pixels(&total_pixels, width, height) != IMG2_OK) {
				*st = IMG2_E_LIMIT;
				goto cleanup_error;
			}
			img2_status_t alloc_st = img2_decode_image_alloc(width, height, &frames[current_frame]);
			if (alloc_st != IMG2_OK) {
				img2_log(IMG2_LOG_ERROR, "Error: Failed to create frame %d", current_frame);
				*st = alloc_st;
				goto cleanup_error;
			}
			frames[current_frame]->delay_ms = frame_delay_ms;

			// Query buffer size
			size_t buffer_size;
			if (JxlDecoderImageOutBufferSize(dec, &format, &buffer_size) != JXL_DEC_SUCCESS) {
				img2_log(IMG2_LOG_ERROR, "Error: Failed to query JXL buffer size for frame %d", current_frame);
				*st = IMG2_E_CODEC;
				goto cleanup_error;
			}

			// Verify buffer size
			if (buffer_size != (size_t)width * height * 4) {
				img2_log(IMG2_LOG_ERROR, "Error: JXL buffer size mismatch for frame %d: expected %zu, got %zu", current_frame, (size_t)width * height * 4, buffer_size);
				*st = IMG2_E_CODEC;
				goto cleanup_error;
			}

			// Set output buffer
			if (JxlDecoderSetImageOutBuffer(dec, &format, frames[current_frame]->pixels, buffer_size) != JXL_DEC_SUCCESS) {
				img2_log(IMG2_LOG_ERROR, "Error: Failed to set JXL output buffer for frame %d", current_frame);
				*st = IMG2_E_CODEC;
				goto cleanup_error;
			}
		} else if (status == JXL_DEC_FULL_IMAGE) {
			// Current frame complete
			current_frame++;
		} else if (status == JXL_DEC_SUCCESS) {
			// All frames decoded
			break;
		} else if (status == JXL_DEC_ERROR) {
			img2_log(IMG2_LOG_ERROR, "Error: JXL decoder error");
			*st = IMG2_E_CODEC;
			goto cleanup_error;
		} else {
			img2_log(IMG2_LOG_ERROR, "Error: Unexpected JXL decoder status: %d", status);
			*st = IMG2_E_CODEC;
			goto cleanup_error;
		}
	}

	// Cleanup decoder
	JxlDecoderDestroy(dec);

	// Verify we got all frames
	if (current_frame != num_frames) {
		img2_log(IMG2_LOG_ERROR, "Error: JXL frame count mismatch: expected %d, got %d", num_frames, current_frame);
		// Cleanup allocated frames
		for (int i = 0; i < num_frames; i++) {
			if (frames[i] != NULL) {
				img2_image_destroy(frames[i]);
			}
		}
		free(frames);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	*frame_count = num_frames;
	*loop_count = num_loops > (uint32_t)INT_MAX ? INT_MAX : (int)num_loops;

	return frames;

cleanup_error:
	JxlDecoderDestroy(dec);
	// Cleanup on error
	for (int i = 0; i < num_frames; i++) {
		if (frames[i] != NULL) {
			img2_image_destroy(frames[i]);
		}
	}
	free(frames);
	return NULL; /* *st set at the goto */
}

#ifdef IMG2_HAVE_EXIF_READER
/* Upper bound of the top-level boxes jxl_find_box() looks at (real files have a handful; partial codestream
 * 'jxlp' boxes can add a few hundred) */
#define JXL_MAX_BOXES 4096

static uint64_t jxl_be(const uint8_t *p, int n)
{
	uint64_t v = 0;
	for (int i = 0; i < n; i++) {
		v = (v << 8) | p[i];
	}
	return v;
}

/**
 * @brief Find a top-level box of a JPEG XL container (ISO/IEC 18181-2) by walking the ISOBMFF box headers
 *
 * Box header: 32-bit big-endian size (header included) and 4-byte type; size 1 = a 64-bit size follows the
 * type, size 0 = the box extends to the end of the file. A bare codestream has no boxes. The walk stops at the
 * first malformed header (size smaller than the header or past the end of the data) and after JXL_MAX_BOXES
 * boxes. Only top-level boxes are matched: bytes inside another box's payload (codestream 'jxlc' / 'jxlp',
 * other boxes) are never taken for a box. Brotli-compressed 'brob' boxes are not decompressed, so a compressed
 * Exif / xml box is not found.
 *
 * @param data File data (decoded successfully as JPEG XL)
 * @param len File length
 * @param box_type 4-character box type (e.g. "Exif")
 * @param box_data_out Output: pointer to the box payload (within data)
 * @param box_size_out Output: payload size
 * @return 0 on success, -1 if not found
 */
static int jxl_find_box(const uint8_t *data, size_t len, const char *box_type, const uint8_t **box_data_out, size_t *box_size_out)
{
	if (JxlSignatureCheck(data, len) != JXL_SIG_CONTAINER) {
		return -1; /* bare codestream: no boxes */
	}

	size_t pos = 0;
	for (int n = 0; n < JXL_MAX_BOXES; n++) {
		size_t left = len - pos;
		if (left < 8) {
			return -1; /* end of the data (or trailing bytes too short for a box header) */
		}
		const uint8_t *hdr = data + pos;
		uint64_t size = jxl_be(hdr, 4);
		uint64_t hdr_len = 8;
		if (size == 1) {
			if (left < 16) {
				return -1;
			}
			size = jxl_be(hdr + 8, 8);
			hdr_len = 16;
		} else if (size == 0) {
			size = left;
		}
		if (size < hdr_len || size > left) {
			return -1; /* malformed header: nothing after it can be located */
		}
		if (memcmp(hdr + 4, box_type, 4) == 0) {
			*box_data_out = hdr + hdr_len;
			*box_size_out = (size_t)(size - hdr_len);
			return 0;
		}
		pos += (size_t)size;
	}
	return -1;
}

static void jxl_extract_metadata(const uint8_t *data, size_t len, img2_exif_t **exif_out, img2_xmp_t **xmp_out, img2_image_t *img)
{
	if (data == NULL || len == 0 || exif_out == NULL || xmp_out == NULL) {
		return;
	}

	// Initialize outputs
	*exif_out = NULL;
	*xmp_out = NULL;

	// Find Exif box in JXL file
	const uint8_t *exif_data = NULL;
	size_t exif_size = 0;
	if (jxl_find_box(data, len, "Exif", &exif_data, &exif_size) == 0) {
		// Exif box often has a 4-byte offset prefix before TIFF data
		// Check if data starts with TIFF magic, if not skip 4 bytes
		const uint8_t *tiff_data = NULL;
		size_t tiff_size = 0;

		// Check for TIFF magic at offset 0
		if (exif_size >= 4 && (exif_data[0] == 'I' || exif_data[0] == 'M')) {
			// Looks like TIFF header at offset 0, use as-is
			tiff_data = exif_data;
			tiff_size = exif_size;
		} else if (exif_size >= 8 && (exif_data[4] == 'I' || exif_data[4] == 'M')) {
			// TIFF header is at offset 4, skip the 4-byte prefix
			tiff_data = exif_data + 4;
			tiff_size = exif_size - 4;
		}
		// else: invalid EXIF data (no EXIF; the XMP box is still read below)

		if (tiff_data != NULL) {
			img2_meta_keep_exif(img, "JXL", tiff_data, tiff_size);
		}

		// Parse EXIF data
		img2_exif_t *exif = tiff_data != NULL ? malloc(sizeof(img2_exif_t)) : NULL;
		if (exif != NULL) {
			img2_exif_init(exif);
			// Call img2_exif_parse_tiff with raw TIFF data
			if (img2_exif_parse_tiff(exif, tiff_data, tiff_size) == 0) {
				*exif_out = exif;
			} else {
				// Parse failed
				free(exif);
			}
		}
	}

	// Find xml box (XMP) in JXL file
	const uint8_t *xmp_data = NULL;
	size_t xmp_size = 0;
	if (jxl_find_box(data, len, "xml ", &xmp_data, &xmp_size) == 0) {
		img2_meta_keep_xmp(img, "JXL", xmp_data, xmp_size);

		// Parse XMP data
		img2_xmp_t *xmp = malloc(sizeof(img2_xmp_t));
		if (xmp != NULL) {
			img2_xmp_init(xmp);
			// Call img2_xmp_parse_xml with XML data
			if (img2_xmp_parse_xml(xmp, (const char *)xmp_data, xmp_size) == 0) {
				*xmp_out = xmp;
			} else {
				// Parse failed
				img2_xmp_clear(xmp);
				free(xmp);
			}
		}
	}
}
#endif

static img2_image_t **decode_jxl_frames(const uint8_t *data, size_t len, int *frame_count, int *loop_count, img2_status_t *st)
{
	if (data == NULL || len == 0 || frame_count == NULL || loop_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_jxl");
		return img2_decode_fail(st, IMG2_E_ARG);
	}

	// Initialize output
	*frame_count = 0;
	*loop_count = 0;

	// Get basic info (dimensions and frame count)
	uint32_t width = 0;
	uint32_t height = 0;
	int num_frames = 0;

	img2_status_t info_st = jxl_get_info(data, len, &width, &height, &num_frames);
	if (info_st != IMG2_OK) {
		// Error already printed by jxl_get_info
		return img2_decode_fail(st, info_st);
	}

	// [F14] every output frame is a full canvas: reject before allocating anything
	if (num_frames > 1 && img2_decode_check_total_pixels((uint64_t)num_frames, width, height) != IMG2_OK) {
		return img2_decode_fail(st, IMG2_E_LIMIT);
	}

	// Route to appropriate decoder
	img2_image_t **frames = NULL;
	if (num_frames == 1) {
		frames = decode_jxl_static(data, len, frame_count, st);
	} else {
		frames = decode_jxl_animated(data, len, frame_count, num_frames, loop_count, st);
	}

	// Extract EXIF/XMP metadata if decoding succeeded
#ifdef IMG2_HAVE_EXIF_READER
	if (frames != NULL && *frame_count > 0) {
		img2_exif_t *exif = NULL;
		img2_xmp_t *xmp = NULL;

		// Extract metadata from JXL boxes (raw blocks kept on the first frame). The orientation field stays 1:
		// libjxl already applied the codestream orientation (no JxlDecoderSetKeepOrientation), and the Exif box
		// orientation is not authoritative for JPEG XL
		jxl_extract_metadata(data, len, &exif, &xmp, frames[0]);

		// Attach metadata to the first frame (metadata applies to entire image)
		if (frames[0] != NULL) {
			frames[0]->exif = exif;
			frames[0]->xmp = xmp;
		} else {
			// Cleanup metadata if frame is NULL
			if (exif != NULL) {
				free(exif);
			}
			if (xmp != NULL) {
				img2_xmp_clear(xmp);
				free(xmp);
			}
		}
	}
#endif

	return frames;
}

img2_status_t img2_decode_jxl(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out)
{
	(void)opts;
	if (out == NULL) {
		return IMG2_E_ARG;
	}
	img2_status_t st = IMG2_E_INTERNAL;
	int count = 0;
	int loop_count = 0;
	img2_image_t **frames = decode_jxl_frames(data, len, &count, &loop_count, &st);
	st = img2_frames_adopt(out, frames, count, st);
	if (st == IMG2_OK) {
		out->loop_count = loop_count;
	}
	return st;
}
