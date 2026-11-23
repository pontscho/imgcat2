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
#include <jxl/thread_parallel_runner.h>
/* clang-format on */

#include "encoder.h"
#include "../core/log.h"

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
img2_status_t img2_encode_jxl(const img2_image_t *img, int quality, uint8_t **out_data, size_t *out_size)
{
	return img2_encode_jxl_opts(img, quality, NULL, out_data, out_size);
}

/** @brief Quality 0..100 -> libjxl distance: >= 95 lossless (0), else (100 - q) / 10 */
static float jxl_distance(int quality)
{
	return quality >= 95 ? 0.0f : (100.0f - (float)quality) / 10.0f;
}

/** @brief Destroy the encoder and, if one was created, the thread pool runner */
static void jxl_encoder_destroy(JxlEncoder *encoder, void *runner)
{
	JxlEncoderDestroy(encoder);
	if (runner != NULL) {
		JxlThreadParallelRunnerDestroy(runner);
	}
}

/**
 * @brief img2_encode_jxl() with encode options (NULL = defaults)
 *
 * lossless (or quality >= 95): JxlEncoderSetFrameLossless; effort e: JXL_ENC_FRAME_SETTING_EFFORT e (default 7);
 * alpha_quality: distance of the alpha extra channel (same mapping as quality); threads >= 2: a thread pool
 * runner with that many workers (default and threads 1: single-threaded, no runner). chroma (XYB / RGB, no
 * subsampling) and progressive: one WARN each; alpha_quality with lossless: one WARN.
 */
