/**
 * @file test_cov_c_jxl.c
 * @brief Coverage lane C: JPEG XL decoder (limits, timing edges, corrupt data, container metadata boxes) and
 *        the JXL encoder (quality clamp, invalid basic info, output over 64 KiB)
 *
 * White-box: img2_decode_jxl() / img2_encode_jxl() are called directly. Codestreams are encoded in-test with the
 * vendored libjxl (cov_c_jxl_util.h); containers with Exif / xml boxes are assembled byte by byte.
 */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../libimg2/core/image.h"
#include "../../libimg2/decoders/decoder.h"
#include "../../libimg2/encoders/encoder.h"
#include "../../libimg2/metadata/exif_reader.h"
#include "../ctest.h"
#include "cov_c_jxl_util.h"

static img2_status_t jd(const uint8_t *d, size_t len, img2_frames_t *fr)
{
	memset(fr, 0, sizeof(*fr));
	return img2_decode_jxl(d, len, NULL, fr);
}

CTEST(cov_c_jxl, argument_checks)
{
	img2_frames_t fr;
	const uint8_t b[4] = { 0xFF, 0x0A, 0, 0 };
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_jxl(b, sizeof(b), NULL, NULL));
	ASSERT_EQUAL(IMG2_E_ARG, jd(NULL, 4, &fr));
	ASSERT_EQUAL(IMG2_E_ARG, jd(b, 0, &fr));
	ASSERT_NULL(fr.frames);
}

CTEST(cov_c_jxl, dimension_limit)
{
	/* 70000 x 1 and 1 x 70000: over IMG2_MAX_DIMENSION, LIMIT from the header walk */
	img2_frames_t fr;
	size_t len = 0;
	jx_params_t wide = { 70000, 1, 1, 0, 0, 0, 0, 0 };
	uint8_t *d = jx_encode(&wide, &len);
	ASSERT_NOT_NULL(d);
	ASSERT_EQUAL(IMG2_E_LIMIT, jd(d, len, &fr));
	free(d);
	jx_params_t tall = { 1, 70000, 1, 0, 0, 0, 0, 0 };
	d = jx_encode(&tall, &len);
	ASSERT_NOT_NULL(d);
	ASSERT_EQUAL(IMG2_E_LIMIT, jd(d, len, &fr));
	free(d);
}

CTEST(cov_c_jxl, animation_timing_edges)
{
	img2_frames_t fr;
	size_t len = 0;
	/* 1 tick = 1024 s: 5000 ticks = 5.12e9 ms, clamped to UINT32_MAX; 2^31 loops: clamped to INT_MAX */
	jx_params_t p = { 2, 2, 2, 1, 1024, 0x80000000u, 5000, 0 };
	uint8_t *d = jx_encode(&p, &len);
	ASSERT_NOT_NULL(d);
	ASSERT_EQUAL(IMG2_OK, jd(d, len, &fr));
	ASSERT_EQUAL(2, fr.count);
	ASSERT_EQUAL_U(UINT32_MAX, fr.frames[0]->delay_ms);
	ASSERT_EQUAL(INT_MAX, fr.loop_count);
	img2_frames_free(&fr);
	free(d);
}

CTEST(cov_c_jxl, corrupt_animation)
{
	/* pixel data of the last frame damaged, headers intact: the header walk passes, frame 2 fails to decode
	 * after frames 0 and 1 were allocated */
	img2_frames_t fr;
	size_t len = 0;
	jx_params_t p = { 8, 8, 3, 1000, 1, 0, 10, 1 };
	uint8_t *d = jx_encode(&p, &len);
	ASSERT_NOT_NULL(d);
	ASSERT_EQUAL(IMG2_OK, jd(d, len, &fr));
	img2_frames_free(&fr);
	for (size_t k = 4; k <= 64; k += 12) {
		uint8_t *c = malloc(len);
		ASSERT_NOT_NULL(c);
		memcpy(c, d, len);
		for (size_t i = len - k; i < len; i++) {
			c[i] ^= 0x5A;
		}
		ASSERT_EQUAL(IMG2_E_CODEC, jd(c, len, &fr));
		ASSERT_NULL(fr.frames);
		free(c);
	}
	free(d);
}

/* ===== container metadata boxes ===== */

/* 2x2 codestream wrapped in a container with the boxes in `extra` before the jxlc box */
static uint8_t *jx_with_boxes(const jx_buf_t *extra, size_t *len)
{
	jx_params_t p = { 2, 2, 1, 0, 0, 0, 0, 0 };
	size_t cs_len = 0;
	uint8_t *cs = jx_encode(&p, &cs_len);
	if (cs == NULL) {
		return NULL;
	}
	jx_buf_t b = { 0 };
	jx_container_start(&b);
	if (extra != NULL && extra->len > 0) {
		jx_raw(&b, extra->d, extra->len);
	}
	jx_box(&b, "jxlc", cs, cs_len);
	free(cs);
	*len = b.len;
	return b.d;
}

typedef struct {
	int exif;
	int xmp;
	char make[16];
	char creator[16];
} jx_meta_t;

