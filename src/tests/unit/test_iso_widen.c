/**
 * @file test_iso_widen.c
 * @brief ISO values above 65535 (metadata/exif_reader.c): ISOSpeedRatings and the EXIF 2.3 sensitivity tags
 *
 * ISOSpeedRatings (0x8827) is a SHORT; 65535 there means "65535 or more": the value is then taken from the LONG tag
 * SensitivityType (0x8830) selects, StandardOutputSensitivity (0x8831), RecommendedExposureIndex (0x8832) or
 * ISOSpeed (0x8833). TIFF blocks are written in-test into exactly sized heap buffers (ASan sees any over-read).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <img2/img2.h>

#include "../../libimg2/metadata/exif_reader.h"
#include "../ctest.h"

#define SHORT 3
#define LONG  4
#define RATNL 5

typedef struct {
	uint16_t tag;
	uint16_t type;
	uint32_t count;
	uint32_t value; /* SHORT: the value (packed into the first two bytes); otherwise the raw 32-bit field */
} ent_t;

typedef struct {
	uint8_t b[512];
	size_t len;
	bool be;
} tw_t;

static void put16(tw_t *w, size_t off, uint16_t v)
{
	w->b[off + (w->be ? 0 : 1)] = (uint8_t)(v >> 8);
	w->b[off + (w->be ? 1 : 0)] = (uint8_t)v;
}

static void put32(tw_t *w, size_t off, uint32_t v)
{
	for (int i = 0; i < 4; i++) {
		w->b[off + (size_t)(w->be ? 3 - i : i)] = (uint8_t)(v >> (8 * i));
	}
}

static void put_ifd(tw_t *w, const ent_t *e, int n)
{
	put16(w, w->len, (uint16_t)n);
	w->len += 2;
	for (int i = 0; i < n; i++) {
		put16(w, w->len, e[i].tag);
		put16(w, w->len + 2, e[i].type);
		put32(w, w->len + 4, e[i].count);
		if (e[i].type == SHORT && e[i].count <= 2) {
			put32(w, w->len + 8, 0);
			put16(w, w->len + 8, (uint16_t)e[i].value);
		} else {
			put32(w, w->len + 8, e[i].value);
		}
		w->len += 12;
	}
	put32(w, w->len, 0);
	w->len += 4;
}

/* IFD0 = { ExifIFD pointer } at 8, ExifIFD = e[0..n) at 26; out-of-line data may follow (tail_off(n)) */
static void build(tw_t *w, bool be, const ent_t *e, int n)
{
	memset(w, 0, sizeof(*w));
	w->be = be;
	w->b[0] = w->b[1] = be ? 'M' : 'I';
	put16(w, 2, 0x2A);
	put32(w, 4, 8);
	w->len = 8;
	const ent_t ifd0[] = { { 0x8769, LONG, 1, 26 } };
	put_ifd(w, ifd0, 1);
	put_ifd(w, e, n);
}

/* offset of the data that follows the ExifIFD of n entries */
static uint32_t tail_off(int n)
{
	return (uint32_t)(26 + 2 + 12 * n + 4);
}

/* parses the first `len` bytes of w from an exactly sized heap copy */
static img2_exif_t parse_n(const tw_t *w, size_t len)
{
	img2_exif_t e;
	img2_exif_init(&e);
	uint8_t *copy = malloc(len);
	if (copy != NULL) {
		memcpy(copy, w->b, len);
		(void)img2_exif_parse_tiff(&e, copy, len);
		free(copy);
	}
	return e;
}

static img2_exif_t parse(const ent_t *ents, int n, bool be)
{
	tw_t w;
	build(&w, be, ents, n);
	return parse_n(&w, w.len);
}

#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* the Nikon Z 9 case: ISOSpeedRatings 65535, SensitivityType 3, ISOSpeed 102400 */
CTEST(iso_widen, short_65535_type3_isospeed)
{
	const ent_t ents[] = {
		{ 0x8827, SHORT, 1, 65535 },
		{ 0x8830, SHORT, 1, 3 },
		{ 0x8832, LONG, 1, 51200 }, /* REI: not permitted by type 3 */
		{ 0x8833, LONG, 1, 102400 },
	};
	for (int be = 0; be < 2; be++) {
		img2_exif_t e = parse(ents, N(ents), be != 0);
		ASSERT_TRUE(e.has_iso);
		ASSERT_EQUAL(102400, e.iso_speed);
	}
}

