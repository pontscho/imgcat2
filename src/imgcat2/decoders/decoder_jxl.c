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
 * @return Array of 1 image_t*, or NULL on error
 */
static image_t **decode_jxl_static(const uint8_t *data, size_t len, int *frame_count);

/**
 * @brief Decode animated (multi-frame) JXL image
 *
 * @param data Raw JXL file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded
 * @param num_frames Expected number of frames
 * @return Array of image_t* frames, or NULL on error
 */
static image_t **decode_jxl_animated(const uint8_t *data, size_t len, int *frame_count, int num_frames);

static int jxl_get_info(const uint8_t *data, size_t len, uint32_t *width, uint32_t *height, int *num_frames)
{
	if (data == NULL || len == 0 || width == NULL || height == NULL || num_frames == NULL) {
		return -1;
	}

	*num_frames = 0;

	// Create decoder
	JxlDecoder *dec = JxlDecoderCreate(NULL);
	if (dec == NULL) {
		fprintf(stderr, "Error: Failed to create JXL decoder\n");
		return -1;
	}

	// Subscribe to events
	if (JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_FRAME) != JXL_DEC_SUCCESS) {
		fprintf(stderr, "Error: Failed to subscribe to JXL decoder events\n");
		JxlDecoderDestroy(dec);
		return -1;
	}

	// Set input data
	if (JxlDecoderSetInput(dec, data, len) != JXL_DEC_SUCCESS) {
		fprintf(stderr, "Error: Failed to set JXL decoder input\n");
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
				fprintf(stderr, "Error: Failed to get JXL basic info\n");
				JxlDecoderDestroy(dec);
				return -1;
			}

			*width = info.xsize;
			*height = info.ysize;

			// Validate dimensions
			if (*width > IMAGE_MAX_DIMENSION || *height > IMAGE_MAX_DIMENSION) {
				fprintf(stderr, "Error: JXL dimensions exceed maximum: %ux%u\n", *width, *height);
				JxlDecoderDestroy(dec);
				return -1;
			}

			if (*width == 0 || *height == 0) {
				fprintf(stderr, "Error: JXL has invalid dimensions: %ux%u\n", *width, *height);
				JxlDecoderDestroy(dec);
				return -1;
			}

			basic_info_received = true;
		} else if (status == JXL_DEC_FRAME) {
			// Count frames
			frame_count++;

			// Enforce frame limit
			if (frame_count > MAX_JXL_FRAMES) {
				fprintf(stderr, "Error: JXL frame count exceeds maximum (%d)\n", MAX_JXL_FRAMES);
				JxlDecoderDestroy(dec);
				return -1;
			}
		} else if (status == JXL_DEC_SUCCESS) {
			// All frames processed
			break;
		} else if (status == JXL_DEC_ERROR) {
			// Decoding error
			fprintf(stderr, "Error: JXL decoder error\n");
			JxlDecoderDestroy(dec);
			return -1;
		} else {
			// Unexpected status
			fprintf(stderr, "Error: Unexpected JXL decoder status: %d\n", status);
			JxlDecoderDestroy(dec);
			return -1;
		}
	}

	// Cleanup
	JxlDecoderDestroy(dec);

	if (!basic_info_received || frame_count == 0) {
		fprintf(stderr, "Error: No frames found in JXL image\n");
		return -1;
	}

	*num_frames = frame_count;
	return frame_count;
}

