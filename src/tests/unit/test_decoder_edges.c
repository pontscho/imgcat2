/**
 * @file test_decoder_edges.c
 * @brief ICO/CUR and QOI decoders, PNM/PAM and raw YUV edge cases
 *
 * Every fixture is generated in memory (ICO directory + BITMAPINFOHEADER DIBs, PNG entries encoded with
 * img2_encode(), QOI streams of QOI_OP_RGBA / QOI_OP_RUN chunks, Netpbm text headers).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <img2/img2.h>

#include "../../libimg2/decoders/decoder.h"
#include "../ctest.h"

static void put16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static void put32be(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

/* ===== ICO / CUR ===== */

typedef struct {
	int32_t width;
	int32_t height;     /* header height (2x the image height when an AND mask follows) */
	uint16_t bpp;
	uint32_t clr_used;
	uint32_t hdr_size;  /* 40 */
	uint16_t planes;    /* 1 */
	uint32_t compression;
	size_t trim;        /* bytes cut from the end of the DIB */
	uint8_t mask_byte;  /* AND mask fill */
} dib_spec_t;

static dib_spec_t dib_default(int32_t w, int32_t h, uint16_t bpp)
{
	dib_spec_t s = { w, h, bpp, 0, 40, 1, 0, 0, 0x55 };
	return s;
}

/* Writes the DIB at p, returns its size */
static size_t write_dib(uint8_t *p, const dib_spec_t *s)
{
	uint32_t w = (uint32_t)s->width;
	uint32_t hh = (uint32_t)s->height;
	bool mask = hh >= w * 2;
	uint32_t h = mask ? hh / 2 : hh;
	uint32_t pal = 0;
	if (s->bpp <= 8) {
		pal = s->clr_used > 0 ? s->clr_used : (1u << s->bpp);
	}
	size_t stride = (((size_t)w * s->bpp + 31) / 32) * 4;
	size_t mstride = (((size_t)w + 31) / 32) * 4;
	size_t total = 40 + (size_t)pal * 4 + stride * h + (mask ? mstride * h : 0);

	memset(p, 0, total);
	put32(p, s->hdr_size);
	put32(p + 4, (uint32_t)s->width);
	put32(p + 8, (uint32_t)s->height);
	put16(p + 12, s->planes);
	put16(p + 14, s->bpp);
	put32(p + 16, s->compression);
	put32(p + 32, s->clr_used);
	uint8_t *q = p + 40;
	for (uint32_t i = 0; i < pal; i++) {
		q[i * 4 + 0] = (uint8_t)(i * 50);
		q[i * 4 + 1] = (uint8_t)(i * 30);
		q[i * 4 + 2] = (uint8_t)(200 - i);
	}
	q += (size_t)pal * 4;
	for (size_t i = 0; i < stride * h; i++) {
		q[i] = (uint8_t)(i * 37 + 11);
	}
	q += stride * h;
	if (mask) {
		memset(q, s->mask_byte, mstride * h);
	}
	return total - s->trim;
}

/* ICO/CUR file with one directory entry pointing at payload (copied to offset 22); returns the length */
static size_t write_ico(uint8_t *out, uint16_t type, uint8_t dir_w, uint8_t dir_h, const uint8_t *payload, size_t payload_len)
{
	memset(out, 0, 22);
	put16(out + 2, type);
	put16(out + 4, 1);
	out[6] = dir_w;
	out[7] = dir_h;
	put16(out + 10, 1);
	put16(out + 12, 32);
	put32(out + 14, (uint32_t)payload_len);
	put32(out + 18, 22);
	memmove(out + 22, payload, payload_len);
	return 22 + payload_len;
}

static img2_status_t ico_decode_dib(const dib_spec_t *s, uint16_t type, img2_frames_t *out)
{
	static uint8_t dib[1 << 16];
	static uint8_t file[(1 << 16) + 64];
	size_t n = write_dib(dib, s);
	size_t len = write_ico(file, type, (uint8_t)s->width, (uint8_t)s->width, dib, n);
	return img2_decode_ico(file, len, NULL, out);
}

