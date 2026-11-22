/**
 * @file test_cov_a_ico.c
 * @brief ICO/CUR decoder (decoders/decoder_ico.c): entry selection and remaining edge cases
 *
 * Every fixture is generated in memory: an ICO directory followed by BITMAPINFOHEADER DIBs.
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

/* Writes a w x h DIB (no AND mask, default palette) at p; returns its size */
static size_t write_dib(uint8_t *p, uint32_t w, uint32_t h, uint16_t bpp)
{
	uint32_t pal = bpp <= 8 ? (1u << bpp) : 0;
	size_t stride = (((size_t)w * bpp + 31) / 32) * 4;
	size_t total = 40 + (size_t)pal * 4 + stride * h;
	memset(p, 0, total);
	put32(p, 40);
	put32(p + 4, w);
	put32(p + 8, h);
	put16(p + 12, 1);
	put16(p + 14, bpp);
	for (size_t i = 40 + (size_t)pal * 4; i < total; i++) {
		p[i] = (uint8_t)(i * 7);
	}
	return total;
}

typedef struct {
	uint32_t w; /* 1..256 (256 is stored as 0 in the directory) */
	uint32_t h;
	uint16_t dir_bpp; /* directory bit count (the scoring tiebreaker) */
	uint16_t dib_bpp;
} ico_entry_spec_t;

/* ICO with n entries, each with its own DIB payload; returns the file length (buffer from malloc) */
static uint8_t *write_ico(const ico_entry_spec_t *e, int n, size_t *len)
{
	size_t cap = 6 + 16 * (size_t)n;
	for (int i = 0; i < n; i++) {
		cap += 40 + 1024 + (((size_t)e[i].w * e[i].dib_bpp + 31) / 32) * 4 * e[i].h;
	}
	uint8_t *f = calloc(1, cap);
	if (f == NULL) {
		return NULL;
	}
	put16(f + 2, 1);
	put16(f + 4, (uint16_t)n);
	size_t off = 6 + 16 * (size_t)n;
	for (int i = 0; i < n; i++) {
		uint8_t *d = f + 6 + 16 * i;
		d[0] = (uint8_t)(e[i].w == 256 ? 0 : e[i].w);
		d[1] = (uint8_t)(e[i].h == 256 ? 0 : e[i].h);
		put16(d + 4, 1);
		put16(d + 6, e[i].dir_bpp);
		size_t sz = write_dib(f + off, e[i].w, e[i].h, e[i].dib_bpp);
		put32(d + 8, (uint32_t)sz);
		put32(d + 12, (uint32_t)off);
		off += sz;
	}
	*len = off;
	return f;
}

static uint32_t decoded_width(const ico_entry_spec_t *e, int n)
{
	size_t len = 0;
	uint8_t *f = write_ico(e, n, &len);
	if (f == NULL) {
		return 0;
	}
	img2_frames_t out = { 0 };
	uint32_t w = 0;
	if (img2_decode_ico(f, len, NULL, &out) == IMG2_OK) {
		w = out.frames[0]->width;
	}
	img2_frames_free(&out);
	free(f);
	return w;
}

CTEST(ico_select, entry_256x256_beats_smaller_entries)
{
	/* the score of a 256x256 entry must not wrap around (65536 << 16 == 2^32) */
	const ico_entry_spec_t small_first[] = { { 16, 16, 32, 32 }, { 48, 48, 32, 32 }, { 256, 256, 32, 1 } };
	ASSERT_EQUAL(256, decoded_width(small_first, 3));
	const ico_entry_spec_t big_first[] = { { 256, 256, 32, 1 }, { 32, 32, 32, 32 } };
	ASSERT_EQUAL(256, decoded_width(big_first, 2));
}

/* decodes an ICO whose entry `broken` has an invalid DIB header: IMG2_OK only if another entry was chosen */
static img2_status_t decode_with_broken(const ico_entry_spec_t *e, int n, int broken)
{
	size_t len = 0;
	uint8_t *f = write_ico(e, n, &len);
	if (f == NULL) {
		return IMG2_E_NOMEM;
	}
	uint32_t off = (uint32_t)f[6 + 16 * broken + 12] | ((uint32_t)f[6 + 16 * broken + 13] << 8) | ((uint32_t)f[6 + 16 * broken + 14] << 16);
	f[off] = 41; /* BITMAPINFOHEADER size != 40 */
	img2_frames_t out = { 0 };
	img2_status_t st = img2_decode_ico(f, len, NULL, &out);
	img2_frames_free(&out);
	free(f);
	return st;
}

CTEST(ico_select, bit_count_breaks_ties)
{
	/* same area: the higher directory bit count wins, whatever the order (the loser is broken) */
	const ico_entry_spec_t lo_first[] = { { 256, 256, 8, 1 }, { 256, 256, 32, 4 } };
	ASSERT_EQUAL(IMG2_OK, decode_with_broken(lo_first, 2, 0));
	const ico_entry_spec_t hi_first[] = { { 16, 16, 32, 32 }, { 16, 16, 8, 8 } };
	ASSERT_EQUAL(IMG2_OK, decode_with_broken(hi_first, 2, 1));
	/* the broken entry is chosen: the decode fails (proves the helper breaks the right entry) */
	ASSERT_EQUAL(IMG2_E_FORMAT, decode_with_broken(hi_first, 2, 0));
}
