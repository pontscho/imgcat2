/**
 * @file encoder_jxl.c
 * @brief JPEG XL encoder implementation using libjxl
 *
 * Encodes RGBA8888 images to JXL format using libjxl.
 * Supports both lossy and lossless compression modes with quality-to-distance mapping.
 */

/* clang-format off */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jxl/encode.h>
/* clang-format on */

#include "../encoders/encoder.h"

/**
 * @brief Encode RGBA image to JXL format
 *
 * Encodes RGBA8888 image to JPEG XL format with quality control.
 * Maps quality parameter to JXL distance parameter:
 * - quality 0-94: lossy compression (distance = (100 - quality) / 10.0)
 * - quality >= 95: lossless compression
 *
 * @param img Input image in RGBA8888 format
 * @param quality JXL quality level (0-100, higher = better quality, larger file)
 *                Quality >= 95 triggers lossless mode
 * @param out_data Output parameter for encoded data (caller must free)
 * @param out_size Output parameter for encoded data size in bytes
 * @return 0 on success, -1 on error
 *
 * @note JXL preserves alpha channel (supports transparency)
 * @note Caller must free *out_data with free() when done
 */
int encode_jxl(const image_t *img, int quality, uint8_t **out_data, size_t *out_size)
{
	if (img == NULL || out_data == NULL || out_size == NULL) {
		fprintf(stderr, "Error: Invalid parameters to encode_jxl\n");
		return -1;
	}

	/* Initialize outputs */
	*out_data = NULL;
	*out_size = 0;

	/* Clamp quality to valid range [0, 100] */
	int jxl_quality = quality;
	if (jxl_quality < 0) {
		jxl_quality = 0;
	}
	if (jxl_quality > 100) {
		jxl_quality = 100;
	}

	/* Determine if lossless mode should be used */
	int use_lossless = (jxl_quality >= 95);

	/* Map quality to distance for lossy mode */
	float distance = 0.0f;
	if (!use_lossless) {
		distance = (100.0f - (float)jxl_quality) / 10.0f;
	}

	/* Create encoder */
	JxlEncoder *encoder = JxlEncoderCreate(NULL);
	if (encoder == NULL) {
		fprintf(stderr, "Error: Failed to create JXL encoder\n");
		return -1;
	}

	/* Get frame settings */
	JxlEncoderFrameSettings *settings = JxlEncoderFrameSettingsCreate(encoder, NULL);
	if (settings == NULL) {
		fprintf(stderr, "Error: Failed to create JXL frame settings\n");
		JxlEncoderDestroy(encoder);
		return -1;
	}

	/* Set basic info */
	JxlBasicInfo basic_info;
	JxlEncoderInitBasicInfo(&basic_info);
	basic_info.xsize = img->width;
	basic_info.ysize = img->height;
	basic_info.bits_per_sample = 8;
	basic_info.exponent_bits_per_sample = 0;
	basic_info.uses_original_profile = JXL_FALSE;
	basic_info.num_color_channels = 3;
	basic_info.num_extra_channels = 1; /* Alpha channel */
	basic_info.alpha_bits = 8;
	basic_info.alpha_exponent_bits = 0;
	basic_info.alpha_premultiplied = JXL_FALSE;

	if (JxlEncoderSetBasicInfo(encoder, &basic_info) != JXL_ENC_SUCCESS) {
		fprintf(stderr, "Error: Failed to set JXL basic info\n");
		JxlEncoderDestroy(encoder);
		return -1;
	}

	/* Set color encoding to sRGB */
	JxlColorEncoding color_encoding;
	JxlColorEncodingSetToSRGB(&color_encoding, JXL_FALSE);
	if (JxlEncoderSetColorEncoding(encoder, &color_encoding) != JXL_ENC_SUCCESS) {
		fprintf(stderr, "Error: Failed to set JXL color encoding\n");
		JxlEncoderDestroy(encoder);
		return -1;
	}

	/* Set lossless or distance */
	if (use_lossless) {
		if (JxlEncoderSetFrameLossless(settings, JXL_TRUE) != JXL_ENC_SUCCESS) {
			fprintf(stderr, "Error: Failed to set JXL lossless mode\n");
			JxlEncoderDestroy(encoder);
			return -1;
		}
	} else {
		if (JxlEncoderSetFrameDistance(settings, distance) != JXL_ENC_SUCCESS) {
			fprintf(stderr, "Error: Failed to set JXL frame distance\n");
			JxlEncoderDestroy(encoder);
			return -1;
		}
	}

	/* Setup pixel format */
	JxlPixelFormat format;
	format.num_channels = 4; /* RGBA */
	format.data_type = JXL_TYPE_UINT8;
	format.endianness = JXL_NATIVE_ENDIAN;
	format.align = 0;

	/* Add image frame */
	size_t pixel_size = (size_t)img->width * (size_t)img->height * 4;
	if (JxlEncoderAddImageFrame(settings, &format, img->pixels, pixel_size) != JXL_ENC_SUCCESS) {
		fprintf(stderr, "Error: Failed to add JXL image frame\n");
		JxlEncoderDestroy(encoder);
		return -1;
	}

	/* Close input */
	JxlEncoderCloseInput(encoder);

	/* Process output */
	size_t buffer_capacity = 65536; /* Initial buffer: 64KB */
	uint8_t *buffer = (uint8_t *)malloc(buffer_capacity);
	if (buffer == NULL) {
		fprintf(stderr, "Error: Failed to allocate JXL output buffer\n");
		JxlEncoderDestroy(encoder);
		return -1;
	}

	size_t buffer_size = 0;
	uint8_t *next_out = buffer;
	size_t avail_out = buffer_capacity;

	while (1) {
		JxlEncoderStatus status = JxlEncoderProcessOutput(encoder, &next_out, &avail_out);

		if (status == JXL_ENC_NEED_MORE_OUTPUT) {
			/* Buffer is full, resize it */
			size_t written = buffer_capacity - avail_out;
			buffer_size += written;

			size_t new_capacity = buffer_capacity * 2;
			uint8_t *new_buffer = (uint8_t *)realloc(buffer, new_capacity);
			if (new_buffer == NULL) {
				fprintf(stderr, "Error: Failed to reallocate JXL output buffer\n");
				free(buffer);
				JxlEncoderDestroy(encoder);
				return -1;
			}

			buffer = new_buffer;
			next_out = buffer + buffer_size;
			avail_out = new_capacity - buffer_size;
			buffer_capacity = new_capacity;
		} else if (status == JXL_ENC_SUCCESS) {
			/* Encoding complete */
			size_t written = buffer_capacity - avail_out;
			buffer_size += written;
			break;
		} else {
			/* Error occurred */
			fprintf(stderr, "Error: JXL encoder failed during processing\n");
			free(buffer);
			JxlEncoderDestroy(encoder);
			return -1;
		}
	}

	/* Cleanup encoder */
	JxlEncoderDestroy(encoder);

	/* Allocate and copy output data */
	if (buffer_size == 0) {
		fprintf(stderr, "Error: JXL encoder produced no output\n");
		free(buffer);
		return -1;
	}

	*out_data = (uint8_t *)malloc(buffer_size);
	if (*out_data == NULL) {
		fprintf(stderr, "Error: Failed to allocate output buffer (%zu bytes)\n", buffer_size);
		free(buffer);
		return -1;
	}

	memcpy(*out_data, buffer, buffer_size);
	*out_size = buffer_size;

	/* Free encoder buffer */
	free(buffer);

	return 0;
}
