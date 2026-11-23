/**
 * @file encoder_heif.c
 * @brief HEIF and AVIF encoder implementation using libheif
 *
 * Encodes RGBA8888 images to HEIF (HEVC) or AVIF (AV1) through one shared libheif path.
 * Preserves alpha channel (both support transparency).
 */

/* clang-format off */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libheif/heif.h>
/* clang-format on */

#include "encoder.h"
#include "../core/log.h"

/**
 * @brief HEIF memory write state structure
 *
 * Used by heif_memory_write_func() to accumulate HEIF output data in memory.
 */
typedef struct {
	uint8_t *buffer; /**< Output buffer (dynamically resized) */
	size_t size; /**< Current size of valid data in buffer */
	size_t capacity; /**< Total allocated capacity of buffer */
} heif_memory_write_state;

/**
 * @brief HEIF memory write callback
 *
 * Custom write function for libheif that writes HEIF data to a memory buffer
 * instead of a file. Automatically resizes the buffer as needed.
 *
 * @param ctx HEIF context (unused)
 * @param data Data to write
 * @param size Number of bytes to write
 * @param userdata Pointer to heif_memory_write_state
 * @return heif_error structure (heif_error_Ok on success)
 */
static struct heif_error heif_memory_write_func(struct heif_context *ctx, const void *data, size_t size, void *userdata)
{
	(void)ctx; /* Unused */

	heif_memory_write_state *state = (heif_memory_write_state *)userdata;

	if (state == NULL) {
		struct heif_error err = { heif_error_Usage_error, heif_suberror_Null_pointer_argument, "Write error: NULL state pointer" };
		return err;
	}

	/* Check if we need to resize buffer */
	if (state->size + size > state->capacity) {
		/* Double capacity until it fits */
		size_t new_capacity = state->capacity;
		if (new_capacity == 0) {
			new_capacity = 65536; /* Initial capacity: 64KB */
		}
		while (new_capacity < state->size + size) {
			new_capacity *= 2;
		}

		/* Reallocate buffer */
		uint8_t *new_buffer = (uint8_t *)realloc(state->buffer, new_capacity);
		if (new_buffer == NULL) {
			struct heif_error err = { heif_error_Memory_allocation_error, heif_suberror_Unspecified, "Write error: Failed to reallocate buffer" };
			return err;
		}

		state->buffer = new_buffer;
		state->capacity = new_capacity;
	}

	/* Append data to buffer */
	memcpy(state->buffer + state->size, data, size);
	state->size += size;

	struct heif_error success = { heif_error_Ok, heif_suberror_Unspecified, "Success" };
	return success;
}

/**
 * @brief HEIF (kvazaar): every option except quality is reported as ignored (one WARN each)
 */
static void heif_warn_options(const img2_encode_opts_t *o)
{
	if (o->lossless) {
		img2_encode_warn_ignored("lossless", "HEIF", "kvazaar lossless is 4:2:0 YCbCr, not RGB-lossless");
	}
	if (o->effort > 0) {
		img2_encode_warn_ignored("effort", "HEIF", "no kvazaar preset parameter");
	}
	if (o->chroma == IMG2_CHROMA_422 || o->chroma == IMG2_CHROMA_444) {
		char option[16];
		snprintf(option, sizeof(option), "chroma %s", img2_chroma_name(o->chroma));
		img2_encode_warn_ignored(option, "HEIF", "always 4:2:0");
	}
	if (o->alpha_quality > 0) {
		img2_encode_warn_ignored("alpha_quality", "HEIF", "no kvazaar alpha quality parameter");
	}
	if (o->progressive) {
		img2_encode_warn_ignored("progressive", "HEIF", "no progressive mode");
	}
	if (o->threads > 0) {
		img2_encode_warn_ignored("threads", "HEIF", "kvazaar chooses the thread count");
	}
}

