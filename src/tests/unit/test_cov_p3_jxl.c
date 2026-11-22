/**
 * @file test_cov_p3_jxl.c
 * @brief Coverage phase 3: JPEG XL container metadata boxes are found by walking the ISOBMFF box structure
 *
 * White-box: img2_decode_jxl() is called directly. The codestream is encoded in-test (cov_c_jxl_util.h) and
 * wrapped in a container assembled byte by byte; the box layouts under test come before or after the jxlc box.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../libimg2/core/image.h"
#include "../../libimg2/decoders/decoder.h"
#include "../../libimg2/metadata/exif_reader.h"
#include "../ctest.h"
#include "cov_c_jxl_util.h"

typedef struct {
	int ok;     /* decode returned IMG2_OK with one frame */
	int exif;   /* EXIF attached */
	int xmp;    /* XMP attached */
	char make[16];
	char creator[16];
} p3_meta_t;

static p3_meta_t p3_decode(const uint8_t *d, size_t len)
{
	p3_meta_t m = { 0 };
	img2_frames_t fr;
	memset(&fr, 0, sizeof(fr));
	if (img2_decode_jxl(d, len, NULL, &fr) != IMG2_OK || fr.count != 1) {
		img2_frames_free(&fr);
		return m;
	}
	m.ok = 1;
	if (fr.frames[0]->exif != NULL) {
		m.exif = 1;
		snprintf(m.make, sizeof(m.make), "%.15s", fr.frames[0]->exif->make);
	}
	if (fr.frames[0]->xmp != NULL) {
		m.xmp = 1;
		snprintf(m.creator, sizeof(m.creator), "%.15s", fr.frames[0]->xmp->creator);
	}
	img2_frames_free(&fr);
	return m;
}

/* 2x2 codestream (cached) */
static const uint8_t *p3_codestream(size_t *len)
{
	static uint8_t *cs;
	static size_t cs_len;
	if (cs == NULL) {
		jx_params_t p = { 2, 2, 1, 0, 0, 0, 0, 0 };
		cs = jx_encode(&p, &cs_len);
	}
	*len = cs_len;
	return cs;
}

/* container: signature + ftyp, `before`, the jxlc box (size field 0 = "to the end" when jxlc_to_end), `after` */
static uint8_t *p3_container(const jx_buf_t *before, const jx_buf_t *after, int jxlc_to_end, size_t *len)
{
	size_t cs_len = 0;
	const uint8_t *cs = p3_codestream(&cs_len);
	if (cs == NULL) {
		return NULL;
	}
	jx_buf_t b = { 0 };
	jx_container_start(&b);
	if (before != NULL && before->len > 0) {
		jx_raw(&b, before->d, before->len);
	}
	if (jxlc_to_end) {
		static const uint8_t hdr[8] = { 0, 0, 0, 0, 'j', 'x', 'l', 'c' };
		jx_raw(&b, hdr, sizeof(hdr));
		jx_raw(&b, cs, cs_len);
	} else {
		jx_box(&b, "jxlc", cs, cs_len);
	}
	if (after != NULL && after->len > 0) {
		jx_raw(&b, after->d, after->len);
	}
	*len = b.len;
	return b.d;
}

static p3_meta_t p3_container_meta(const jx_buf_t *before, const jx_buf_t *after, int jxlc_to_end)
{
	size_t len = 0;
	uint8_t *d = p3_container(before, after, jxlc_to_end, &len);
	p3_meta_t m = { 0 };
	if (d != NULL) {
		m = p3_decode(d, len);
		free(d);
	}
	return m;
}

static const char k_xmp_bob[] = "<x:xmpmeta><rdf:Description><dc:creator>Bob</dc:creator></rdf:Description></x:xmpmeta>";
static const char k_xmp_mallory[] =
	"<x:xmpmeta><rdf:Description><dc:creator>Mallory</dc:creator></rdf:Description></x:xmpmeta>";

/* a complete, well-formed Exif box (TIFF with Make "Make1") appended to b */
static void p3_exif_box(jx_buf_t *b)
{
	uint8_t tiff[512];
	size_t n = cov_tiff_build(tiff, COV_T_MAKE, 0);
	jx_box(b, "Exif", tiff, n);
}

/*
 * Bytes that look like an Exif / xml box inside the payload of another box are not boxes. The old byte scanner
 * took the first "Exif" / "xml " it saw anywhere in the file.
 */
CTEST(p3_jxl_boxes, box_lookalikes_inside_another_box_are_ignored)
{
	/* a 'junk' box whose payload is a complete Exif box and a complete xml box; no real Exif box */
	jx_buf_t inner = { 0 };
	p3_exif_box(&inner);
	jx_box(&inner, "xml ", k_xmp_mallory, strlen(k_xmp_mallory));
	jx_buf_t before = { 0 };
	jx_box(&before, "junk", inner.d, inner.len);
	p3_meta_t m = p3_container_meta(&before, NULL, 0);
	ASSERT_EQUAL(1, m.ok);
	ASSERT_EQUAL(0, m.exif);
	ASSERT_EQUAL(0, m.xmp);

	/* the same junk box followed by the real xml box: the real one is used */
	jx_box(&before, "xml ", k_xmp_bob, strlen(k_xmp_bob));
	m = p3_container_meta(&before, NULL, 0);
	ASSERT_EQUAL(1, m.ok);
	ASSERT_EQUAL(0, m.exif);
	ASSERT_EQUAL(1, m.xmp);
	ASSERT_STR("Bob", m.creator);
	free(inner.d);
	free(before.d);
}