CTEST(ico, dib_every_depth)
{
	const uint16_t depths[] = { 1, 4, 8, 24, 32 };
	for (size_t i = 0; i < sizeof(depths) / sizeof(depths[0]); i++) {
		/* odd width (partial bytes for 1/4 bpp), with and without AND mask */
		for (int masked = 0; masked < 2; masked++) {
			dib_spec_t s = dib_default(3, masked ? 6 : 3, depths[i]);
			img2_frames_t out = { 0 };
			ASSERT_EQUAL(IMG2_OK, ico_decode_dib(&s, 1, &out));
			ASSERT_EQUAL(1, out.count);
			ASSERT_EQUAL(3, out.frames[0]->width);
			ASSERT_EQUAL(3, out.frames[0]->height);
			img2_frames_free(&out);
		}
	}
}

CTEST(ico, palette_index_out_of_range_is_transparent)
{
	/* clr_used smaller than the index range of the depth: indices past the palette -> transparent black */
	const uint16_t depths[] = { 1, 4, 8 };
	for (size_t i = 0; i < 3; i++) {
		dib_spec_t s = dib_default(9, 2, depths[i]);
		s.clr_used = 1;
		img2_frames_t out = { 0 };
		ASSERT_EQUAL(IMG2_OK, ico_decode_dib(&s, 2, &out)); /* CUR */
		bool saw_transparent = false;
		for (size_t p = 0; p < 9 * 2; p++) {
			if (out.frames[0]->pixels[p * 4 + 3] == 0) {
				saw_transparent = true;
			}
		}
		ASSERT_TRUE(saw_transparent);
		img2_frames_free(&out);
	}
}

CTEST(ico, truncated_and_mask_is_ignored)
{
	dib_spec_t s = dib_default(4, 8, 32);
	s.trim = 4; /* the AND mask (4 rows of 4 bytes) is incomplete */
	img2_frames_t out = { 0 };
	ASSERT_EQUAL(IMG2_OK, ico_decode_dib(&s, 1, &out));
	img2_frames_free(&out);
}

CTEST(ico, malformed_dib_headers)
{
	img2_frames_t out = { 0 };
	dib_spec_t s;

	s = dib_default(4, 4, 24);
	s.hdr_size = 12;
	ASSERT_EQUAL(IMG2_E_FORMAT, ico_decode_dib(&s, 1, &out));
	s = dib_default(4, 4, 24);
	s.planes = 2;
	ASSERT_EQUAL(IMG2_E_FORMAT, ico_decode_dib(&s, 1, &out));
	s = dib_default(4, 4, 16);
	ASSERT_EQUAL(IMG2_E_FORMAT, ico_decode_dib(&s, 1, &out));
	s = dib_default(4, 4, 24);
	s.compression = 1;
	ASSERT_EQUAL(IMG2_E_FORMAT, ico_decode_dib(&s, 1, &out));
	/* pixel rows cut short */
	s = dib_default(4, 3, 24);
	s.trim = 8;
	ASSERT_EQUAL(IMG2_E_FORMAT, ico_decode_dib(&s, 1, &out));
	/* zero width: the image allocation is refused */
	s = dib_default(0, 4, 32);
	ASSERT_EQUAL(IMG2_E_FORMAT, ico_decode_dib(&s, 1, &out));
	/* DIB shorter than a BITMAPINFOHEADER, and shorter than a PNG signature */
	s = dib_default(4, 4, 24);
	s.trim = 40 + 4 * 4 * 3 - 20;
	ASSERT_EQUAL(IMG2_E_FORMAT, ico_decode_dib(&s, 1, &out));
	s.trim = 40 + 4 * 4 * 3 - 4;
	ASSERT_EQUAL(IMG2_E_FORMAT, ico_decode_dib(&s, 1, &out));
}

CTEST(ico, huge_palette_count_is_rejected)
{
	/* clr_used * 4 must not wrap in 32 bits (0x40000001 * 4 == 4) */
	static uint8_t file[22 + 40 + 64];
	uint8_t dib[40 + 64] = { 0 };
	put32(dib, 40);
	put32(dib + 4, 2);
	put32(dib + 8, 2);
	put16(dib + 12, 1);
	put16(dib + 14, 8);
	put32(dib + 32, 0x40000001u);
	size_t len = write_ico(file, 1, 2, 2, dib, sizeof(dib));
	img2_frames_t out = { 0 };
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_decode_ico(file, len, NULL, &out));
}