/**
 * @brief AVIF (aom): set the encoder parameters of the options (defaults: nothing set, libheif's aom defaults)
 *
 * lossless: aom lossless, chroma 444 and (through @p nclx) the identity matrix (GBR planes), so RGB survives
 * bit-exactly; effort e: speed 10 - e clamped 0..9; chroma 420/422/444; alpha_quality; threads.
 * progressive, and with lossless chroma other than 444 or alpha_quality: one WARN each.
 *
 * @param nclx Output: allocated identity-matrix profile for lossless (caller frees), else NULL
 * @return heif_error of the first failing call
 */
static void avif_keep_first_error(struct heif_error *first, struct heif_error err)
{
	if (first->code == heif_error_Ok) {
		*first = err;
	}
}

static struct heif_error avif_set_options(struct heif_encoder *encoder, const img2_encode_opts_t *o, struct heif_color_profile_nclx **nclx)
{
	struct heif_error err = { heif_error_Ok, heif_suberror_Unspecified, "Success" };
	*nclx = NULL;
	if (o->progressive) {
		img2_encode_warn_ignored("progressive", "AVIF", "no progressive mode");
	}
	if (o->lossless) {
		if (o->chroma == IMG2_CHROMA_420 || o->chroma == IMG2_CHROMA_422) {
			char option[16];
			snprintf(option, sizeof(option), "chroma %s", img2_chroma_name(o->chroma));
			img2_encode_warn_ignored(option, "lossless AVIF", "always 4:4:4");
		}
		if (o->alpha_quality > 0) {
			img2_encode_warn_ignored("alpha_quality", "lossless AVIF", "alpha is lossless");
		}
		avif_keep_first_error(&err, heif_encoder_set_lossless(encoder, 1));
		avif_keep_first_error(&err, heif_encoder_set_parameter_string(encoder, "chroma", "444"));
		*nclx = heif_nclx_color_profile_alloc(); /* C++ new: throws, never NULL */
		(*nclx)->color_primaries = heif_color_primaries_ITU_R_BT_709_5;
		(*nclx)->transfer_characteristics = heif_transfer_characteristic_IEC_61966_2_1;
		(*nclx)->matrix_coefficients = heif_matrix_coefficients_RGB_GBR;
		(*nclx)->full_range_flag = 1;
	} else {
		if (o->chroma != IMG2_CHROMA_DEFAULT) {
			avif_keep_first_error(&err, heif_encoder_set_parameter_string(encoder, "chroma", img2_chroma_name(o->chroma)));
		}
		if (o->alpha_quality > 0) {
			avif_keep_first_error(&err, heif_encoder_set_parameter_integer(encoder, "alpha-quality", o->alpha_quality));
		}
	}
	if (o->effort > 0) {
		/* effort 1..10 (validated) -> speed 9..0, inside aom's 0..9 */
		avif_keep_first_error(&err, heif_encoder_set_parameter_integer(encoder, "speed", 10 - o->effort));
	}
	if (o->threads > 0) {
		avif_keep_first_error(&err, heif_encoder_set_parameter_integer(encoder, "threads", o->threads));
	}
	return err;
}

/**
 * @brief Encode an RGBA image into a HEIF container with the given libheif compression format
 *
 * Shared by the HEIF (HEVC, kvazaar) and AVIF (AV1, aom) encoders: the libheif encoder is taken with its
 * default parameters, the lossy quality is set, and for AVIF the options (avif_set_options()). Preserves the
 * alpha channel.
 *
 * @param img Input image in RGBA8888 format
 * @param quality Quality level (0-100, higher = better quality, larger file)
 * @param opts Encode options (resolved, not NULL)
 * @param compression heif_compression_HEVC or heif_compression_AV1
 * @param label Format name for messages ("HEIF" / "AVIF")
 * @param codec Codec name for messages ("HEVC" / "AV1")
 * @param fn Function name for the invalid-parameter message
 * @param out_data Output parameter for encoded data (caller must free)
 * @param out_size Output parameter for encoded data size in bytes
 * @return IMG2_OK; IMG2_E_ARG, IMG2_E_NOMEM, IMG2_E_CODEC
 */