img2_status_t img2_encode_jxl_opts(const img2_image_t *img, int quality, const img2_encode_opts_t *opts, uint8_t **out_data, size_t *out_size)
{
	if (img == NULL || out_data == NULL || out_size == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to encode_jxl");
		return IMG2_E_ARG;
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

	const img2_encode_opts_t *o = img2_encode_opts_or_default(opts);

	/* Determine if lossless mode should be used */
	int use_lossless = o->lossless || (jxl_quality >= 95);

	/* Map quality to distance for lossy mode */
	float distance = 0.0f;
	if (!use_lossless) {
		distance = jxl_distance(jxl_quality);
	}

	if (o->chroma != IMG2_CHROMA_DEFAULT) {
		char option[16];
		snprintf(option, sizeof(option), "chroma %s", img2_chroma_name(o->chroma));
		img2_encode_warn_ignored(option, "JXL", "no chroma subsampling of RGB / XYB");
	}
	if (o->progressive) {
		img2_encode_warn_ignored("progressive", "JXL", "not mapped");
	}
	if (use_lossless && o->alpha_quality > 0) {
		img2_encode_warn_ignored("alpha_quality", "lossless JXL", "alpha is lossless");
	}

	/* Create encoder */
	JxlEncoder *encoder = JxlEncoderCreate(NULL);
	if (encoder == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create JXL encoder");
		return IMG2_E_CODEC;
	}

	/* Thread pool (threads >= 2 only: the default stays single-threaded) */
	void *runner = NULL;
	if (o->threads >= 2) {
		runner = JxlThreadParallelRunnerCreate(NULL, (size_t)o->threads);
		if (runner == NULL || JxlEncoderSetParallelRunner(encoder, JxlThreadParallelRunner, runner) != JXL_ENC_SUCCESS) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to set up the JXL thread pool");
			jxl_encoder_destroy(encoder, runner);
			return IMG2_E_CODEC;
		}
	}

	/* Get frame settings */
	JxlEncoderFrameSettings *settings = JxlEncoderFrameSettingsCreate(encoder, NULL);
	if (settings == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create JXL frame settings");
		jxl_encoder_destroy(encoder, runner);
		return IMG2_E_CODEC;
	}

	/* Set basic info */
	JxlBasicInfo basic_info;
	JxlEncoderInitBasicInfo(&basic_info);
	basic_info.xsize = img->width;
	basic_info.ysize = img->height;
	basic_info.bits_per_sample = 8;
	basic_info.exponent_bits_per_sample = 0;
	/* Lossless requires the original (non-XYB) color space; libjxl rejects
	 * JxlEncoderSetFrameLossless() otherwise */
	basic_info.uses_original_profile = use_lossless ? JXL_TRUE : JXL_FALSE;
	basic_info.num_color_channels = 3;
	basic_info.num_extra_channels = 1; /* Alpha channel */
	basic_info.alpha_bits = 8;
	basic_info.alpha_exponent_bits = 0;
	basic_info.alpha_premultiplied = JXL_FALSE;

	if (JxlEncoderSetBasicInfo(encoder, &basic_info) != JXL_ENC_SUCCESS) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to set JXL basic info");
		jxl_encoder_destroy(encoder, runner);
		return IMG2_E_CODEC;
	}

	/* Set color encoding to sRGB */
	JxlColorEncoding color_encoding;
	JxlColorEncodingSetToSRGB(&color_encoding, JXL_FALSE);
	if (JxlEncoderSetColorEncoding(encoder, &color_encoding) != JXL_ENC_SUCCESS) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to set JXL color encoding");
		jxl_encoder_destroy(encoder, runner);
		return IMG2_E_CODEC;
	}

	/* Set lossless or distance */
	if (use_lossless) {
		if (JxlEncoderSetFrameLossless(settings, JXL_TRUE) != JXL_ENC_SUCCESS) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to set JXL lossless mode");
			jxl_encoder_destroy(encoder, runner);
			return IMG2_E_CODEC;
		}
	} else {
		if (JxlEncoderSetFrameDistance(settings, distance) != JXL_ENC_SUCCESS) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to set JXL frame distance");
			jxl_encoder_destroy(encoder, runner);
			return IMG2_E_CODEC;
		}
		if (o->alpha_quality > 0 && JxlEncoderSetExtraChannelDistance(settings, 0, jxl_distance(o->alpha_quality)) != JXL_ENC_SUCCESS) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to set JXL alpha distance");
			jxl_encoder_destroy(encoder, runner);
			return IMG2_E_CODEC;
		}
	}
	if (o->effort > 0 && JxlEncoderFrameSettingsSetOption(settings, JXL_ENC_FRAME_SETTING_EFFORT, o->effort) != JXL_ENC_SUCCESS) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to set JXL effort");
		jxl_encoder_destroy(encoder, runner);
		return IMG2_E_CODEC;
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
		img2_log(IMG2_LOG_ERROR, "Error: Failed to add JXL image frame");
		jxl_encoder_destroy(encoder, runner);
		return IMG2_E_CODEC;
	}

	/* Close input */
	JxlEncoderCloseInput(encoder);

	/* Process output */
	size_t buffer_capacity = 65536; /* Initial buffer: 64KB */
	uint8_t *buffer = (uint8_t *)malloc(buffer_capacity);
	if (buffer == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate JXL output buffer");
		jxl_encoder_destroy(encoder, runner);
		return IMG2_E_NOMEM;
	}

	size_t buffer_size = 0;
	uint8_t *next_out = buffer;
	size_t avail_out = buffer_capacity;

	while (1) {
		JxlEncoderStatus status = JxlEncoderProcessOutput(encoder, &next_out, &avail_out);

		if (status == JXL_ENC_NEED_MORE_OUTPUT) {
			/* Buffer is full, resize it (bytes written so far = next_out - buffer;
			 * avail_out only covers the space handed out since the last resize) */
			buffer_size = (size_t)(next_out - buffer);

			size_t new_capacity = buffer_capacity * 2;
			uint8_t *new_buffer = (uint8_t *)realloc(buffer, new_capacity);
			if (new_buffer == NULL) {
				img2_log(IMG2_LOG_ERROR, "Error: Failed to reallocate JXL output buffer");
				free(buffer);
				jxl_encoder_destroy(encoder, runner);
				return IMG2_E_NOMEM;
			}

			buffer = new_buffer;
			next_out = buffer + buffer_size;
			avail_out = new_capacity - buffer_size;
			buffer_capacity = new_capacity;
		} else if (status == JXL_ENC_SUCCESS) {
			/* Encoding complete */
			buffer_size = (size_t)(next_out - buffer);
			break;
		} else {
			/* Error occurred */
			img2_log(IMG2_LOG_ERROR, "Error: JXL encoder failed during processing");
			free(buffer);
			jxl_encoder_destroy(encoder, runner);
			return IMG2_E_CODEC;
		}
	}

	/* Cleanup encoder */
	jxl_encoder_destroy(encoder, runner);

	/* Allocate and copy output data */
	if (buffer_size == 0) {
		img2_log(IMG2_LOG_ERROR, "Error: JXL encoder produced no output");
		free(buffer);
		return IMG2_E_CODEC;
	}

	*out_data = (uint8_t *)malloc(buffer_size);
	if (*out_data == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate output buffer (%zu bytes)", buffer_size);
		free(buffer);
		return IMG2_E_NOMEM;
	}

	memcpy(*out_data, buffer, buffer_size);
	*out_size = buffer_size;

	/* Free encoder buffer */
	free(buffer);

	return IMG2_OK;
}