CTEST(ico, directory_errors)
{
	uint8_t file[64] = { 0 };
	img2_frames_t out = { 0 };
	uint8_t payload[24] = { 0 };

	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_ico(file, sizeof(file), NULL, NULL));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_ico(NULL, 4, NULL, &out));
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_decode_ico(file, 21, NULL, &out));

	size_t len = write_ico(file, 1, 1, 1, payload, sizeof(payload));
	file[0] = 1; /* reserved */
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_decode_ico(file, len, NULL, &out));
	len = write_ico(file, 3, 1, 1, payload, sizeof(payload)); /* type */
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_decode_ico(file, len, NULL, &out));
	len = write_ico(file, 1, 1, 1, payload, sizeof(payload));
	put16(file + 4, 0); /* count 0 */
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_decode_ico(file, len, NULL, &out));
	put16(file + 4, 256);
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_decode_ico(file, len, NULL, &out));
	put16(file + 4, 3); /* 3 entries need 54 bytes of directory */
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_decode_ico(file, 40, NULL, &out));
	put16(file + 4, 1);
	put32(file + 18, (uint32_t)len); /* offset past the end */
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_decode_ico(file, len, NULL, &out));
	put32(file + 18, 22);
	put32(file + 14, 0); /* empty entry */
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_decode_ico(file, len, NULL, &out));
	put32(file + 14, (uint32_t)sizeof(payload) + 1); /* entry beyond the file */
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_decode_ico(file, len, NULL, &out));
	put32(file + 14, 0xFFFFFFF0u); /* offset + size wraps in 32 bits */
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_decode_ico(file, len, NULL, &out));
}

CTEST(ico, best_entry_and_png_payload)
{
	/* encode a 3x2 PNG, store it as the second (larger, 0 = 256 px) directory entry */
	img2_image_t *img = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_create(3, 2, &img));
	img2_image_t *arr[1] = { img };
	img2_frames_t in = { arr, 1, 0 };
	uint8_t *png = NULL;
	size_t png_len = 0;
	ASSERT_EQUAL(IMG2_OK, img2_encode(&in, IMG2_FORMAT_PNG, NULL, &png, &png_len));
	img2_image_destroy(img);

	size_t len = 6 + 32 + png_len;
	uint8_t *file = calloc(1, len);
	ASSERT_NOT_NULL(file);
	put16(file + 2, 1);
	put16(file + 4, 2);
	/* entry 0: 16x16, bogus offset (not selected: entry 1 has the larger area) */
	file[6] = 16;
	file[7] = 16;
	put32(file + 6 + 8, 4);
	put32(file + 6 + 12, 0);
	/* entry 1: 255x255 PNG (largest area wins) */
	file[22] = 255;
	file[23] = 255;
	put16(file + 22 + 6, 32);
	put32(file + 22 + 8, (uint32_t)png_len);
	put32(file + 22 + 12, 38);
	memcpy(file + 38, png, png_len);

	img2_frames_t out = { 0 };
	ASSERT_EQUAL(IMG2_OK, img2_decode(file, len, NULL, &out));
	ASSERT_EQUAL(3, out.frames[0]->width);
	img2_frames_free(&out);

	/* corrupt PNG payload (signature kept) */
	memset(file + 38 + 8, 0xEE, png_len - 8);
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_decode_ico(file, len, NULL, &out));

	free(file);
	img2_free(png);
}

/* ===== QOI ===== */

static size_t write_qoi(uint8_t *out, uint32_t w, uint32_t h, size_t rgba_pixels, size_t runs)
{
	memcpy(out, "qoif", 4);
	put32be(out + 4, w);
	put32be(out + 8, h);
	out[12] = 4;
	out[13] = 0;
	size_t n = 14;
	for (size_t i = 0; i < rgba_pixels; i++) {
		out[n++] = 0xFF; /* QOI_OP_RGBA */
		out[n++] = (uint8_t)(i * 40);
		out[n++] = (uint8_t)(i * 20);
		out[n++] = 7;
		out[n++] = 200;
	}
	for (size_t i = 0; i < runs; i++) {
		out[n++] = 0xC0 | 61; /* QOI_OP_RUN of 62 */
	}
	memset(out + n, 0, 7);
	out[n + 7] = 1;
	return n + 8;
}

