/**
 * @file api.c
 * @brief Public API facade: thin wrappers over the internal decoder / encoder / image modules
 *
 * Every boundary check added here ([SM1]/[SM2]) logs only at IMG2_LOG_DEBUG, so the
 * imgcat2 CLI (which never trips them) keeps its output byte-identical.
 */

#include <stdlib.h>

#include "../img2_internal.h"
#include "../decoders/decoder.h"
#include "../encoders/encoder.h"
#include "image.h"

void img2_free(void *ptr)
{
	free(ptr);
}

const char *img2_version_string(void)
{
	return IMG2_VERSION_STRING;
}

img2_status_t img2_decode(const uint8_t *buf, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out)
{
	/* [SM1] before touching buf */
	if (out == NULL) {
		img2_log(IMG2_LOG_DEBUG, "img2_decode: out is NULL");
		return IMG2_E_ARG;
	}
	out->frames = NULL;
	out->count = 0;
	out->loop_count = 0;
	if (buf == NULL || len == 0) {
		img2_log(IMG2_LOG_DEBUG, "img2_decode: empty input");
		return IMG2_E_ARG;
	}
	if (len > IMG2_MAX_FILE_SIZE) {
		img2_log(IMG2_LOG_DEBUG, "img2_decode: input of %zu bytes exceeds IMG2_MAX_FILE_SIZE", len);
		return IMG2_E_LIMIT;
	}
	/* [F21] the JPEG decoder treats any non-zero value as "on": only the documented values are accepted */
	if (opts != NULL && (opts->jpeg_deblock < -1 || opts->jpeg_deblock > 1)) {
		img2_log(IMG2_LOG_DEBUG, "img2_decode: jpeg_deblock %d is not -1, 0 or 1", opts->jpeg_deblock);
		return IMG2_E_ARG;
	}

	img2_mime_t mime = (opts != NULL && opts->force_mime != IMG2_MIME_UNKNOWN) ? opts->force_mime : img2_detect_mime(buf, len);
	return img2_decoder_dispatch(opts, buf, len, mime, out);
}

img2_status_t img2_encode(const img2_frames_t *in, img2_format_t fmt, const img2_encode_opts_t *opts, uint8_t **out_buf, size_t *out_len)
{
	if (out_buf != NULL) {
		*out_buf = NULL;
	}
	if (out_len != NULL) {
		*out_len = 0;
	}
	if (in == NULL || in->count < 1 || out_buf == NULL || out_len == NULL) {
		img2_log(IMG2_LOG_DEBUG, "img2_encode: invalid arguments");
		return IMG2_E_ARG;
	}
	if (img2_frames_validate(in) != IMG2_OK) { /* [SM2] */
		img2_log(IMG2_LOG_DEBUG, "img2_encode: invalid input frames");
		return IMG2_E_ARG;
	}

	/* [R2] before quality is touched */
	if (fmt == IMG2_FORMAT_NONE) {
		img2_log(IMG2_LOG_DEBUG, "img2_encode: IMG2_FORMAT_NONE");
		return IMG2_E_ARG;
	}
	if (opts != NULL) {
		if (opts->effort < 0 || opts->effort > 10) {
			img2_log(IMG2_LOG_DEBUG, "img2_encode: effort %d is not 0..10", opts->effort);
			return IMG2_E_ARG;
		}
		if (opts->alpha_quality < 0 || opts->alpha_quality > 100) {
			img2_log(IMG2_LOG_DEBUG, "img2_encode: alpha_quality %d is not 0..100", opts->alpha_quality);
			return IMG2_E_ARG;
		}
		if (opts->threads < 0 || opts->threads > 64) {
			img2_log(IMG2_LOG_DEBUG, "img2_encode: threads %d is not 0..64", opts->threads);
			return IMG2_E_ARG;
		}
		if ((unsigned)opts->chroma > (unsigned)IMG2_CHROMA_444) {
			img2_log(IMG2_LOG_DEBUG, "img2_encode: chroma %d is not an img2_chroma_t", (int)opts->chroma);
			return IMG2_E_ARG;
		}
		if ((opts->metadata & ~(unsigned)(IMG2_META_EXIF | IMG2_META_XMP)) != 0) {
			img2_log(IMG2_LOG_DEBUG, "img2_encode: unknown metadata bits 0x%x", opts->metadata);
			return IMG2_E_ARG;
		}
	}
	const img2_encoder_entry_t *entry = img2_encoder_find(fmt);
	if (entry == NULL) {
		img2_encoder_log_missing(fmt); /* [P9] same lines as the dispatcher's miss path */
		return IMG2_E_UNSUPPORTED;
	}

	/* -1 = the encoder's default (already on its native scale); PNG maps 0..100 to zlib level 0..9 */
	int native;
	if (opts == NULL || opts->quality < 0) {
		native = entry->default_quality;
	} else if (fmt == IMG2_FORMAT_PNG) {
		native = opts->quality / 10;
		if (native > 9) {
			native = 9;
		}
	} else {
		native = opts->quality;
	}

	/* opts->metadata: the encoders write the image's raw EXIF / XMP blocks (img2_encode_meta_prepare) */
	return img2_encoder_dispatch_opts(in->frames[0], fmt, native, opts, out_buf, out_len);
}

img2_status_t img2_apply_orientation(img2_image_t **img)
{
	if (img == NULL || *img == NULL || img2_image_validate(*img) != IMG2_OK) { /* [SM2] */
		img2_log(IMG2_LOG_DEBUG, "img2_apply_orientation: invalid image");
		return IMG2_E_ARG;
	}
	/* the orientation still to be applied (decoders: 1 for the self-orienting codecs); outside 2..8 = upright */
	const uint8_t o = (*img)->orientation;
	if (o < 2 || o > 8) {
		(*img)->orientation = 1;
		return IMG2_OK;
	}
	img2_status_t st = img2_image_orient(*img, o);
	if (st == IMG2_OK) {
		(*img)->orientation = 1;
	}
	return st;
}