CTEST(iso_widen, type2_rei)
{
	const ent_t ents[] = {
		{ 0x8827, SHORT, 1, 65535 },
		{ 0x8830, SHORT, 1, 2 },
		{ 0x8831, LONG, 1, 25600 },
		{ 0x8832, LONG, 1, 204800 },
		{ 0x8833, LONG, 1, 102400 },
	};
	img2_exif_t e = parse(ents, N(ents), false);
	ASSERT_TRUE(e.has_iso);
	ASSERT_EQUAL(204800, e.iso_speed);
}

/* every SensitivityType: ISOSpeed, then REI, then SOS among the permitted ones */
CTEST(iso_widen, sensitivity_type_table)
{
	/* expected value with SOS 1000, REI 2000, ISOSpeed 3000 all present */
	static const uint32_t all3[8] = { 3000, 1000, 2000, 3000, 2000, 3000, 3000, 3000 };
	/* expected value with only SOS 1000 present (65535 kept when SOS is not permitted) */
	static const uint32_t sos_only[8] = { 1000, 1000, 65535, 65535, 1000, 1000, 65535, 1000 };
	for (uint32_t t = 0; t < 8; t++) {
		const ent_t ents[] = {
			{ 0x8827, SHORT, 1, 65535 },
			{ 0x8830, SHORT, 1, t },
			{ 0x8831, LONG, 1, 1000 },
			{ 0x8832, LONG, 1, 2000 },
			{ 0x8833, LONG, 1, 3000 },
		};
		img2_exif_t e = parse(ents, N(ents), false);
		ASSERT_EQUAL(all3[t], e.iso_speed);
		const ent_t sos[] = {
			{ 0x8827, SHORT, 1, 65535 },
			{ 0x8830, SHORT, 1, t },
			{ 0x8831, LONG, 1, 1000 },
		};
		e = parse(sos, N(sos), true);
		ASSERT_TRUE(e.has_iso);
		ASSERT_EQUAL(sos_only[t], e.iso_speed);
	}
	/* SensitivityType above 7 is invalid: treated as absent */
	const ent_t bad[] = {
		{ 0x8827, SHORT, 1, 65535 },
		{ 0x8830, SHORT, 1, 8 },
		{ 0x8831, LONG, 1, 1000 },
	};
	img2_exif_t e = parse(bad, N(bad), false);
	ASSERT_EQUAL(1000, e.iso_speed);
}

/* SensitivityType absent: the first present of ISOSpeed, REI, SOS; ISOSpeedRatings absent too */
CTEST(iso_widen, absent_sensitivity_type)
{
	const ent_t rei_sos[] = {
		{ 0x8831, LONG, 1, 25600 },
		{ 0x8832, LONG, 1, 128000 },
	};
	img2_exif_t e = parse(rei_sos, N(rei_sos), false);
	ASSERT_TRUE(e.has_iso);
	ASSERT_EQUAL(128000, e.iso_speed);

	const ent_t sos[] = { { 0x8831, LONG, 1, 80000 } };
	e = parse(sos, N(sos), true);
	ASSERT_TRUE(e.has_iso);
	ASSERT_EQUAL(80000, e.iso_speed);

	const ent_t all[] = {
		{ 0x8833, LONG, 1, 4294967295u },
		{ 0x8832, LONG, 1, 2000 },
		{ 0x8831, LONG, 1, 1000 },
	};
	e = parse(all, N(all), false);
	ASSERT_EQUAL(4294967295u, e.iso_speed);
}

/* an ordinary ISOSpeedRatings wins over the LONG tags */
CTEST(iso_widen, short_value_wins)
{
	const ent_t ents[] = {
		{ 0x8827, SHORT, 1, 800 },
		{ 0x8830, SHORT, 1, 3 },
		{ 0x8833, LONG, 1, 102400 },
	};
	img2_exif_t e = parse(ents, N(ents), false);
	ASSERT_TRUE(e.has_iso);
	ASSERT_EQUAL(800, e.iso_speed);

	/* 65535 with no usable LONG tag stays 65535 */
	const ent_t alone[] = { { 0x8827, SHORT, 1, 65535 } };
	e = parse(alone, N(alone), true);
	ASSERT_TRUE(e.has_iso);
	ASSERT_EQUAL(65535, e.iso_speed);
}

