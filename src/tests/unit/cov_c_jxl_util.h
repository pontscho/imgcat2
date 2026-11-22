/**
 * @file cov_c_jxl_util.h
 * @brief Coverage lane C: in-test JPEG XL fixtures (vendored libjxl encoder) and an ISOBMFF container wrapper
 *
 * Header-only, test code only.
 */

#ifndef IMG2_TESTS_COV_C_JXL_UTIL_H
#define IMG2_TESTS_COV_C_JXL_UTIL_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <jxl/encode.h>

#include "cov_c_util.h"

typedef struct {
	uint32_t xsize, ysize;   /* canvas */
	int nframes;             /* > 1: animation */
	uint32_t tps_num, tps_den;
	uint32_t num_loops;
	uint32_t duration;       /* ticks per frame */
	int noise;               /* frame pixels from cov_fill_noise instead of a flat value */
} jx_params_t;

/* Lossless RGB codestream (effort 1), frames of the full canvas. Returns malloc'ed data or NULL. */
static inline uint8_t *jx_encode(const jx_params_t *p, size_t *len)
{
	JxlEncoder *enc = JxlEncoderCreate(NULL);
	if (enc == NULL) {
		return NULL;
	}
	uint8_t *out = NULL;
	uint8_t *pix = NULL;
	JxlBasicInfo bi;
	JxlEncoderInitBasicInfo(&bi);
	bi.xsize = p->xsize;
	bi.ysize = p->ysize;
	bi.bits_per_sample = 8;
	bi.uses_original_profile = JXL_TRUE;
	bi.num_color_channels = 3;
	if (p->nframes > 1) {
		bi.have_animation = JXL_TRUE;
		bi.animation.tps_numerator = p->tps_num;
		bi.animation.tps_denominator = p->tps_den;
		bi.animation.num_loops = p->num_loops;
	}
	JxlColorEncoding ce;
	JxlColorEncodingSetToSRGB(&ce, JXL_FALSE);
	JxlEncoderFrameSettings *fs = NULL;
	if (JxlEncoderSetBasicInfo(enc, &bi) != JXL_ENC_SUCCESS || JxlEncoderSetColorEncoding(enc, &ce) != JXL_ENC_SUCCESS ||
	    (fs = JxlEncoderFrameSettingsCreate(enc, NULL)) == NULL || JxlEncoderSetFrameLossless(fs, JXL_TRUE) != JXL_ENC_SUCCESS ||
	    JxlEncoderFrameSettingsSetOption(fs, JXL_ENC_FRAME_SETTING_EFFORT, 1) != JXL_ENC_SUCCESS) {
		goto fail;
	}
	JxlPixelFormat fmt = { 3, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0 };
	size_t pix_len = (size_t)p->xsize * p->ysize * 3;
	pix = malloc(pix_len);
	if (pix == NULL) {
		goto fail;
	}
	for (int i = 0; i < p->nframes; i++) {
		JxlFrameHeader fh;
		JxlEncoderInitFrameHeader(&fh);
		fh.duration = p->duration;
		if (JxlEncoderSetFrameHeader(fs, &fh) != JXL_ENC_SUCCESS) {
			goto fail;
		}
		if (p->noise) {
			cov_fill_noise(pix, pix_len, 99u + (uint32_t)i);
		} else {
			memset(pix, (i * 37 + 10) & 0xFF, pix_len);
		}
		if (JxlEncoderAddImageFrame(fs, &fmt, pix, pix_len) != JXL_ENC_SUCCESS) {
			goto fail;
		}
	}
	JxlEncoderCloseInput(enc);
	size_t cap = 4096;
	size_t used = 0;
	out = malloc(cap);
	while (out != NULL) {
		uint8_t *next = out + used;
		size_t avail = cap - used;
		JxlEncoderStatus s = JxlEncoderProcessOutput(enc, &next, &avail);
		used = (size_t)(next - out);
		if (s == JXL_ENC_SUCCESS) {
			free(pix);
			JxlEncoderDestroy(enc);
			*len = used;
			return out;
		}
		if (s != JXL_ENC_NEED_MORE_OUTPUT) {
			break;
		}
		uint8_t *grown = realloc(out, cap * 2);
		if (grown == NULL) {
			break;
		}
		out = grown;
		cap *= 2;
	}
fail:
	free(pix);
	free(out);
	JxlEncoderDestroy(enc);
	return NULL;
}

/* Growable byte buffer for jx_box() / jx_container() */
typedef struct {
	uint8_t *d;
	size_t len;
} jx_buf_t;

static inline void jx_raw(jx_buf_t *b, const void *data, size_t n)
{
	uint8_t *p = realloc(b->d, b->len + n);
	if (p == NULL) {
		abort();
	}
	memcpy(p + b->len, data, n);
	b->d = p;
	b->len += n;
}

/* ISOBMFF box: 32-bit size, 4cc, payload */
static inline void jx_box(jx_buf_t *b, const char *type, const void *data, size_t n)
{
	uint8_t hdr[8];
	cov_put_be32(hdr, (uint32_t)(n + 8));
	memcpy(hdr + 4, type, 4);
	jx_raw(b, hdr, 8);
	jx_raw(b, data, n);
}

/* Container start: signature box and ftyp */
static inline void jx_container_start(jx_buf_t *b)
{
	static const uint8_t sig[12] = { 0, 0, 0, 12, 'J', 'X', 'L', ' ', 0x0D, 0x0A, 0x87, 0x0A };
	static const uint8_t ftyp[12] = { 'j', 'x', 'l', ' ', 0, 0, 0, 0, 'j', 'x', 'l', ' ' };
	jx_raw(b, sig, sizeof(sig));
	jx_box(b, "ftyp", ftyp, sizeof(ftyp));
}

#endif /* IMG2_TESTS_COV_C_JXL_UTIL_H */