static img2_status_t heif_encode_with(const img2_image_t *img, int quality, const img2_encode_opts_t *opts, enum heif_compression_format compression, const char *label, const char *codec, const char *fn, uint8_t **out_data, size_t *out_size)
{
	if (img == NULL || out_data == NULL || out_size == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to %s", fn);
		return IMG2_E_ARG;
	}

	/* Initialize outputs */
	*out_data = NULL;
	*out_size = 0;

	const bool avif = compression == heif_compression_AV1;
	if (!avif) {
		heif_warn_options(opts);
	}

	/* Clamp quality to valid range [0, 100] */
	int heif_quality = quality;
	if (heif_quality < 0) {
		heif_quality = 0;
	}
	if (heif_quality > 100) {
		heif_quality = 100;
	}

	/* Create HEIF context */
	struct heif_context *ctx = heif_context_alloc();
	if (ctx == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate HEIF context");
		return IMG2_E_NOMEM;
	}

	/* Create HEIF image from RGBA8888 */
	struct heif_image *heif_img = NULL;
	struct heif_error err = heif_image_create(img->width, img->height, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, &heif_img);

	if (err.code != heif_error_Ok) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create HEIF image: %s", err.message);
		heif_context_free(ctx);
		return IMG2_E_CODEC;
	}

	/* Add image plane */
	err = heif_image_add_plane(heif_img, heif_channel_interleaved, img->width, img->height, 8);
	if (err.code != heif_error_Ok) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to add HEIF image plane: %s", err.message);
		heif_image_release(heif_img);
		heif_context_free(ctx);
		return IMG2_E_CODEC;
	}

	/* Get plane pointer and stride */
	int stride = 0;
	uint8_t *plane = heif_image_get_plane(heif_img, heif_channel_interleaved, &stride);
	if (plane == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to get HEIF image plane");
		heif_image_release(heif_img);
		heif_context_free(ctx);
		return IMG2_E_CODEC;
	}

	/* Copy pixel data row-by-row (handle stride) */
	for (uint32_t y = 0; y < img->height; y++) {
		const uint8_t *src_row = img->pixels + y * img->width * 4;
		uint8_t *dst_row = plane + y * stride;
		memcpy(dst_row, src_row, img->width * 4);
	}

	/* Get the HEVC / AV1 encoder (highest-priority plugin, default parameters) */
	struct heif_encoder *encoder = NULL;
	err = heif_context_get_encoder_for_format(ctx, compression, &encoder);
	if (err.code != heif_error_Ok || encoder == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: No %s encoder available: %s", codec, err.message);
		heif_encoder_release(encoder); /* a plugin's new_encoder error comes with the encoder already created */
		heif_image_release(heif_img);
		heif_context_free(ctx);
		return IMG2_E_CODEC;
	}

	/* Set quality */
	err = heif_encoder_set_lossy_quality(encoder, heif_quality);
	if (err.code != heif_error_Ok) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to set %s quality: %s", label, err.message);
		heif_encoder_release(encoder);
		heif_image_release(heif_img);
		heif_context_free(ctx);
		return IMG2_E_CODEC;
	}

	/* AVIF options (lossless: an identity-matrix NCLX in the encoding options) */
	struct heif_color_profile_nclx *nclx = NULL;
	struct heif_encoding_options *enc_opts = NULL;
	if (avif) {
		err = avif_set_options(encoder, opts, &nclx);
		if (err.code != heif_error_Ok) {
			/* aom accepts every value set here; another AV1 plugin of a host libheif may not */
			img2_log(IMG2_LOG_ERROR, "Error: Failed to set %s options: %s", label, err.message);
			heif_nclx_color_profile_free(nclx);
			heif_encoder_release(encoder);
			heif_image_release(heif_img);
			heif_context_free(ctx);
			return IMG2_E_CODEC;
		}
		if (nclx != NULL) {
			enc_opts = heif_encoding_options_alloc(); /* C++ new: throws, never NULL */
			enc_opts->output_nclx_profile = nclx;
		}
	}

	/* Encode image */
	struct heif_image_handle *handle = NULL;
	err = heif_context_encode_image(ctx, heif_img, encoder, enc_opts, &handle);
	heif_encoding_options_free(enc_opts);
	heif_nclx_color_profile_free(nclx);
	if (err.code != heif_error_Ok) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to encode %s image: %s", label, err.message);
		heif_encoder_release(encoder);
		heif_image_release(heif_img);
		heif_context_free(ctx);
		return IMG2_E_CODEC;
	}

	/* Release encoder (no longer needed) */
	heif_encoder_release(encoder);

	/* Initialize memory write state */
	heif_memory_write_state write_state;
	write_state.buffer = NULL;
	write_state.size = 0;
	write_state.capacity = 0;

	/* Setup heif_writer with our memory write callback */
	struct heif_writer writer;
	writer.writer_api_version = 1;
	writer.write = heif_memory_write_func;

	/* Write to memory using our callback */
	err = heif_context_write(ctx, &writer, &write_state);
	if (err.code != heif_error_Ok) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to write HEIF to memory: %s", err.message);
		heif_image_handle_release(handle);
		heif_image_release(heif_img);
		heif_context_free(ctx);
		if (write_state.buffer != NULL) {
			free(write_state.buffer);
		}
		return IMG2_E_NOMEM;
	}

	/* Cleanup HEIF structures */
	heif_image_handle_release(handle);
	heif_image_release(heif_img);
	heif_context_free(ctx);

	/* Check output */
	if (write_state.buffer == NULL || write_state.size == 0) {
		img2_log(IMG2_LOG_ERROR, "Error: %s encoder produced no output", label);
		if (write_state.buffer != NULL) {
			free(write_state.buffer);
		}
		return IMG2_E_CODEC;
	}

	/* Allocate and copy output data */
	*out_data = (uint8_t *)malloc(write_state.size);
	if (*out_data == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate output buffer (%zu bytes)", write_state.size);
		free(write_state.buffer);
		return IMG2_E_NOMEM;
	}

	memcpy(*out_data, write_state.buffer, write_state.size);
	*out_size = write_state.size;

	/* Free write state buffer */
	free(write_state.buffer);

	return IMG2_OK;
}

