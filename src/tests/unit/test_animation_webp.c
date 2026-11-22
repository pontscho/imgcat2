/**
 * @file test_animation_webp.c
 * @brief Animated WebP through img2_decode (S13b): 200-frame clamp and the [F14] total pixel cap
 *
 * libwebpmux / WebPAnimEncoder are not built, so the animation container is assembled in-test:
 * RIFF/WEBP + VP8X (animation + alpha flags) + ANIM + one ANMF per frame wrapping the VP8L chunk of a
 * WebPEncodeLosslessRGBA 1x1 image (vendored libwebp in the fat archive).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <webp/encode.h>

#include "anim_test_util.h"

static void put_le24(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v & 0xFF);
	p[1] = (uint8_t)((v >> 8) & 0xFF);
	p[2] = (uint8_t)((v >> 16) & 0xFF);
}

static void put_le32(uint8_t *p, uint32_t v)
{
	put_le24(p, v);
	p[3] = (uint8_t)(v >> 24);
}

/* Chunk header + payload (+ pad byte for odd sizes) */
static void put_chunk(anim_buf_t *b, const char fourcc[4], const uint8_t *payload, size_t n)
{
	uint8_t hdr[8];
	memcpy(hdr, fourcc, 4);
	put_le32(hdr + 4, (uint32_t)n);
	anim_buf_put(b, hdr, sizeof(hdr));
	anim_buf_put(b, payload, n);
	if (n & 1) {
		static const uint8_t pad = 0;
		anim_buf_put(b, &pad, 1);
	}
}

/*
 * Animated WebP: canvas_w x canvas_h, nframes 1x1 frames at (0,0) of duration_ms each, loop count `loops`.
 * Returns a malloc'ed buffer (*len set) or NULL.
 */
static uint8_t *webp_build_anim(uint32_t canvas_w, uint32_t canvas_h, int nframes, uint32_t duration_ms, uint16_t loops, size_t *len)
{
	static const uint8_t rgba[4] = { 0x20, 0x40, 0x80, 0xFF };
	uint8_t *still = NULL;
	size_t still_len = WebPEncodeLosslessRGBA(rgba, 1, 1, 4, &still);
	/* still = RIFF <size> WEBP | VP8L <size> <bitstream> */
	if (still_len < 20 || still == NULL || memcmp(still + 12, "VP8L", 4) != 0) {
		WebPFree(still);
		return NULL;
	}
	const uint8_t *vp8l_chunk = still + 12;
	size_t vp8l_chunk_len = still_len - 12;

	anim_buf_t b = { 0 };
	static const uint8_t riff_hdr[12] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P' };
	anim_buf_put(&b, riff_hdr, sizeof(riff_hdr));

	uint8_t vp8x[10] = { 0x12, 0, 0, 0 }; /* ANIMATION (0x02) | ALPHA (0x10) */
	put_le24(vp8x + 4, canvas_w - 1);
	put_le24(vp8x + 7, canvas_h - 1);
	put_chunk(&b, "VP8X", vp8x, sizeof(vp8x));

	uint8_t anim[6] = { 0, 0, 0, 0, (uint8_t)(loops & 0xFF), (uint8_t)(loops >> 8) };
	put_chunk(&b, "ANIM", anim, sizeof(anim));

	size_t anmf_len = 16 + vp8l_chunk_len;
	uint8_t *anmf = malloc(anmf_len);
	if (anmf == NULL) {
		WebPFree(still);
		anim_buf_free(&b);
		return NULL;
	}
	memset(anmf, 0, 16);
	/* X/2, Y/2 = 0; width-1, height-1 = 0; duration; flags 0 (alpha-blend, no dispose) */
	put_le24(anmf + 12, duration_ms);
	memcpy(anmf + 16, vp8l_chunk, vp8l_chunk_len);
	for (int i = 0; i < nframes; i++) {
		put_chunk(&b, "ANMF", anmf, anmf_len);
	}
	free(anmf);
	WebPFree(still);

	if (b.failed) {
		anim_buf_free(&b);
		return NULL;
	}
	put_le32(b.data + 4, (uint32_t)(b.len - 8));
	*len = b.len;
	return b.data;
}

CTEST(anim_webp, synthetic_timing)
{
	/* sanity check of webp_build_anim: 3 frames of 40 ms, loop 2 */
	size_t len = 0;
	uint8_t *data = webp_build_anim(2, 2, 3, 40, 2, &len);
	ASSERT_NOT_NULL(data);
	anim_check_uniform(data, len, 3, 40u, 2);
	free(data);
}

CTEST(anim_webp, frame_cap_keeps_200_frames)
{
	/* 230 frames of 20 ms -> the first 200 (warning), loop count kept */
	size_t len = 0;
	uint8_t *data = webp_build_anim(1, 1, 230, 20, 0, &len);
	ASSERT_NOT_NULL(data);
	anim_check_uniform(data, len, 200, 20u, 0);
	free(data);
}

CTEST(anim_webp, total_pixels_is_limit)
{
	/* [F14] 17 frames on an 8192 x 8192 canvas (each within IMG2_MAX_PIXELS) = 1.14e9 > 2^30 total:
	 * rejected up front from the frame count, before any frame is decoded */
	size_t len = 0;
	uint8_t *data = webp_build_anim(8192, 8192, 17, 20, 0, &len);
	ASSERT_NOT_NULL(data);
	anim_check_status(data, len, IMG2_E_LIMIT);
	free(data);
}
