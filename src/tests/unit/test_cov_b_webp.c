/**
 * @file test_cov_b_webp.c
 * @brief WebP decoder coverage (decoder_webp.c): EXIF / XMP chunk variants, single-frame and zero-duration
 *        animations, undecodable input
 *
 * Containers are assembled in-test around the VP8L chunk of a WebPEncodeLosslessRGBA 1x1 image (libwebpmux
 * is not built), as in test_animation_webp.c.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <img2/metadata.h>
#include <webp/encode.h>

#include "../../libimg2/decoders/decoder.h"
#include "test_cov_b_util.h"

#define VP8X_ANIM 0x02
#define VP8X_XMP 0x04
#define VP8X_EXIF 0x08
#define VP8X_ALPHA 0x10

static void webp_chunk(cb_buf_t *b, const char fourcc[4], const void *payload, size_t n)
{
	cb_put(b, fourcc, 4);
	cb_le32(b, (uint32_t)n);
	cb_put(b, payload, n);
	if (n & 1) {
		cb_u8(b, 0);
	}
}

/** VP8L chunk (header + payload) of a 1x1 lossless image */
static cb_buf_t webp_vp8l_1x1(void)
{
	static const uint8_t rgba[4] = { 0x20, 0x40, 0x80, 0xFF };
	uint8_t *still = NULL;
	size_t still_len = WebPEncodeLosslessRGBA(rgba, 1, 1, 4, &still);
	cb_buf_t c = { 0 };
	if (still_len >= 20 && still != NULL && memcmp(still + 12, "VP8L", 4) == 0) {
		cb_put(&c, still + 12, still_len - 12);
	}
	WebPFree(still);
	return c;
}