static image_t **decode_jxl_static(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_jxl_static\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Create decoder
	JxlDecoder *dec = JxlDecoderCreate(NULL);
	if (dec == NULL) {
		fprintf(stderr, "Error: Failed to create JXL decoder\n");
		return NULL;
	}

	// Subscribe to events (NEED_IMAGE_OUT_BUFFER is a status, not an event to subscribe to)
	if (JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS) {
		fprintf(stderr, "Error: Failed to subscribe to JXL decoder events\n");
		JxlDecoderDestroy(dec);
		return NULL;
	}

	// Set input data
	if (JxlDecoderSetInput(dec, data, len) != JXL_DEC_SUCCESS) {
		fprintf(stderr, "Error: Failed to set JXL decoder input\n");
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

	image_t *output = NULL;
	uint32_t width = 0;
	uint32_t height = 0;

	// Event loop
	while (true) {
		JxlDecoderStatus status = JxlDecoderProcessInput(dec);

		if (status == JXL_DEC_BASIC_INFO) {
			// Get dimensions
			JxlBasicInfo info;
			if (JxlDecoderGetBasicInfo(dec, &info) != JXL_DEC_SUCCESS) {
				fprintf(stderr, "Error: Failed to get JXL basic info\n");
				goto cleanup_error;
			}

			width = info.xsize;
			height = info.ysize;

			// Validate dimensions
			if (width > IMAGE_MAX_DIMENSION || height > IMAGE_MAX_DIMENSION) {
				fprintf(stderr, "Error: JXL dimensions exceed maximum: %ux%u\n", width, height);
				goto cleanup_error;
			}

			if (width == 0 || height == 0) {
				fprintf(stderr, "Error: JXL has invalid dimensions: %ux%u\n", width, height);
				goto cleanup_error;
			}

			// Create image_t
			output = image_create(width, height);
			if (output == NULL) {
				fprintf(stderr, "Error: Failed to create image_t structure\n");
				goto cleanup_error;
			}
		} else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
			// Ensure we have basic info first
			if (output == NULL) {
				fprintf(stderr, "Error: JXL buffer requested before basic info\n");
				goto cleanup_error;
			}

			// Query buffer size
			size_t buffer_size;
			if (JxlDecoderImageOutBufferSize(dec, &format, &buffer_size) != JXL_DEC_SUCCESS) {
				fprintf(stderr, "Error: Failed to query JXL buffer size\n");
				goto cleanup_error;
			}

			// Verify buffer size matches our allocation
			if (buffer_size != width * height * 4) {
				fprintf(stderr, "Error: JXL buffer size mismatch: expected %zu, got %zu\n", (size_t)(width * height * 4), buffer_size);
				goto cleanup_error;
			}

			// Set output buffer
			if (JxlDecoderSetImageOutBuffer(dec, &format, output->pixels, buffer_size) != JXL_DEC_SUCCESS) {
				fprintf(stderr, "Error: Failed to set JXL output buffer\n");
				goto cleanup_error;
			}
		} else if (status == JXL_DEC_FULL_IMAGE) {
			// Pixels are ready
			break;
		} else if (status == JXL_DEC_SUCCESS) {
			// All done
			break;
		} else if (status == JXL_DEC_ERROR) {
			fprintf(stderr, "Error: JXL decoder error\n");
			goto cleanup_error;
		} else {
			fprintf(stderr, "Error: Unexpected JXL decoder status: %d\n", status);
			goto cleanup_error;
		}
	}

	// Cleanup decoder
	JxlDecoderDestroy(dec);

	// Allocate frames array (single frame)
	image_t **frames = (image_t **)malloc(sizeof(image_t *));
	if (frames == NULL) {
		fprintf(stderr, "Error: Failed to allocate frames array\n");
		image_destroy(output);
		return NULL;
	}

	frames[0] = output;
	*frame_count = 1;

	return frames;

cleanup_error:
	JxlDecoderDestroy(dec);
	if (output != NULL) {
		image_destroy(output);
	}
	return NULL;
}

