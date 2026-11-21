/**
 * @file decoder_jxl.c
 * @brief JXL decoder implementation using libjxl
 *
 * Decodes JPEG XL images (both static and animated) to RGBA8888 format.
 * Supports both bare codestream and ISOBMFF container formats.
 */

/* clang-format off */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jxl/decode.h>
/* clang-format on */

#include "decoder.h"
#include "../core/log.h"

#ifdef IMG2_HAVE_EXIF_READER
#include "../metadata/exif_reader.h"
#endif

/** Maximum number of JXL frames to decode (prevents DoS) */
#define MAX_JXL_FRAMES 200

/**
 * @brief Get basic info from JXL image (dimensions and frame count)
 *
 * @param data Raw JXL file data
 * @param len Length of data in bytes
 * @param width Output: image width
 * @param height Output: image height
 * @param num_frames Output: number of frames
 * @return Number of frames on success, -1 on error
 */
static int jxl_get_info(const uint8_t *data, size_t len, uint32_t *width, uint32_t *height, int *num_frames);

/**
 * @brief Decode static (single-frame) JXL image
 *
 * @param data Raw JXL file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded (always 1)
 * @return Array of 1 img2_image_t*, or NULL on error
 */
static img2_image_t **decode_jxl_static(const uint8_t *data, size_t len, int *frame_count);

/**
 * @brief Decode animated (multi-frame) JXL image
 *
 * @param data Raw JXL file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded
 * @param num_frames Expected number of frames
 * @return Array of img2_image_t* frames, or NULL on error
 */
static img2_image_t **decode_jxl_animated(const uint8_t *data, size_t len, int *frame_count, int num_frames);

#ifdef IMG2_HAVE_EXIF_READER
/**
 * @brief Extract EXIF and XMP metadata from JXL boxes
 *
 * @param data Raw JXL file data
 * @param len Length of data in bytes
 * @param exif_out Output: EXIF metadata (allocated on success, NULL on failure)
 * @param xmp_out Output: XMP metadata (allocated on success, NULL on failure)
 */
static void jxl_extract_metadata(const uint8_t *data, size_t len, img2_exif_t **exif_out, img2_xmp_t **xmp_out);
#endif

static int jxl_get_info(const uint8_t *data, size_t len, uint32_t *width, uint32_t *height, int *num_frames)
{
	if (data == NULL || len == 0 || width == NULL || height == NULL || num_frames == NULL) {
		return -1;
	}

	*num_frames = 0;

	// Create decoder
	JxlDecoder *dec = JxlDecoderCreate(NULL);
	if (dec == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create JXL decoder");
		return -1;
	}

	// Subscribe to events
	if (JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_FRAME) != JXL_DEC_SUCCESS) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to subscribe to JXL decoder events");
		JxlDecoderDestroy(dec);
		return -1;
	}

	// Set input data
	if (JxlDecoderSetInput(dec, data, len) != JXL_DEC_SUCCESS) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to set JXL decoder input");
		JxlDecoderDestroy(dec);
		return -1;
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
				return -1;
			}

			*width = info.xsize;
			*height = info.ysize;

			// Validate dimensions
			if (*width > IMG2_MAX_DIMENSION || *height > IMG2_MAX_DIMENSION) {
				img2_log(IMG2_LOG_ERROR, "Error: JXL dimensions exceed maximum: %ux%u", *width, *height);
				JxlDecoderDestroy(dec);
				return -1;
			}

			if (*width == 0 || *height == 0) {
				img2_log(IMG2_LOG_ERROR, "Error: JXL has invalid dimensions: %ux%u", *width, *height);
				JxlDecoderDestroy(dec);
				return -1;
			}

			basic_info_received = true;
		} else if (status == JXL_DEC_FRAME) {
			// Count frames
			frame_count++;

			// Enforce frame limit
			if (frame_count > MAX_JXL_FRAMES) {
				img2_log(IMG2_LOG_ERROR, "Error: JXL frame count exceeds maximum (%d)", MAX_JXL_FRAMES);
				JxlDecoderDestroy(dec);
				return -1;
			}
		} else if (status == JXL_DEC_SUCCESS) {
			// All frames processed
			break;
		} else if (status == JXL_DEC_ERROR) {
			// Decoding error
			img2_log(IMG2_LOG_ERROR, "Error: JXL decoder error");
			JxlDecoderDestroy(dec);
			return -1;
		} else {
			// Unexpected status
			img2_log(IMG2_LOG_ERROR, "Error: Unexpected JXL decoder status: %d", status);
			JxlDecoderDestroy(dec);
			return -1;
		}
	}

	// Cleanup
	JxlDecoderDestroy(dec);

	if (!basic_info_received || frame_count == 0) {
		img2_log(IMG2_LOG_ERROR, "Error: No frames found in JXL image");
		return -1;
	}

	*num_frames = frame_count;
	return frame_count;
}