/* 64-bit largesize headers and size 0 ("to the end of the file") */
CTEST(p3_jxl_boxes, largesize_and_size_zero_boxes)
{
	uint8_t tiff[512];
	size_t n = cov_tiff_build(tiff, COV_T_MAKE, 0);

	/* Exif box with size field 1 and a 64-bit size, before jxlc */
	jx_buf_t before = { 0 };
	uint8_t hdr[16] = { 0, 0, 0, 1, 'E', 'x', 'i', 'f', 0, 0, 0, 0, 0, 0, 0, 0 };
	cov_put_be32(hdr + 12, (uint32_t)(n + 16));
	jx_raw(&before, hdr, sizeof(hdr));
	jx_raw(&before, tiff, n);
	p3_meta_t m = p3_container_meta(&before, NULL, 0);
	ASSERT_EQUAL(1, m.ok);
	ASSERT_EQUAL(1, m.exif);
	ASSERT_STR("Make1", m.make);

	/* last box with size 0 after jxlc: extends to the end of the file */
	jx_buf_t after = { 0 };
	static const uint8_t to_end[8] = { 0, 0, 0, 0, 'x', 'm', 'l', ' ' };
	jx_raw(&after, to_end, sizeof(to_end));
	jx_raw(&after, k_xmp_bob, strlen(k_xmp_bob));
	m = p3_container_meta(NULL, &after, 0);
	ASSERT_EQUAL(1, m.ok);
	ASSERT_EQUAL(0, m.exif);
	ASSERT_EQUAL(1, m.xmp);
	ASSERT_STR("Bob", m.creator);

	/* jxlc with size 0: the walk ends with it (the bytes of the codestream are no boxes) */
	m = p3_container_meta(NULL, NULL, 1);
	ASSERT_EQUAL(1, m.ok);
	ASSERT_EQUAL(0, m.exif);
	ASSERT_EQUAL(0, m.xmp);
	free(before.d);
	free(after.d);
}

/* malformed or truncated box headers after the codestream end the walk; nothing behind them is found */
CTEST(p3_jxl_boxes, malformed_trailing_headers_end_the_walk)
{
	static const struct {
		uint8_t b[16];
		size_t n;
		int last; /* the file ends with these bytes */
	} bad[] = {
		{ { 'E', 'x', 'i' }, 3, 1 },                                             /* < 8 bytes left */
		{ { 0, 0, 0, 1, 'E', 'x', 'i', 'f', 0, 0 }, 10, 1 },                     /* largesize, < 16 bytes left */
		{ { 0, 0, 0, 4, 'f', 'r', 'e', 'e' }, 8, 0 },                            /* size < 8 */
		{ { 0, 0, 0, 1, 'f', 'r', 'e', 'e', 0, 0, 0, 0, 0, 0, 0, 12 }, 16, 0 },   /* largesize < 16 */
		{ { 0, 0, 0, 1, 'f', 'r', 'e', 'e', 0x80, 0, 0, 0, 0, 0, 0, 0 }, 16, 0 }, /* largesize past the end */
		{ { 0x7F, 0xFF, 0xFF, 0xF0, 'f', 'r', 'e', 'e' }, 8, 0 },                /* size past the end */
	};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		/* the malformed header, then (out of reach for the walk) a real Exif box */
		jx_buf_t after = { 0 };
		jx_raw(&after, bad[i].b, bad[i].n);
		if (!bad[i].last) {
			p3_exif_box(&after);
		}
		p3_meta_t m = p3_container_meta(NULL, &after, 0);
		ASSERT_EQUAL(1, m.ok);
		ASSERT_EQUAL(0, m.exif);
		free(after.d);
	}
}

/* at most JXL_MAX_BOXES (4096) boxes are looked at */
CTEST(p3_jxl_boxes, box_count_is_bounded)
{
	static const uint8_t free_box[8] = { 0, 0, 0, 8, 'f', 'r', 'e', 'e' };
	/* signature, ftyp, jxlc = 3 boxes; 4092 free boxes put the Exif box at index 4095 (found), one more hides it */
	for (int extra = 0; extra < 2; extra++) {
		jx_buf_t after = { 0 };
		for (int i = 0; i < 4092 + extra; i++) {
			jx_raw(&after, free_box, sizeof(free_box));
		}
		p3_exif_box(&after);
		p3_meta_t m = p3_container_meta(NULL, &after, 0);
		ASSERT_EQUAL(1, m.ok);
		ASSERT_EQUAL(extra ? 0 : 1, m.exif);
		free(after.d);
	}
}

/* a Brotli-compressed box ('brob', original type in the first 4 payload bytes) is not decompressed */
CTEST(p3_jxl_boxes, brob_boxes_are_not_matched)
{
	uint8_t payload[4 + 512];
	memcpy(payload, "Exif", 4);
	size_t n = cov_tiff_build(payload + 4, COV_T_MAKE, 0);
	jx_buf_t after = { 0 };
	jx_box(&after, "brob", payload, n + 4);
	p3_meta_t m = p3_container_meta(NULL, &after, 0);
	ASSERT_EQUAL(1, m.ok);
	ASSERT_EQUAL(0, m.exif);
	free(after.d);
}

/* a bare codestream (no container) has no boxes, whatever bytes follow it */
CTEST(p3_jxl_boxes, bare_codestream_has_no_boxes)
{
	size_t cs_len = 0;
	const uint8_t *cs = p3_codestream(&cs_len);
	ASSERT_NOT_NULL(cs);
	jx_buf_t b = { 0 };
	jx_raw(&b, cs, cs_len);
	p3_exif_box(&b);
	jx_box(&b, "xml ", k_xmp_mallory, strlen(k_xmp_mallory));
	p3_meta_t m = p3_decode(b.d, b.len);
	ASSERT_EQUAL(1, m.ok);
	ASSERT_EQUAL(0, m.exif);
	ASSERT_EQUAL(0, m.xmp);
	free(b.d);
}