static image_t **decode_jxl_animated(const uint8_t *data, size_t len, int *frame_count, int num_frames)
{
	if (data == NULL || len == 0 || frame_count == NULL || num_frames <= 0) {
		fprintf(stderr, "Error: Invalid parameters to decode_jxl_animated\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Allocate frames array
	image_t **frames = (image_t **)malloc(sizeof(image_t *) * num_frames);
	if (frames == NULL) {
		fprintf(stderr, "Error: Failed to allocate frames array\n");
		return NULL;
	}

	// Initialize frames to NULL for cleanup
	for (int i = 0; i < num_frames; i++) {
		frames[i] = NULL;
	}

	// Create decoder
	JxlDecoder *dec = JxlDecoderCreate(NULL);
	if (dec == NULL) {
		fprintf(stderr, "Error: Failed to create JXL decoder\n");
		free(frames);
		return NULL;
	}

	// Subscribe to events (NEED_IMAGE_OUT_BUFFER is a status, not an event to subscribe to)
	if (JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_FRAME | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS) {
		fprintf(stderr, "Error: Failed to subscribe to JXL decoder events\n");
		JxlDecoderDestroy(dec);
		free(frames);
		return NULL;
	}

	// Set input data
	if (JxlDecoderSetInput(dec, data, len) != JXL_DEC_SUCCESS) {
		fprintf(stderr, "Error: Failed to set JXL decoder input\n");
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
				fprintf(stderr, "Error: Failed to get JXL basic info\n");
				goto cleanup_error;
			}

			width = info.xsize;
			height = info.ysize;

			// Validate dimensions
			if (width > IMAGE_MAX_DIMENSION || height > IMAGE_MAX_DIMENSION) {
				fprintf(stderr, "Error: JXL dimensions exceed maximum: %ux%u\n", width, height);
				goto cleanup_error;
			}

			if (width == 0 || height == 0) {
				fprintf(stderr, "Error: JXL has invalid dimensions: %ux%u\n", width, height);
				goto cleanup_error;
			}

			basic_info_received = true;
		} else if (status == JXL_DEC_FRAME) {
			// Start of new frame
			if (current_frame >= num_frames) {
				fprintf(stderr, "Error: JXL frame count mismatch: expected %d, got more\n", num_frames);
				goto cleanup_error;
			}
		} else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
			// Ensure we have basic info
			if (!basic_info_received) {
				fprintf(stderr, "Error: JXL buffer requested before basic info\n");
				goto cleanup_error;
			}

			// Allocate image_t for current frame
			frames[current_frame] = image_create(width, height);
			if (frames[current_frame] == NULL) {
				fprintf(stderr, "Error: Failed to create frame %d\n", current_frame);
				goto cleanup_error;
			}

			// Query buffer size
			size_t buffer_size;
			if (JxlDecoderImageOutBufferSize(dec, &format, &buffer_size) != JXL_DEC_SUCCESS) {
				fprintf(stderr, "Error: Failed to query JXL buffer size for frame %d\n", current_frame);
				goto cleanup_error;
			}

			// Verify buffer size
			if (buffer_size != width * height * 4) {
				fprintf(stderr, "Error: JXL buffer size mismatch for frame %d: expected %zu, got %zu\n", current_frame, (size_t)(width * height * 4), buffer_size);
				goto cleanup_error;
			}

			// Set output buffer
			if (JxlDecoderSetImageOutBuffer(dec, &format, frames[current_frame]->pixels, buffer_size) != JXL_DEC_SUCCESS) {
				fprintf(stderr, "Error: Failed to set JXL output buffer for frame %d\n", current_frame);
				goto cleanup_error;
			}
		} else if (status == JXL_DEC_FULL_IMAGE) {
			// Current frame complete
			current_frame++;
		} else if (status == JXL_DEC_SUCCESS) {
			// All frames decoded
			break;
		} else if (status == JXL_DEC_ERROR) {
			fprintf(stderr, "Error: JXL decoder error\n");
			goto cleanup_error;
		} else {
			fprintf(stderr, "Error: Unexpected JXL decoder status: %d\n", status);
			goto cleanup_error;
		}
	}

	// Cleanup decoder
	JxlDecoderDestroy(dec);

	// Verify we got all frames
	if (current_frame != num_frames) {
		fprintf(stderr, "Error: JXL frame count mismatch: expected %d, got %d\n", num_frames, current_frame);
		// Cleanup allocated frames
		for (int i = 0; i < num_frames; i++) {
			if (frames[i] != NULL) {
				image_destroy(frames[i]);
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
			image_destroy(frames[i]);
		}
	}
	free(frames);
	return NULL;
}

image_t **decode_jxl(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_jxl\n");
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
	if (num_frames == 1) {
		return decode_jxl_static(data, len, frame_count);
	} else {
		return decode_jxl_animated(data, len, frame_count, num_frames);
	}
}