/* a LONG value of 0 never overrides */
CTEST(iso_widen, long_zero_ignored)
{
	const ent_t ents[] = {
		{ 0x8827, SHORT, 1, 65535 },
		{ 0x8830, SHORT, 1, 7 },
		{ 0x8833, LONG, 1, 0 },
		{ 0x8832, LONG, 1, 0 },
		{ 0x8831, LONG, 1, 0 },
	};
	img2_exif_t e = parse(ents, N(ents), false);
	ASSERT_TRUE(e.has_iso);
	ASSERT_EQUAL(65535, e.iso_speed);

	/* a later 0 does not erase an earlier value; ISOSpeed 0 falls through to REI */
	const ent_t later0[] = {
		{ 0x8832, LONG, 1, 51200 },
		{ 0x8832, LONG, 1, 0 },
		{ 0x8833, LONG, 1, 0 },
	};
	e = parse(later0, N(later0), false);
	ASSERT_TRUE(e.has_iso);
	ASSERT_EQUAL(51200, e.iso_speed);

	const ent_t only0[] = { { 0x8833, LONG, 1, 0 } };
	e = parse(only0, N(only0), false);
	ASSERT_FALSE(e.has_iso);
	ASSERT_EQUAL(0, e.iso_speed);
}

/* wrong types and counts are ignored */
CTEST(iso_widen, wrong_type_or_count)
{
	const ent_t ents[] = {
		{ 0x8827, SHORT, 1, 65535 },
		{ 0x8830, LONG, 1, 1 },       /* SensitivityType must be a SHORT: ignored (any tag permitted) */
		{ 0x8830, SHORT, 0, 1 },      /* count 0: ignored */
		{ 0x8831, SHORT, 1, 1000 },   /* SOS as SHORT: ignored */
		{ 0x8832, LONG, 0, 2000 },    /* count 0: ignored */
		{ 0x8833, RATNL, 1, 40 },     /* out of line, not LONG: ignored */
		{ 0x8831, LONG, 1, 3000 },
	};
	img2_exif_t e = parse(ents, N(ents), false);
	ASSERT_EQUAL(3000, e.iso_speed);
}

/* LONG tags with count > 1 are out of line: the first value, inside the bounds */
CTEST(iso_widen, out_of_line_long)
{
	static const uint16_t tags[3] = { 0x8831, 0x8832, 0x8833 };
	for (int i = 0; i < 6; i++) {
		const ent_t ents[] = {
			{ 0x8827, SHORT, 1, 65535 },
			{ tags[i % 3], LONG, 2, tail_off(2) },
		};
		tw_t w;
		build(&w, i >= 3, ents, N(ents));
		put32(&w, w.len, 409600);
		put32(&w, w.len + 4, 7);
		w.len += 8;
		img2_exif_t e = parse_n(&w, w.len);
		ASSERT_EQUAL(409600, e.iso_speed);
	}
}

/* truncated / out-of-bounds LONG tags are rejected without reading past the buffer */
CTEST(iso_widen, truncated_long_rejected)
{
	/* count 2 at the very end, only 4 of its 8 bytes in the buffer */
	const ent_t ents[] = {
		{ 0x8827, SHORT, 1, 65535 },
		{ 0x8833, LONG, 2, tail_off(2) },
	};
	tw_t w;
	build(&w, false, ents, N(ents));
	put32(&w, w.len, 409600);
	w.len += 4;
	img2_exif_t e = parse_n(&w, w.len);
	ASSERT_TRUE(e.has_iso);
	ASSERT_EQUAL(65535, e.iso_speed);

	/* offset near 4 GiB: must not wrap around the bounds check */
	const ent_t wrap[] = {
		{ 0x8827, SHORT, 1, 65535 },
		{ 0x8833, LONG, 2, 0xFFFFFFFCu },
		{ 0x8832, LONG, 0x40000000u, 0xFFFFFFF0u },
	};
	e = parse(wrap, N(wrap), true);
	ASSERT_EQUAL(65535, e.iso_speed);

	/* the buffer ends inside the ISOSpeed entry: the entry is never read */
	const ent_t cut[] = {
		{ 0x8827, SHORT, 1, 65535 },
		{ 0x8830, SHORT, 1, 3 },
		{ 0x8833, LONG, 1, 102400 },
	};
	build(&w, false, cut, N(cut));
	size_t entry3 = 26 + 2 + 12 * 2;
	for (size_t len = entry3; len < entry3 + 12; len++) {
		e = parse_n(&w, len);
		ASSERT_TRUE(e.has_iso);
		ASSERT_EQUAL(65535, e.iso_speed);
	}
	e = parse_n(&w, entry3 + 12);
	ASSERT_EQUAL(102400, e.iso_speed);
}