CTEST(qoi, decode_and_errors)
{
	uint8_t buf[4096];
	img2_frames_t out = { 0 };

	size_t len = write_qoi(buf, 2, 2, 4, 0);
	ASSERT_EQUAL(IMG2_OK, img2_decode(buf, len, NULL, &out));
	ASSERT_EQUAL(2, out.frames[0]->width);
	ASSERT_EQUAL(200, out.frames[0]->pixels[3]);
	img2_frames_free(&out);

	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_qoi(buf, len, NULL, NULL));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_qoi(NULL, len, NULL, &out));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_qoi(buf, 0, NULL, &out));
	len = write_qoi(buf, 0, 2, 0, 0); /* zero width: qoi_decode refuses */
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_decode_qoi(buf, len, NULL, &out));

	/* 65537 x 1 decodes in qoi.h, img2 refuses the dimensions */
	len = write_qoi(buf, IMG2_MAX_DIMENSION + 1, 1, 1, 1100);
	ASSERT_EQUAL(IMG2_E_LIMIT, img2_decode_qoi(buf, len, NULL, &out));
}

/* ===== PNM / PAM ===== */

static img2_status_t pnm(const char *s, size_t len)
{
	img2_frames_t out = { 0 };
	img2_status_t st = img2_decode_pnm((const uint8_t *)s, len, NULL, &out);
	img2_frames_free(&out);
	return st;
}

#define PNM_LIT(s) pnm((s), sizeof(s) - 1)

CTEST(pnm_edges, header_errors)
{
	img2_frames_t out = { 0 };
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_pnm((const uint8_t *)"P1 1 1 0", 8, NULL, NULL));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_pnm(NULL, 8, NULL, &out));
	ASSERT_EQUAL(IMG2_E_FORMAT, pnm("P5", 2));
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("Q5 1 1 255 x"));
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P0 1 1 255 x"));
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P8 1 1 255 x"));
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P7#c\nWIDTH 1\n"));     /* PAM: no comment right after the magic */
	ASSERT_EQUAL(IMG2_OK, PNM_LIT("P2#c\n1 1 255 7"));            /* P1-P6: comment allowed */
	ASSERT_EQUAL(IMG2_OK, PNM_LIT("P2 #c\r1\v1\f255\r7"));        /* CR-terminated comment, VT/FF/CR spaces */
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P2 -1 1 255 7"));
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P2 a 1 255 7"));
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P2 1a 1 255 7"));        /* digits then a letter: '1' then bad height */
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P2 1 x 255 7"));
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P2 99999999999 1 255 7"));
	ASSERT_EQUAL(IMG2_E_LIMIT, PNM_LIT("P2 1 65537 255 7"));
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P5 1 1 255"));           /* no separator before the raster */
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P5 1 1 255x"));
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P1 1 1 2"));             /* P1 sample is neither 0 nor 1 */
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P1 2 1 0 "));            /* P1 raster ends early */
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P2 #comment at EOF"));
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P7\nWIDTH"));             /* keyword at EOF */
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P7\nTUPLTYPE"));          /* TUPLTYPE at EOF, empty value */
}

CTEST(pnm_edges, pam_header_variants)
{
	ASSERT_EQUAL(IMG2_OK, PNM_LIT("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 3\nMAXVAL 255\nTUPLTYPE\tRGB \nENDHDR \t\nabc"));
	ASSERT_EQUAL(IMG2_OK, PNM_LIT("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\nTUPLTYPE GRAYSCALE\rENDHDR\na"));
	ASSERT_EQUAL(IMG2_OK, PNM_LIT("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 2\nMAXVAL 255\nTUPLTYPE BLACKANDWHITE_ALPHA\nENDHDR\nab"));
	ASSERT_EQUAL(IMG2_OK, PNM_LIT("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 4\nMAXVAL 255\nTUPLTYPE CUSTOM\nENDHDR\nabcd"));
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 2\nMAXVAL 255\nTUPLTYPE RGB\nENDHDR\nab"));
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\nENDHDR x\na"));
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\nENDHDR  "));
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P7\nWIDTH x\n"));
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P7\nHEIGHT x\n"));
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P7\nDEPTH x\n"));
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P7\nMAXVAL x\n"));
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P7\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\nENDHDR\na"));
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P7\nWIDTH 1\nDEPTH 1\nMAXVAL 255\nENDHDR\na"));
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P7\nWIDTH 1\nHEIGHT 1\nMAXVAL 255\nENDHDR\na"));
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 1\nENDHDR\na"));
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 0\nMAXVAL 255\nENDHDR\na"));
	ASSERT_EQUAL(IMG2_E_FORMAT, PNM_LIT("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 5\nMAXVAL 255\nENDHDR\nabcde"));
}