static img2_image_t **decode_jxl_static(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_jxl_static");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Create decoder
	JxlDecoder *dec = JxlDecoderCreate(NULL);
	if (dec == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create JXL decoder");
		return NULL;
	}

	// Subscribe to events (NEED_IMAGE_OUT_BUFFER is a status, not an event to subscribe to)
	if (JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to subscribe to JXL decoder events");
		JxlDecoderDestroy(dec);
		return NULL;
	}

	// Set input data
	if (JxlDecoderSetInput(dec, data, len) != JXL_DEC_SUCCESS) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to set JXL decoder input");
		JxlDecoderDestroy(dec);
		return NULL;
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
				goto cleanup_error;
			}

			width = info.xsize;
			height = info.ysize;

			// Validate dimensions
			if (width > IMG2_MAX_DIMENSION || height > IMG2_MAX_DIMENSION) {
				img2_log(IMG2_LOG_ERROR, "Error: JXL dimensions exceed maximum: %ux%u", width, height);
				goto cleanup_error;
			}

			if (width == 0 || height == 0) {
				img2_log(IMG2_LOG_ERROR, "Error: JXL has invalid dimensions: %ux%u", width, height);
				goto cleanup_error;
			}

			// Create img2_image_t
			output = img2_image_alloc(width, height);
			if (output == NULL) {
				img2_log(IMG2_LOG_ERROR, "Error: Failed to create image_t structure");
				goto cleanup_error;
			}
		} else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
			// Ensure we have basic info first
			if (output == NULL) {
				img2_log(IMG2_LOG_ERROR, "Error: JXL buffer requested before basic info");
				goto cleanup_error;
			}

			// Query buffer size
			size_t buffer_size;
			if (JxlDecoderImageOutBufferSize(dec, &format, &buffer_size) != JXL_DEC_SUCCESS) {
				img2_log(IMG2_LOG_ERROR, "Error: Failed to query JXL buffer size");
				goto cleanup_error;
			}

			// Verify buffer size matches our allocation
			if (buffer_size != width * height * 4) {
				img2_log(IMG2_LOG_ERROR, "Error: JXL buffer size mismatch: expected %zu, got %zu", (size_t)(width * height * 4), buffer_size);
				goto cleanup_error;
			}

			// Set output buffer
			if (JxlDecoderSetImageOutBuffer(dec, &format, output->pixels, buffer_size) != JXL_DEC_SUCCESS) {
				img2_log(IMG2_LOG_ERROR, "Error: Failed to set JXL output buffer");
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
			goto cleanup_error;
		} else {
			img2_log(IMG2_LOG_ERROR, "Error: Unexpected JXL decoder status: %d", status);
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
		return NULL;
	}

	frames[0] = output;
	*frame_count = 1;

	return frames;

cleanup_error:
	JxlDecoderDestroy(dec);
	if (output != NULL) {
		img2_image_destroy(output);
	}
	return NULL;
}