static void webp_begin(cb_buf_t *b, unsigned flags)
{
	cb_str(b, "RIFF");
	cb_le32(b, 0);
	cb_str(b, "WEBP");
	uint8_t vp8x[10] = { (uint8_t)flags, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; /* canvas 1x1 */
	webp_chunk(b, "VP8X", vp8x, sizeof(vp8x));
}

static void webp_finish(cb_buf_t *b)
{
	uint32_t riff = (uint32_t)(b->n - 8);
	b->d[4] = (uint8_t)riff;
	b->d[5] = (uint8_t)(riff >> 8);
	b->d[6] = (uint8_t)(riff >> 16);
	b->d[7] = (uint8_t)(riff >> 24);
}

/** Still 1x1 image with an optional EXIF and XMP chunk (NULL: no chunk) */
static void webp_still_meta(cb_buf_t *b, const void *exif, size_t exif_len, const char *xmp, size_t xmp_len)
{
	cb_buf_t vp8l = webp_vp8l_1x1();
	ASSERT_TRUE(vp8l.n > 8);
	webp_begin(b, (exif != NULL ? VP8X_EXIF : 0) | (xmp != NULL ? VP8X_XMP : 0) | VP8X_ALPHA);
	cb_put(b, vp8l.d, vp8l.n);
	if (exif != NULL) {
		webp_chunk(b, "EXIF", exif, exif_len);
	}
	if (xmp != NULL) {
		webp_chunk(b, "XMP ", xmp, xmp_len);
	}
	webp_finish(b);
	cb_free(&vp8l);
}

/** Animation of n 1x1 frames of duration_ms each */
static void webp_anim(cb_buf_t *b, int n, unsigned duration_ms)
{
	cb_buf_t vp8l = webp_vp8l_1x1();
	ASSERT_TRUE(vp8l.n > 8);
	webp_begin(b, VP8X_ANIM | VP8X_ALPHA);
	const uint8_t anim[6] = { 0, 0, 0, 0, 7, 0 }; /* loop count 7 */
	webp_chunk(b, "ANIM", anim, sizeof(anim));
	cb_buf_t anmf = { 0 };
	const uint8_t hdr[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, (uint8_t)duration_ms, (uint8_t)(duration_ms >> 8), 0, 0 };
	cb_put(&anmf, hdr, sizeof(hdr));
	cb_put(&anmf, vp8l.d, vp8l.n);
	for (int i = 0; i < n; i++) {
		webp_chunk(b, "ANMF", anmf.d, anmf.n);
	}
	webp_finish(b);
	cb_free(&anmf);
	cb_free(&vp8l);
}

static img2_status_t webp_decode(const cb_buf_t *b, img2_frames_t *out)
{
	return img2_decode_webp(b->d, b->n, NULL, out);
}

/* TIFF header (II, 42) + an IFD with one entry: Orientation 6 */
static const uint8_t k_tiff_orient6[26] = { 'I', 'I', 42, 0, 8, 0, 0, 0, 1, 0, 0x12, 0x01, 3, 0, 1, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0 };

CTEST(cov_b_webp, exif_chunk_variants)
{
	img2_frames_t fr;
	cb_buf_t b = { 0 };

	/* raw TIFF without the "Exif\0\0" prefix */
	webp_still_meta(&b, k_tiff_orient6, sizeof(k_tiff_orient6), NULL, 0);
	ASSERT_EQUAL(IMG2_OK, webp_decode(&b, &fr));
	ASSERT_NOT_NULL(fr.frames[0]->exif);
	ASSERT_EQUAL(6, fr.frames[0]->exif->orientation);
	ASSERT_NULL(fr.frames[0]->xmp);
	img2_frames_free(&fr);
	cb_free(&b);

	/* prefix + fewer than 8 TIFF bytes */
	webp_still_meta(&b, "Exif\0\0II*", 9, NULL, 0);
	ASSERT_EQUAL(IMG2_OK, webp_decode(&b, &fr));
	ASSERT_NULL(fr.frames[0]->exif);
	img2_frames_free(&fr);
	cb_free(&b);

	/* 8 bytes that are not a TIFF header */
	webp_still_meta(&b, "XXXXXXXXXX", 10, NULL, 0);
	ASSERT_EQUAL(IMG2_OK, webp_decode(&b, &fr));
	ASSERT_NULL(fr.frames[0]->exif);
	img2_frames_free(&fr);
	cb_free(&b);

	/* EXIF chunk shorter than the "Exif\0\0" prefix */
	webp_still_meta(&b, "Exi", 3, NULL, 0);
	ASSERT_EQUAL(IMG2_OK, webp_decode(&b, &fr));
	ASSERT_NULL(fr.frames[0]->exif);
	img2_frames_free(&fr);
	cb_free(&b);

	/* empty EXIF chunk */
	webp_still_meta(&b, "", 0, NULL, 0);
	ASSERT_EQUAL(IMG2_OK, webp_decode(&b, &fr));
	ASSERT_NULL(fr.frames[0]->exif);
	img2_frames_free(&fr);
	cb_free(&b);
}

CTEST(cov_b_webp, xmp_chunk_variants)
{
	img2_frames_t fr;
	cb_buf_t b = { 0 };

	static const char xmp[] = "<r><d><dc:creator>Bob</dc:creator></d></r>";
	webp_still_meta(&b, NULL, 0, xmp, sizeof(xmp) - 1);
	ASSERT_EQUAL(IMG2_OK, webp_decode(&b, &fr));
	ASSERT_NOT_NULL(fr.frames[0]->xmp);
	ASSERT_STR("Bob", fr.frames[0]->xmp->creator);
	ASSERT_NULL(fr.frames[0]->exif);
	img2_frames_free(&fr);
	cb_free(&b);

	webp_still_meta(&b, NULL, 0, "<r><open", 8);
	ASSERT_EQUAL(IMG2_OK, webp_decode(&b, &fr));
	ASSERT_NULL(fr.frames[0]->xmp);
	img2_frames_free(&fr);
	cb_free(&b);

	webp_still_meta(&b, NULL, 0, "", 0);
	ASSERT_EQUAL(IMG2_OK, webp_decode(&b, &fr));
	ASSERT_NULL(fr.frames[0]->xmp);
	img2_frames_free(&fr);
	cb_free(&b);
}

CTEST(cov_b_webp, single_frame_animation)
{
	cb_buf_t b = { 0 };
	webp_anim(&b, 1, 40);
	ASSERT_TRUE(img2_webp_is_animated(b.d, b.n));
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, webp_decode(&b, &fr));
	ASSERT_EQUAL(1, fr.count);
	ASSERT_EQUAL(0, fr.loop_count);
	ASSERT_EQUAL_U(0u, fr.frames[0]->delay_ms);
	img2_frames_free(&fr);
	cb_free(&b);
}

CTEST(cov_b_webp, zero_duration_frames)
{
	cb_buf_t b = { 0 };
	webp_anim(&b, 3, 0);
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, webp_decode(&b, &fr));
	ASSERT_EQUAL(3, fr.count);
	ASSERT_EQUAL(7, fr.loop_count);
	for (int i = 0; i < 3; i++) {
		ASSERT_EQUAL_U(0u, fr.frames[i]->delay_ms);
	}
	img2_frames_free(&fr);
	cb_free(&b);
}

CTEST(cov_b_webp, animation_without_frames)
{
	cb_buf_t b = { 0 };
	webp_anim(&b, 0, 10);
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_E_CODEC, webp_decode(&b, &fr));
	ASSERT_NULL(fr.frames);
	cb_free(&b);
}

CTEST(cov_b_webp, undecodable_input)
{
	static const uint8_t junk[16] = { 'R', 'I', 'F', 'F', 8, 0, 0, 0, 'W', 'E', 'B', 'P', 'J', 'U', 'N', 'K' };
	ASSERT_FALSE(img2_webp_is_animated(junk, sizeof(junk)));
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_E_CODEC, img2_decode_webp(junk, sizeof(junk), NULL, &fr));
	ASSERT_NULL(fr.frames);
}

CTEST(cov_b_webp, argument_checks)
{
	const uint8_t one = 'R';
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_webp(&one, 1, NULL, NULL));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_webp(NULL, 1, NULL, &fr));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_webp(&one, 0, NULL, &fr));
	ASSERT_FALSE(img2_webp_is_animated(NULL, 1));
	ASSERT_FALSE(img2_webp_is_animated(&one, 0));
}