static jx_meta_t jx_meta(const jx_buf_t *extra)
{
	size_t len = 0;
	uint8_t *d = jx_with_boxes(extra, &len);
	ASSERT_NOT_NULL(d);
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, jd(d, len, &fr));
	ASSERT_EQUAL(1, fr.count);
	jx_meta_t m = { 0 };
	if (fr.frames[0]->exif != NULL) {
		m.exif = 1;
		snprintf(m.make, sizeof(m.make), "%.15s", fr.frames[0]->exif->make);
	}
	if (fr.frames[0]->xmp != NULL) {
		m.xmp = 1;
		snprintf(m.creator, sizeof(m.creator), "%.15s", fr.frames[0]->xmp->creator);
	}
	img2_frames_free(&fr);
	free(d);
	return m;
}

static const char k_xmp[] = "<x:xmpmeta><rdf:Description><dc:creator>Bob</dc:creator></rdf:Description></x:xmpmeta>";

/* Exif box: prefix (0..4 bytes) + TIFF with Make */
static void exif_box(jx_buf_t *b, const void *prefix, size_t plen, int big_endian)
{
	uint8_t buf[600];
	memcpy(buf, prefix, plen);
	size_t n = plen + cov_tiff_build(buf + plen, COV_T_MAKE, big_endian);
	jx_box(b, "Exif", buf, n);
}

CTEST(cov_c_jxl, exif_and_xmp_boxes)
{
	static const uint8_t zero4[4] = { 0, 0, 0, 0 };
	jx_buf_t b = { 0 };
	/* TIFF at 0 (II and MM) and behind the 4-byte offset (II and MM), with an XMP box */
	for (int be = 0; be < 2; be++) {
		for (int pre = 0; pre < 2; pre++) {
			b.len = 0;
			exif_box(&b, zero4, pre ? 4 : 0, be);
			jx_box(&b, "xml ", k_xmp, strlen(k_xmp));
			jx_meta_t m = jx_meta(&b);
			ASSERT_EQUAL(1, m.exif);
			ASSERT_STR("Make1", m.make);
			ASSERT_EQUAL(1, m.xmp);
			ASSERT_STR("Bob", m.creator);
		}
	}

	/* decoys first: a junk box holding "Exif" headers with a size < 8 and one past the end, and partial
	 * type matches ("ExiX", "ExXX", "xmlX"); the real boxes come after */
	b.len = 0;
	uint8_t junk[40] = { 0, 0, 0, 4, 'E', 'x', 'i', 'f', 0x7F, 0xFF, 0xFF, 0xF0, 'E', 'x', 'i', 'f', 0, 0, 0, 9, 'E', 'x', 'i', 'X', 0, 0, 0, 9, 'E', 'x', 'X', 'X', 0, 0, 0, 9, 'x', 'm', 'l', 'X' };
	jx_box(&b, "junk", junk, sizeof(junk));
	exif_box(&b, "", 0, 0);
	jx_meta_t m = jx_meta(&b);
	ASSERT_EQUAL(1, m.exif);
	ASSERT_EQUAL(0, m.xmp);

	/* Exif that is no TIFF: 'I' without a valid header (parse fails), and an unparsable XMP box */
	b.len = 0;
	jx_box(&b, "Exif", "IXYZW", 5);
	jx_box(&b, "xml ", "<a", 2);
	m = jx_meta(&b);
	ASSERT_EQUAL(0, m.exif);
	ASSERT_EQUAL(0, m.xmp);

	/* no magic at 0 or 4, and a 2-byte block: no EXIF, but the XMP box is still read (an unusable Exif box
	 * used to end the metadata extraction before the XMP box was looked at) */
	static const char *const bad[] = { "abcdefgh", "ab" };
	for (size_t i = 0; i < 2; i++) {
		b.len = 0;
		jx_box(&b, "Exif", bad[i], strlen(bad[i]));
		jx_box(&b, "xml ", k_xmp, strlen(k_xmp));
		m = jx_meta(&b);
		ASSERT_EQUAL(0, m.exif);
		ASSERT_EQUAL(1, m.xmp);
		ASSERT_STR("Bob", m.creator);
	}
	/* 'M' at 0 / at 4 with a broken TIFF */
	b.len = 0;
	jx_box(&b, "Exif", "abcdMXYZ", 8);
	m = jx_meta(&b);
	ASSERT_EQUAL(0, m.exif);
	free(b.d);
}

/* ===== encoder ===== */

CTEST(cov_c_jxl, encoder_clamp_and_basic_info_error)
{
	img2_image_t *img = img2_image_alloc(4, 4);
	ASSERT_NOT_NULL(img);
	memset(img->pixels, 0x40, 4 * 4 * 4);
	uint8_t *out = NULL;
	size_t size = 0;
	ASSERT_EQUAL(IMG2_OK, img2_encode_jxl(img, -3, &out, &size));
	free(out);
	ASSERT_EQUAL(IMG2_OK, img2_encode_jxl(img, 250, &out, &size));
	free(out);

	/* a zero width is rejected by JxlEncoderSetBasicInfo (the pixels are never read) */
	img2_image_t zero = *img;
	zero.width = 0;
	ASSERT_EQUAL(IMG2_E_CODEC, img2_encode_jxl(&zero, 50, &out, &size));
	ASSERT_NULL(out);
	img2_image_destroy(img);
}