static img2_image_t **decode_jxl_animated(const uint8_t *data, size_t len, int *frame_count, int num_frames)
{
	if (data == NULL || len == 0 || frame_count == NULL || num_frames <= 0) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_jxl_animated");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Allocate frames array
	img2_image_t **frames = (img2_image_t **)malloc(sizeof(img2_image_t *) * num_frames);
	if (frames == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate frames array");
		return NULL;
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
		return NULL;
	}

	// Subscribe to events (NEED_IMAGE_OUT_BUFFER is a status, not an event to subscribe to)
	if (JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_FRAME | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to subscribe to JXL decoder events");
		JxlDecoderDestroy(dec);
		free(frames);
		return NULL;
	}

	// Set input data
	if (JxlDecoderSetInput(dec, data, len) != JXL_DEC_SUCCESS) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to set JXL decoder input");
		JxlDecoderDestroy(dec);
		free(frames);
		return NULL;
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

	// Event loop
	while (true) {
		JxlDecoderStatus status = JxlDecoderProcessInput(dec);

		if (status == JXL_DEC_BASIC_INFO) {
			// Get dimensions (only once)
			JxlBasicInfo info;
			if (JxlDecoderGetBasicInfo(dec, &info) != JXL_DEC_SUCCESS) {
				img2_log(IMG2_LOG_ERROR, "Error: Failed to get JXL basic info");
				goto cleanup_error;
			}

			width = info.xsize;
			height = info.ysize;

			// Validate dimensions
			if (width > IMG2_MAX_DIMENSION || height > IMG2_MAX_DIMENSION) {
				img2_log(IMG2_LOG_ERROR, "Error: JXL dimensions exceed maximum: %ux%u", width, height);
				goto cleanup_error;
			}

			if (width == 0 || height == 0) {
				img2_log(IMG2_LOG_ERROR, "Error: JXL has invalid dimensions: %ux%u", width, height);
				goto cleanup_error;
			}

			basic_info_received = true;
		} else if (status == JXL_DEC_FRAME) {
			// Start of new frame
			if (current_frame >= num_frames) {
				img2_log(IMG2_LOG_ERROR, "Error: JXL frame count mismatch: expected %d, got more", num_frames);
				goto cleanup_error;
			}
		} else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
			// Ensure we have basic info
			if (!basic_info_received) {
				img2_log(IMG2_LOG_ERROR, "Error: JXL buffer requested before basic info");
				goto cleanup_error;
			}

			// Allocate img2_image_t for current frame
			frames[current_frame] = img2_image_alloc(width, height);
			if (frames[current_frame] == NULL) {
				img2_log(IMG2_LOG_ERROR, "Error: Failed to create frame %d", current_frame);
				goto cleanup_error;
			}

			// Query buffer size
			size_t buffer_size;
			if (JxlDecoderImageOutBufferSize(dec, &format, &buffer_size) != JXL_DEC_SUCCESS) {
				img2_log(IMG2_LOG_ERROR, "Error: Failed to query JXL buffer size for frame %d", current_frame);
				goto cleanup_error;
			}

			// Verify buffer size
			if (buffer_size != width * height * 4) {
				img2_log(IMG2_LOG_ERROR, "Error: JXL buffer size mismatch for frame %d: expected %zu, got %zu", current_frame, (size_t)(width * height * 4), buffer_size);
				goto cleanup_error;
			}

			// Set output buffer
			if (JxlDecoderSetImageOutBuffer(dec, &format, frames[current_frame]->pixels, buffer_size) != JXL_DEC_SUCCESS) {
				img2_log(IMG2_LOG_ERROR, "Error: Failed to set JXL output buffer for frame %d", current_frame);
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
			goto cleanup_error;
		} else {
			img2_log(IMG2_LOG_ERROR, "Error: Unexpected JXL decoder status: %d", status);
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
		return NULL;
	}

	*frame_count = num_frames;

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
	return NULL;
}

#ifdef IMG2_HAVE_EXIF_READER
/**
 * @brief Find a box in JXL ISOBMFF container by scanning memory
 *
 * WORKAROUND: The libjxl box API is complex and hard to use correctly.
 * This function directly scans the file memory for box headers.
 *
 * @param data File data
 * @param len File length
 * @param box_type 4-character box type (e.g. "Exif")
 * @param box_data_out Output: pointer to box data (within input data buffer)
 * @param box_size_out Output: size of box data
 * @return 0 on success, -1 if not found
 */
static int jxl_find_box(const uint8_t *data, size_t len, const char *box_type, const uint8_t **box_data_out, size_t *box_size_out)
{
	if (data == NULL || len < 12 || box_type == NULL || box_data_out == NULL || box_size_out == NULL) {
		return -1;
	}

	// Scan for box headers in ISOBMFF format:
	// 4 bytes: box size (big-endian)
	// 4 bytes: box type
	// N bytes: box data

	for (size_t i = 0; i + 8 < len; i++) {
		// Read box size (big-endian uint32)
		uint32_t box_size = ((uint32_t)data[i] << 24) | ((uint32_t)data[i + 1] << 16) | ((uint32_t)data[i + 2] << 8) | ((uint32_t)data[i + 3]);

		// Check if box type matches
		if (data[i + 4] == box_type[0] && data[i + 5] == box_type[1] && data[i + 6] == box_type[2] && data[i + 7] == box_type[3]) {

			// Found the box!
			// Box data starts at i+8, size is (box_size - 8)
			if (box_size >= 8 && i + box_size <= len) {
				*box_data_out = data + i + 8;
				*box_size_out = box_size - 8;
				return 0;
			}
		}

		// Skip to next potential box (advance by 1 byte for safety)
	}

	return -1; // Not found
}

static void jxl_extract_metadata(const uint8_t *data, size_t len, img2_exif_t **exif_out, img2_xmp_t **xmp_out)
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
		const uint8_t *tiff_data = exif_data;
		size_t tiff_size = exif_size;

		// Check for TIFF magic at offset 0
		if (exif_size >= 4 && (exif_data[0] == 'I' || exif_data[0] == 'M')) {
			// Looks like TIFF header at offset 0, use as-is
			tiff_data = exif_data;
			tiff_size = exif_size;
		} else if (exif_size >= 8 && (exif_data[4] == 'I' || exif_data[4] == 'M')) {
			// TIFF header is at offset 4, skip the 4-byte prefix
			tiff_data = exif_data + 4;
			tiff_size = exif_size - 4;
		} else {
			// Invalid EXIF data
			return;
		}

		// Parse EXIF data
		img2_exif_t *exif = malloc(sizeof(img2_exif_t));
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

img2_image_t **img2_decode_jxl(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_jxl");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Get basic info (dimensions and frame count)
	uint32_t width = 0;
	uint32_t height = 0;
	int num_frames = 0;

	int result = jxl_get_info(data, len, &width, &height, &num_frames);
	if (result == -1) {
		// Error already printed by jxl_get_info
		return NULL;
	}

	// Route to appropriate decoder
	img2_image_t **frames = NULL;
	if (num_frames == 1) {
		frames = decode_jxl_static(data, len, frame_count);
	} else {
		frames = decode_jxl_animated(data, len, frame_count, num_frames);
	}

	// Extract EXIF/XMP metadata if decoding succeeded
#ifdef IMG2_HAVE_EXIF_READER
	if (frames != NULL && *frame_count > 0) {
		img2_exif_t *exif = NULL;
		img2_xmp_t *xmp = NULL;

		// Extract metadata from JXL boxes
		jxl_extract_metadata(data, len, &exif, &xmp);

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