/* ===== raw YUV ===== */

CTEST(yuv_edges, spec_and_layout_errors)
{
	img2_yuv_params_t p = { 0 };
	char err[160];
	size_t sz = 0;

	ASSERT_EQUAL(IMG2_OK, img2_yuv_parse_spec("I420:2:2", &p, err, sizeof(err)));
	ASSERT_EQUAL(IMG2_YUV_FMT_I420, p.format);
	ASSERT_EQUAL(IMG2_E_ARG, img2_yuv_parse_spec("i420::2", &p, err, sizeof(err)));
	ASSERT_EQUAL(IMG2_OK, img2_yuv_parse_spec("i420:2:2:2:", &p, err, sizeof(err)));
	ASSERT_EQUAL(IMG2_E_ARG, img2_yuv_parse_spec("i420:2:2::0", &p, err, sizeof(err)));
	ASSERT_EQUAL(IMG2_E_ARG, img2_yuv_parse_spec("i420:2:2::x", &p, err, sizeof(err)));
	/* no error buffer, or one of size 0 */
	ASSERT_EQUAL(IMG2_E_ARG, img2_yuv_parse_spec("bogus:2:2", &p, NULL, 0));
	ASSERT_EQUAL(IMG2_E_ARG, img2_yuv_parse_spec("bogus:2:2", &p, err, 0));

	ASSERT_FALSE(img2_yuv_frame_size(NULL, &sz));
	img2_yuv_params_t q = { IMG2_YUV_FMT_GRAY, 2, 2, 0, 0 };
	ASSERT_FALSE(img2_yuv_frame_size(&q, NULL));
	q.height = IMG2_MAX_DIMENSION + 1;
	ASSERT_FALSE(img2_yuv_frame_size(&q, &sz));
	q.height = 2;
	q.format = IMG2_YUV_FMT_NONE;
	ASSERT_FALSE(img2_yuv_frame_size(&q, &sz));
	q.format = (img2_yuv_format_t)99;
	ASSERT_FALSE(img2_yuv_frame_size(&q, &sz));
	ASSERT_STR("unknown", img2_yuv_format_name((img2_yuv_format_t)99));

	/* size_t overflow in each step of the planar layout */
	const size_t big = SIZE_MAX / 2;
	img2_yuv_params_t o1 = { IMG2_YUV_FMT_I420, 4, 3, 4, big };           /* chroma plane size */
	ASSERT_FALSE(img2_yuv_frame_size(&o1, &sz));
	img2_yuv_params_t o2 = { IMG2_YUV_FMT_I420, 4, 1, big, big };         /* second plane offset + last row */
	ASSERT_FALSE(img2_yuv_frame_size(&o2, &sz));
	img2_yuv_params_t o3 = { IMG2_YUV_FMT_NV12, 4, 3, SIZE_MAX / 3, big }; /* plane offset + last rows */
	ASSERT_FALSE(img2_yuv_frame_size(&o3, &sz));
	img2_yuv_params_t o4 = { IMG2_YUV_FMT_I420, 4, 6, 4, big };           /* chroma plane of 3 rows */
	ASSERT_FALSE(img2_yuv_frame_size(&o4, &sz));
	img2_yuv_params_t o5 = { IMG2_YUV_FMT_GRAY, 4, 3, big, 0 };           /* luma plane */
	ASSERT_FALSE(img2_yuv_frame_size(&o5, &sz));
}

CTEST(yuv_edges, decode_arguments)
{
	img2_frames_t out = { 0 };
	const uint8_t data[16] = { 0 };
	img2_yuv_params_t p = { IMG2_YUV_FMT_GRAY, 2, 2, 0, 0 };
	img2_decode_opts_t opts = IMG2_DECODE_OPTS_INIT;
	opts.yuv = &p;

	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_yuv(data, sizeof(data), &opts, NULL));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_yuv(data, sizeof(data), NULL, &out));
	img2_decode_opts_t no_yuv = IMG2_DECODE_OPTS_INIT;
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_yuv(data, sizeof(data), &no_yuv, &out));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_yuv(NULL, sizeof(data), &opts, &out));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_yuv(data, 0, &opts, &out));
	ASSERT_EQUAL(IMG2_OK, img2_decode_yuv(data, sizeof(data), &opts, &out));
	img2_frames_free(&out);
}