/**
 * @brief Encode RGBA image to HEIF (HEVC, quality 0-100); the encoder must be available in libheif
 */
img2_status_t img2_encode_heif(const img2_image_t *img, int quality, uint8_t **out_data, size_t *out_size)
{
	return img2_encode_heif_opts(img, quality, NULL, out_data, out_size);
}

/** @brief img2_encode_heif() with encode options (NULL = defaults; kvazaar honours none of them: one WARN each) */
img2_status_t img2_encode_heif_opts(const img2_image_t *img, int quality, const img2_encode_opts_t *opts, uint8_t **out_data, size_t *out_size)
{
	return heif_encode_with(img, quality, img2_encode_opts_or_default(opts), heif_compression_HEVC, "HEIF", "HEVC", "encode_heif", out_data, out_size);
}

/**
 * @brief Encode RGBA image to AVIF (AV1 in HEIF, quality 0-100); the encoder must be available in libheif
 */
img2_status_t img2_encode_avif(const img2_image_t *img, int quality, uint8_t **out_data, size_t *out_size)
{
	return img2_encode_avif_opts(img, quality, NULL, out_data, out_size);
}

/** @brief img2_encode_avif() with encode options (NULL = defaults; mapping: avif_set_options()) */
img2_status_t img2_encode_avif_opts(const img2_image_t *img, int quality, const img2_encode_opts_t *opts, uint8_t **out_data, size_t *out_size)
{
	return heif_encode_with(img, quality, img2_encode_opts_or_default(opts), heif_compression_AV1, "AVIF", "AV1", "encode_avif", out_data, out_size);
}

bool img2_encode_heif_available(void)
{
	return heif_have_encoder_for_format(heif_compression_HEVC) != 0;
}

bool img2_encode_avif_available(void)
{
	return heif_have_encoder_for_format(heif_compression_AV1) != 0;
}
