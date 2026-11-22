/**
 * @file test_cov_a_exif.c
 * @brief EXIF / XMP reader (metadata/exif_reader.c): hostile IFD structures, every tag and type, JPEG scan
 *
 * TIFF/EXIF blocks are written in-test (tw_* below: header, IFDs, out-of-line values) in both byte orders.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <img2/img2.h>

#include "../../libimg2/metadata/exif_reader.h"
#include "../ctest.h"

/* ===== TIFF writer ===== */

#define TW_CAP 16384

typedef struct {
	uint8_t b[TW_CAP];
	size_t len;
	bool be;
} tw_t;

typedef struct {
	uint16_t tag;
	uint16_t type;
	uint32_t count;
	uint32_t value; /* inline value (already packed, see tw_inline16) or offset */
} tw_ent_t;

static void tw_put16(tw_t *w, size_t off, uint16_t v)
{
	if (w->be) {
		w->b[off] = (uint8_t)(v >> 8);
		w->b[off + 1] = (uint8_t)v;
	} else {
		w->b[off] = (uint8_t)v;
		w->b[off + 1] = (uint8_t)(v >> 8);
	}
}

static void tw_put32(tw_t *w, size_t off, uint32_t v)
{
	if (w->be) {
		w->b[off] = (uint8_t)(v >> 24);
		w->b[off + 1] = (uint8_t)(v >> 16);
		w->b[off + 2] = (uint8_t)(v >> 8);
		w->b[off + 3] = (uint8_t)v;
	} else {
		w->b[off] = (uint8_t)v;
		w->b[off + 1] = (uint8_t)(v >> 8);
		w->b[off + 2] = (uint8_t)(v >> 16);
		w->b[off + 3] = (uint8_t)(v >> 24);
	}
}

/* header with IFD0 at ifd0 (patch later with tw_set_ifd0) */
static void tw_init(tw_t *w, bool be)
{
	memset(w, 0, sizeof(*w));
	w->be = be;
	w->b[0] = w->b[1] = be ? 'M' : 'I';
	tw_put16(w, 2, 0x2A);
	tw_put32(w, 4, 8);
	w->len = 8;
}

static void tw_set_ifd0(tw_t *w, uint32_t off)
{
	tw_put32(w, 4, off);
}

/* appends raw bytes (2-byte aligned), returns their offset */
static uint32_t tw_blob(tw_t *w, const void *data, size_t n)
{
	w->len = (w->len + 1) & ~(size_t)1;
	uint32_t off = (uint32_t)w->len;
	memcpy(w->b + w->len, data, n);
	w->len += n;
	return off;
}

/* appends RATIONAL / SRATIONAL values (num, den pairs) in the writer's byte order */
static uint32_t tw_rationals(tw_t *w, const uint32_t *nd, int pairs)
{
	w->len = (w->len + 1) & ~(size_t)1;
	uint32_t off = (uint32_t)w->len;
	for (int i = 0; i < 2 * pairs; i++) {
		tw_put32(w, w->len, nd[i]);
		w->len += 4;
	}
	return off;
}

/* appends an IFD with n entries (next-IFD offset 0), returns its offset */
static uint32_t tw_ifd(tw_t *w, const tw_ent_t *e, int n)
{
	w->len = (w->len + 1) & ~(size_t)1;
	uint32_t off = (uint32_t)w->len;
	tw_put16(w, w->len, (uint16_t)n);
	w->len += 2;
	for (int i = 0; i < n; i++) {
		tw_put16(w, w->len, e[i].tag);
		tw_put16(w, w->len + 2, e[i].type);
		tw_put32(w, w->len + 4, e[i].count);
		tw_put32(w, w->len + 8, e[i].value);
		w->len += 12;
	}
	tw_put32(w, w->len, 0);
	w->len += 4;
	return off;
}

/* a SHORT stored inline: the value field holds it in its first two bytes (byte-order dependent) */
static uint32_t tw_inline16(const tw_t *w, uint16_t v)
{
	return w->be ? (uint32_t)v << 16 : v;
}

/* a single byte / char stored inline in the first byte of the value field */
static uint32_t tw_inline8(const tw_t *w, uint8_t v)
{
	return w->be ? (uint32_t)v << 24 : v;
}

static img2_status_t tw_parse(const tw_t *w, img2_exif_t *exif)
{
	img2_exif_init(exif);
	return img2_exif_parse_tiff(exif, w->b, w->len);
}

#define T_ASCII     2
#define T_SHORT     3
#define T_LONG      4
#define T_RATIONAL  5
#define T_SRATIONAL 10

/* ===== hostile IFD structures ===== */

CTEST(exif_cycle, sub_ifd_pointing_back_at_ifd0)
{
	/* IFD0's ExifIFD pointer is IFD0 itself: recursing into it never ends (stack overflow before the fix) */
	tw_t w;
	tw_init(&w, false);
	tw_ent_t e[] = { { 0x0112, T_SHORT, 1, 6 }, { 0x8769, T_LONG, 1, 8 } };
	tw_ifd(&w, e, 2);
	img2_exif_t exif;
	ASSERT_EQUAL(IMG2_OK, tw_parse(&w, &exif));
	ASSERT_EQUAL(6, exif.orientation);

	/* two IFDs pointing at each other through ExifIFD pointers, reached from IFD0 */
	tw_init(&w, true);
	uint32_t a = 8;
	uint32_t b = 8 + 2 + 12 + 4;
	tw_ent_t ea[] = { { 0x8769, T_LONG, 1, b } };
	tw_ifd(&w, ea, 1);
	tw_ent_t eb[] = { { 0x8769, T_LONG, 1, a } };
	tw_ifd(&w, eb, 1);
	tw_ent_t ifd0[] = { { 0x8769, T_LONG, 1, a } };
	tw_set_ifd0(&w, tw_ifd(&w, ifd0, 1));
	ASSERT_EQUAL(IMG2_OK, tw_parse(&w, &exif));
}

CTEST(exif_cycle, sub_ifds_followed_once_from_ifd0_only)
{
	/* the ExifIFD / GPS IFD pointers of IFD0 are followed once each; pointers inside a sub-IFD are ignored */
	tw_t w;
	tw_init(&w, false);
	uint32_t r1[] = { 1, 100 };
	uint32_t r2[] = { 1, 50 };
	tw_ent_t ex1[] = { { 0x829A, T_RATIONAL, 1, tw_rationals(&w, r1, 1) } };
	uint32_t exif1 = tw_ifd(&w, ex1, 1);
	tw_ent_t ex2[] = { { 0x829A, T_RATIONAL, 1, tw_rationals(&w, r2, 1) } };
	uint32_t exif2 = tw_ifd(&w, ex2, 1);
	tw_ent_t gps[] = { { 0x0005, 1, 1, tw_inline8(&w, 1) } };
	uint32_t gps_ifd = tw_ifd(&w, gps, 1);
	tw_ent_t inner[] = { { 0x8825, T_LONG, 1, gps_ifd } };
	uint32_t exif3 = tw_ifd(&w, inner, 1);
	tw_ent_t gps2[] = { { 0x0005, 1, 1, tw_inline8(&w, 0) } };
	uint32_t gps_ifd2 = tw_ifd(&w, gps2, 1);
	tw_ent_t ifd0[] = { { 0x8769, T_LONG, 1, exif1 }, { 0x8769, T_LONG, 1, exif2 }, { 0x8825, T_LONG, 1, gps_ifd }, { 0x8825, T_LONG, 1, gps_ifd2 } };
	tw_set_ifd0(&w, tw_ifd(&w, ifd0, 4));
	img2_exif_t exif;
	ASSERT_EQUAL(IMG2_OK, tw_parse(&w, &exif));
	ASSERT_DBL_NEAR(0.01, exif.exposure_time); /* the first ExifIFD only */
	ASSERT_EQUAL(1, exif.gps_altitude_ref);    /* the first GPS IFD only */

	tw_ent_t ifd0b[] = { { 0x8769, T_LONG, 1, exif3 } };
	tw_set_ifd0(&w, tw_ifd(&w, ifd0b, 1));
	ASSERT_EQUAL(IMG2_OK, tw_parse(&w, &exif));
	ASSERT_FALSE(exif.has_gps); /* GPS pointer inside the ExifIFD: not followed */
}

CTEST(exif_wrap_ifd, sub_ifd_offset_near_4g)
{
	/* 0xFFFFFFFF + 2 wrapped to 1 in 32 bits and passed the bounds check (read 4 GiB past the buffer) */
	tw_t w;
	tw_init(&w, false);
	tw_ent_t e[] = { { 0x8769, T_LONG, 1, 0xFFFFFFFFu }, { 0x8825, T_LONG, 1, 0xFFFFFFFFu } };
	tw_ifd(&w, e, 2);
	img2_exif_t exif;
	ASSERT_EQUAL(IMG2_OK, tw_parse(&w, &exif));
	ASSERT_FALSE(exif.has_gps);
}

CTEST(exif_wrap_gps, gps_value_offsets_near_4g)
{
	/* value_offset + {1, 24, 8, count} wrapped in 32 bits and passed the bounds checks */
	tw_t w;
	tw_init(&w, false);
	tw_ent_t gps[] = {
		{ 0x0001, T_ASCII, 5, 0xFFFFFFFFu },  /* latitude ref, out of line */
		{ 0x0002, T_RATIONAL, 3, 0xFFFFFFF0u }, /* latitude */
		{ 0x0003, T_ASCII, 5, 0xFFFFFFFFu },  /* longitude ref */
		{ 0x0004, T_RATIONAL, 3, 0xFFFFFFF0u }, /* longitude */
		{ 0x0006, T_RATIONAL, 1, 0xFFFFFFF9u }, /* altitude */
		{ 0x001D, T_ASCII, 11, 0xFFFFFFF8u },   /* datestamp */
	};
	uint32_t gps_ifd = tw_ifd(&w, gps, 6);
	tw_ent_t ifd0[] = { { 0x8825, T_LONG, 1, gps_ifd } };
	tw_set_ifd0(&w, tw_ifd(&w, ifd0, 1));
	img2_exif_t exif;
	ASSERT_EQUAL(IMG2_OK, tw_parse(&w, &exif));
	ASSERT_TRUE(exif.has_gps);
	ASSERT_EQUAL(0, exif.gps_latitude_ref);
	ASSERT_DBL_NEAR(0.0, exif.gps_latitude);
	ASSERT_DBL_NEAR(0.0, exif.gps_altitude);
	ASSERT_STR("", exif.gps_datestamp);
}

/* ===== TIFF header and IFD walking ===== */

CTEST(exif_tiff, header_and_argument_errors)
{
	img2_exif_t exif;
	const uint8_t ii[8] = { 'I', 'I', 42, 0, 8, 0, 0, 0 };
	ASSERT_EQUAL(IMG2_E_ARG, img2_exif_parse_tiff(NULL, ii, 8));
	ASSERT_EQUAL(IMG2_E_ARG, img2_exif_parse_tiff(&exif, NULL, 8));
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_exif_parse_tiff(&exif, ii, 7));
	const uint8_t im[8] = { 'I', 'M', 42, 0, 8, 0, 0, 0 };
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_exif_parse_tiff(&exif, im, 8));
	const uint8_t mi[8] = { 'M', 'I', 0, 42, 0, 0, 0, 8 };
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_exif_parse_tiff(&exif, mi, 8));
	const uint8_t bad_magic[8] = { 'M', 'M', 0, 43, 0, 0, 0, 8 };
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_exif_parse_tiff(&exif, bad_magic, 8));
	/* IFD0 offset past the end, and an IFD whose entries run past the end: OK, nothing parsed */
	ASSERT_EQUAL(IMG2_OK, img2_exif_parse_tiff(&exif, ii, 8));
	const uint8_t trunc[12] = { 'I', 'I', 42, 0, 8, 0, 0, 0, 5, 0, 0x12, 0x01 };
	ASSERT_EQUAL(IMG2_OK, img2_exif_parse_tiff(&exif, trunc, sizeof(trunc)));
	img2_exif_init(NULL);
	img2_xmp_init(NULL);
	img2_exif_clear(NULL);
	img2_xmp_clear(NULL);
}

/* ===== every IFD0 / ExifIFD tag, both byte orders ===== */

static void fill_all_tags(tw_t *w, bool good)
{
	/* good: the expected type and size; !good: a wrong type (or an oversized count) for every tag */
	uint16_t ascii = good ? T_ASCII : T_SHORT;
	uint16_t rat = good ? T_RATIONAL : T_LONG;
	uint16_t srat = good ? T_SRATIONAL : T_RATIONAL;
	uint16_t shrt = good ? T_SHORT : T_LONG;
	static const char make[] = "Canon  \0\0";  /* trailing spaces and NULs are trimmed */
	static const char model[] = "EOS 5D \0";
	static const char text[] = "Some text value";
	uint32_t s_make = tw_blob(w, make, sizeof(make) - 1);
	uint32_t s_model = tw_blob(w, model, sizeof(model) - 1);
	uint32_t s_text = tw_blob(w, text, sizeof(text));
	uint32_t comment = tw_blob(w, "ASCII\0\0\0Hello", 13);
	uint32_t r[] = { 1, 250, 28, 10, 7, 0, 3, 1, 5, 2, 50, 1, 72, 1, 300, 1 };
	uint32_t rats = tw_rationals(w, r, 8);
	const uint16_t shorts[3] = { 0, 0, 0 };
	uint32_t s35 = tw_blob(w, shorts, sizeof(shorts));
	tw_put16(w, s35, 50);
	uint32_t rc = good ? 1 : 2; /* wrong-type rationals as 2 LONGs: out of line like a RATIONAL */

	tw_ent_t ex[] = {
		{ 0x829A, rat, rc, rats },           /* exposure time 1/250 */
		{ 0x829D, rat, rc, rats + 8 },       /* f-number 2.8 */
		{ 0x8827, shrt, 1, tw_inline16(w, 400) },
		{ 0x9003, ascii, 16, s_text },       /* date/time original */
		{ 0x9201, srat, 1, rats + 16 },      /* shutter speed: denominator 0 */
		{ 0x9202, rat, rc, rats + 24 },
		{ 0x9203, srat, 1, rats + 32 },
		{ 0x9204, srat, 1, rats + 32 },
		{ 0x920A, rat, rc, rats + 40 },
		{ 0x9286, 7, 13, comment },          /* user comment with an 8-byte encoding prefix */
		{ 0x9209, shrt, 1, tw_inline16(w, 1) },
		{ 0x9207, shrt, 1, tw_inline16(w, 5) },
		{ 0x9208, shrt, 1, tw_inline16(w, 4) },
		{ 0xA405, good ? T_SHORT : T_LONG, 3, s35 }, /* 35 mm focal length, out of line (count 3) */
		{ 0xA433, ascii, 16, s_text },
		{ 0xA434, ascii, 16, s_text },
	};
	uint32_t exif_ifd = tw_ifd(w, ex, (int)(sizeof(ex) / sizeof(ex[0])));

	tw_ent_t ifd0[] = {
		{ 0x010F, ascii, sizeof(make) - 1, s_make },
		{ 0x0110, ascii, sizeof(model) - 1, s_model },
		{ 0x0112, shrt, 1, tw_inline16(w, 3) },
		{ 0x0131, ascii, 16, s_text },
		{ 0x0132, ascii, 16, s_text },
		{ 0x013B, ascii, 16, s_text },
		{ 0x8298, ascii, 16, s_text },
		{ 0x010E, ascii, 16, s_text },
		{ 0x0100, shrt, 1, good ? tw_inline16(w, 640) : 640 },
		{ 0x0101, shrt, 1, good ? tw_inline16(w, 480) : 480 },
		{ 0x0103, shrt, 1, tw_inline16(w, 6) },
		{ 0x0102, shrt, 1, tw_inline16(w, 8) },
		{ 0x0106, shrt, 1, tw_inline16(w, 2) },
		{ 0x011A, rat, rc, rats + 48 },
		{ 0x011B, rat, rc, rats + 56 },
		{ 0x0128, shrt, 1, tw_inline16(w, 2) },
		{ 0x8769, T_LONG, 1, exif_ifd },
	};
	tw_set_ifd0(w, tw_ifd(w, ifd0, (int)(sizeof(ifd0) / sizeof(ifd0[0]))));
}

CTEST(exif_tags, every_tag_with_its_type)
{
	for (int be = 0; be < 2; be++) {
		tw_t w;
		tw_init(&w, be != 0);
		fill_all_tags(&w, true);
		img2_exif_t e;
		ASSERT_EQUAL(IMG2_OK, tw_parse(&w, &e));
		ASSERT_STR("Canon", e.make);
		ASSERT_STR("EOS 5D", e.model);
		ASSERT_EQUAL(3, e.orientation);
		ASSERT_STR("Some text value", e.software);
		ASSERT_STR("Some text value", e.copyright);
		ASSERT_STR("Some text value", e.artist);
		ASSERT_STR("Some text value", e.description);
		ASSERT_STR("Some text value", e.lens_make);
		ASSERT_STR("Some text value", e.lens_model);
		ASSERT_TRUE(e.has_datetime);
		ASSERT_EQUAL(640, e.image_width);
		ASSERT_EQUAL(480, e.image_height);
		ASSERT_EQUAL(6, e.compression);
		ASSERT_EQUAL(8, e.bits_per_sample);
		ASSERT_EQUAL(2, e.photometric_interpretation);
		ASSERT_EQUAL(2, e.resolution_unit);
		ASSERT_DBL_NEAR(72.0, e.x_resolution);
		ASSERT_DBL_NEAR(300.0, e.y_resolution);
		ASSERT_TRUE(e.has_exposure_time);
		ASSERT_DBL_NEAR(0.004, e.exposure_time);
		ASSERT_DBL_NEAR(2.8, e.f_number);
		ASSERT_TRUE(e.has_iso);
		ASSERT_EQUAL(400, e.iso_speed);
		ASSERT_DBL_NEAR(0.0, e.shutter_speed); /* 7/0 */
		ASSERT_DBL_NEAR(3.0, e.aperture_value);
		ASSERT_DBL_NEAR(2.5, e.brightness_value);
		ASSERT_DBL_NEAR(2.5, e.exposure_bias);
		ASSERT_DBL_NEAR(50.0, e.focal_length);
		ASSERT_DBL_NEAR(50.0, e.focal_length_35mm);
		ASSERT_STR("Hello", e.user_comment);
		ASSERT_EQUAL(1, e.flash);
		ASSERT_EQUAL(5, e.metering_mode);
		ASSERT_EQUAL(4, e.light_source);
	}
}

CTEST(exif_tags, wrong_types_are_ignored)
{
	for (int be = 0; be < 2; be++) {
		tw_t w;
		tw_init(&w, be != 0);
		fill_all_tags(&w, false);
		img2_exif_t e;
		ASSERT_EQUAL(IMG2_OK, tw_parse(&w, &e));
		ASSERT_STR("", e.make);
		ASSERT_STR("", e.model);
		ASSERT_STR("", e.software);
		ASSERT_STR("", e.copyright);
		ASSERT_EQUAL(0, e.orientation);
		ASSERT_EQUAL(640, e.image_width); /* LONG is accepted for the dimensions */
		ASSERT_EQUAL(480, e.image_height);
		ASSERT_FALSE(e.has_exposure_time);
		ASSERT_FALSE(e.has_iso);
		ASSERT_DBL_NEAR(0.0, e.focal_length_35mm);
	}
}

CTEST(exif_tags, oversized_and_odd_values)
{
	tw_t w;
	tw_init(&w, false);
	static char big[700];
	memset(big, 'y', sizeof(big));
	uint32_t s_big = tw_blob(&w, big, sizeof(big));
	uint32_t s_short = tw_blob(&w, "Hi there", 8);
	tw_ent_t ex[] = {
		{ 0x9286, 7, 8, s_short },  /* user comment without an encoding prefix (count <= 8) */
		{ 0x9286, 7, 600, s_big }, /* user comment too long: ignored, the first one stays */
	};
	uint32_t exif_ifd = tw_ifd(&w, ex, 2);
	tw_ent_t ifd0[] = {
		{ 0x010F, T_ASCII, 600, s_big }, /* longer than every string field */
		{ 0x0110, T_ASCII, 600, s_big },
		{ 0x0131, T_ASCII, 600, s_big },
		{ 0x0132, T_ASCII, 600, s_big },
		{ 0x013B, T_ASCII, 600, s_big },
		{ 0x8298, T_ASCII, 600, s_big },
		{ 0x010E, T_ASCII, 600, s_big },
		{ 0xA433, T_ASCII, 600, s_big },
		{ 0xA434, T_ASCII, 600, s_big },
		{ 0x010F, T_ASCII, 64, 0x7FFFFFF0u }, /* out-of-line value past the end */
		{ 0x0100, 3, 1, 0 },                  /* IMAGE_WIDTH as SHORT 0 */
		{ 0x0100, T_RATIONAL, 0, 0 },         /* width / height with an unusable type: ignored */
		{ 0x0101, 9, 1, 7 },
		{ 0x0112, 1, 1, 1 },                  /* BYTE / SBYTE / SSHORT / SLONG and an unknown type */
		{ 0x0112, 6, 1, 1 },
		{ 0x0112, 8, 1, 1 },
		{ 0x0112, 99, 1, 1 },
		{ 0x9999, T_SHORT, 1, 1 },            /* unknown tag, inline */
		{ 0x9999, T_ASCII, 16, s_short },     /* unknown tag, out of line */
		{ 0x8769, T_LONG, 1, exif_ifd },
	};
	tw_set_ifd0(&w, tw_ifd(&w, ifd0, (int)(sizeof(ifd0) / sizeof(ifd0[0]))));
	img2_exif_t e;
	ASSERT_EQUAL(IMG2_OK, tw_parse(&w, &e));
	ASSERT_STR("", e.make);
	ASSERT_STR("", e.description);
	ASSERT_STR("Hi there", e.user_comment);
	ASSERT_EQUAL(0, e.image_width);
	ASSERT_EQUAL(0, e.orientation);
}

/* ===== GPS ===== */

static void gps_case(bool be, bool south_west, img2_exif_t *e)
{
	tw_t w;
	tw_init(&w, be);
	uint32_t r[] = { 47, 1, 30, 1, 36, 1, 19, 1, 3, 1, 0, 1, 120, 1 };
	uint32_t rats = tw_rationals(&w, r, 7);
	uint32_t date = tw_blob(&w, "2024:05:06", 11);
	uint32_t lat_ool = tw_blob(&w, "SOUTH", 5);
	uint32_t lon_ool = tw_blob(&w, "WEST.", 5);
	tw_ent_t gps[] = {
		{ 0x0001, T_ASCII, south_west ? 5 : 2, south_west ? lat_ool : tw_inline8(&w, 'N') },
		{ 0x0002, T_RATIONAL, 3, rats },
		{ 0x0003, T_ASCII, south_west ? 5 : 2, south_west ? lon_ool : tw_inline8(&w, 'E') },
		{ 0x0004, T_RATIONAL, 3, rats + 24 },
		{ 0x0005, 1, 1, tw_inline8(&w, south_west ? 1 : 0) },
		{ 0x0006, T_RATIONAL, 1, rats + 48 },
		{ 0x001D, T_ASCII, 11, date },
		{ 0x0007, T_RATIONAL, 3, rats }, /* time stamp: not parsed */
	};
	uint32_t gps_ifd = tw_ifd(&w, gps, (int)(sizeof(gps) / sizeof(gps[0])));
	tw_ent_t ifd0[] = { { 0x8825, T_LONG, 1, gps_ifd } };
	tw_set_ifd0(&w, tw_ifd(&w, ifd0, 1));
	tw_parse(&w, e);
}

CTEST(exif_gps, coordinates_both_hemispheres)
{
	img2_exif_t e;
	gps_case(true, false, &e); /* big endian, inline references */
	ASSERT_TRUE(e.has_gps);
	ASSERT_EQUAL('N', e.gps_latitude_ref);
	ASSERT_EQUAL('E', e.gps_longitude_ref);
	ASSERT_DBL_NEAR(47.51, e.gps_latitude);
	ASSERT_DBL_NEAR(19.05, e.gps_longitude);
	ASSERT_DBL_NEAR(120.0, e.gps_altitude);
	ASSERT_STR("2024:05:06", e.gps_datestamp);

	gps_case(false, true, &e); /* little endian, out-of-line references */
	ASSERT_EQUAL('S', e.gps_latitude_ref);
	ASSERT_EQUAL('W', e.gps_longitude_ref);
	ASSERT_DBL_NEAR(-47.51, e.gps_latitude);
	ASSERT_DBL_NEAR(-19.05, e.gps_longitude);
	ASSERT_DBL_NEAR(-120.0, e.gps_altitude);
}

CTEST(exif_gps, malformed_entries_are_ignored)
{
	tw_t w;
	tw_init(&w, false);
	uint32_t r[] = { 1, 1, 2, 1, 3, 1 };
	uint32_t rats = tw_rationals(&w, r, 3);
	tw_ent_t gps[] = {
		{ 0x0001, T_SHORT, 1, 'N' },          /* wrong type */
		{ 0x0001, T_ASCII, 0, 'N' },          /* empty */
		{ 0x0003, T_SHORT, 1, 'E' },
		{ 0x0003, T_ASCII, 0, 'E' },
		{ 0x0003, T_ASCII, 9, 0x7FFFFFF0u },  /* out of line past the end */
		{ 0x0001, T_ASCII, 9, 0x7FFFFFF0u },
		{ 0x0002, T_SHORT, 3, rats },         /* latitude: wrong type, wrong count, past the end */
		{ 0x0002, T_RATIONAL, 2, rats },
		{ 0x0002, T_RATIONAL, 3, 0x7FFFFFF0u },
		{ 0x0004, T_SHORT, 3, rats },
		{ 0x0004, T_RATIONAL, 2, rats },
		{ 0x0004, T_RATIONAL, 3, 0x7FFFFFF0u },
		{ 0x0005, 1, 0, 1 },                  /* altitude ref without a value */
		{ 0x0006, T_SHORT, 1, rats },         /* altitude: wrong type, past the end */
		{ 0x0006, T_RATIONAL, 1, 0x7FFFFFF0u },
		{ 0x001D, T_SHORT, 11, rats },        /* date stamp: wrong type, too long, past the end */
		{ 0x001D, T_ASCII, 40, rats },
		{ 0x001D, T_ASCII, 11, 0x7FFFFFF0u },
	};
	uint32_t gps_ifd = tw_ifd(&w, gps, (int)(sizeof(gps) / sizeof(gps[0])));
	tw_ent_t ifd0[] = { { 0x8825, T_LONG, 1, gps_ifd } };
	tw_set_ifd0(&w, tw_ifd(&w, ifd0, 1));
	img2_exif_t e;
	ASSERT_EQUAL(IMG2_OK, tw_parse(&w, &e));
	ASSERT_TRUE(e.has_gps);
	ASSERT_EQUAL(0, e.gps_latitude_ref);
	ASSERT_EQUAL(0, e.gps_longitude_ref);
	ASSERT_DBL_NEAR(0.0, e.gps_latitude);
	ASSERT_DBL_NEAR(0.0, e.gps_longitude);
	ASSERT_DBL_NEAR(0.0, e.gps_altitude);
	ASSERT_STR("", e.gps_datestamp);
}

/* ===== JPEG APP1 scan ===== */

static size_t jpeg_with(uint8_t *out, const uint8_t *pre, size_t pre_len, const char *marker, size_t marker_len, const uint8_t *payload, size_t payload_len)
{
	size_t n = 0;
	out[n++] = 0xFF;
	out[n++] = 0xD8;
	if (pre_len > 0) {
		memcpy(out + n, pre, pre_len);
		n += pre_len;
	}
	size_t seg = 2 + marker_len + payload_len;
	out[n++] = 0xFF;
	out[n++] = 0xE1;
	out[n++] = (uint8_t)(seg >> 8);
	out[n++] = (uint8_t)seg;
	memcpy(out + n, marker, marker_len);
	n += marker_len;
	memcpy(out + n, payload, payload_len);
	n += payload_len;
	out[n++] = 0xFF;
	out[n++] = 0xDA;
	out[n++] = 0;
	out[n++] = 2;
	return n;
}

CTEST(exif_jpeg, segment_scan)
{
	static uint8_t jpg[TW_CAP + 256];
	tw_t w;
	tw_init(&w, false);
	tw_ent_t ifd0[] = { { 0x0112, T_SHORT, 1, 8 } };
	tw_ifd(&w, ifd0, 1);

	/* padding FF bytes, a standalone marker (RST0), an APP0 and an XMP APP1 before the EXIF APP1 */
	static const char xmp_marker[] = "http://ns.adobe.com/xap/1.0/";
	uint8_t pre[64];
	size_t p = 0;
	pre[p++] = 0xFF;
	pre[p++] = 0xFF;
	pre[p++] = 0xD0;
	pre[p++] = 0xFF;
	pre[p++] = 0xE0;
	pre[p++] = 0;
	pre[p++] = 4;
	pre[p++] = 'J';
	pre[p++] = 'F';
	pre[p++] = 0xFF;
	pre[p++] = 0xE1;
	pre[p++] = 0;
	pre[p++] = (uint8_t)(2 + sizeof(xmp_marker));
	memcpy(pre + p, xmp_marker, sizeof(xmp_marker));
	p += sizeof(xmp_marker);
	size_t n = jpeg_with(jpg, pre, p, "Exif\0\0", 6, w.b, w.len);
	img2_exif_t e;
	img2_exif_init(&e);
	ASSERT_EQUAL(IMG2_OK, img2_exif_parse_jpeg(&e, jpg, n));
	ASSERT_EQUAL(8, e.orientation);
	img2_exif_init(&e);
	ASSERT_EQUAL(IMG2_OK, img2_exif_parse(&e, jpg, n));

	/* no EXIF before SOS; too short; not a JPEG; NULL arguments */
	n = jpeg_with(jpg, NULL, 0, "Other\0", 6, w.b, w.len);
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_exif_parse_jpeg(&e, jpg, n));
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_exif_parse_jpeg(&e, jpg, 3));
	const uint8_t not_soi[8] = { 0xFF, 0xD9, 0xFF, 0xE1, 0, 8, 0, 0 };
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_exif_parse_jpeg(&e, not_soi, 8));
	const uint8_t not_ff[8] = { 0x00, 0xD8, 0xFF, 0xE1, 0, 8, 0, 0 };
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_exif_parse_jpeg(&e, not_ff, 8));
	ASSERT_EQUAL(IMG2_E_ARG, img2_exif_parse_jpeg(NULL, jpg, n));
	ASSERT_EQUAL(IMG2_E_ARG, img2_exif_parse_jpeg(&e, NULL, n));

	/* malformed scans: no marker where one is expected, only padding to the end, APP1 header cut off,
	 * APP1 longer than the data, another marker's length cut off, a scan that runs out */
	const uint8_t no_marker[8] = { 0xFF, 0xD8, 0x00, 0xE1, 0, 8, 0, 0 };
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_exif_parse_jpeg(&e, no_marker, 8));
	const uint8_t all_pad[8] = { 0xFF, 0xD8, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_exif_parse_jpeg(&e, all_pad, 8));
	const uint8_t app1_cut[7] = { 0xFF, 0xD8, 0xFF, 0xFF, 0xFF, 0xFF, 0xE1 };
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_exif_parse_jpeg(&e, app1_cut, 7));
	const uint8_t app1_long[10] = { 0xFF, 0xD8, 0xFF, 0xE1, 0x10, 0x00, 'E', 'x', 'i', 'f' };
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_exif_parse_jpeg(&e, app1_long, 10));
	const uint8_t other_cut[7] = { 0xFF, 0xD8, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0 };
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_exif_parse_jpeg(&e, other_cut, 7));
	const uint8_t runs_out[9] = { 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x02, 0xFF, 0xD0, 0x00 };
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_exif_parse_jpeg(&e, runs_out, 9));
}

/* ===== XMP ===== */

static img2_status_t xmp_of(const char *body, img2_xmp_t *x)
{
	static char xml[4096];
	snprintf(xml, sizeof(xml), "<x:xmpmeta><rdf:Description>%s</rdf:Description></x:xmpmeta>", body);
	img2_xmp_init(x);
	return img2_xmp_parse_xml(x, xml, strlen(xml));
}

CTEST(exif_xmp, every_field)
{
	img2_xmp_t x;
	ASSERT_EQUAL(IMG2_OK, xmp_of("loose text<dc:creator>Ann</dc:creator><dc:rights>(c) Ann</dc:rights>"
	                             "<dc:description>a > b</dc:description><dc:title>T</dc:title><xmp:Rating>4</xmp:Rating>"
	                             "<xmp:CreateDate>2024</xmp:CreateDate><xmp:ModifyDate>2025</xmp:ModifyDate>"
	                             "<xmp:Label>Red</xmp:Label><photoshop:City>Pecs</photoshop:City>"
	                             "<photoshop:Country>HU</photoshop:Country><photoshop:Credit>Cr</photoshop:Credit>"
	                             "<photoshop:Source>Src</photoshop:Source><other>o</other>"
	                             "<dc:subject><rdf:Bag><rdf:li>one</rdf:li><rdf:li/><rdf:li>two</rdf:li></rdf:Bag></dc:subject>",
	                             &x));
	ASSERT_STR("Ann", x.creator);
	ASSERT_STR("(c) Ann", x.copyright);
	ASSERT_STR("a > b", x.description);
	ASSERT_STR("T", x.title);
	ASSERT_TRUE(x.has_rating);
	ASSERT_EQUAL(4, x.rating);
	ASSERT_STR("2024", x.create_date);
	ASSERT_STR("2025", x.modify_date);
	ASSERT_STR("Red", x.label);
	ASSERT_STR("Pecs", x.city);
	ASSERT_STR("HU", x.country);
	ASSERT_STR("Cr", x.credit);
	ASSERT_STR("Src", x.source);
	ASSERT_TRUE(x.has_keywords);
	ASSERT_STR("one, two", x.keywords);
	img2_xmp_clear(&x);
	ASSERT_NULL(x.keywords);
}

CTEST(exif_xmp, fields_without_text_and_odd_keywords)
{
	img2_xmp_t x;
	ASSERT_EQUAL(IMG2_OK, xmp_of("<dc:creator/><dc:rights/><dc:description/><dc:title/><xmp:Rating/><xmp:CreateDate/>"
	                             "<xmp:ModifyDate/><xmp:Label/><photoshop:City/><photoshop:Country/><photoshop:Credit/>"
	                             "<photoshop:Source/><dc:subject/>",
	                             &x));
	ASSERT_STR("", x.creator);
	ASSERT_FALSE(x.has_rating);
	ASSERT_FALSE(x.has_keywords);
	img2_xmp_clear(&x);

	/* empty bag; and keywords that overflow the 1 KiB buffer are dropped */
	ASSERT_EQUAL(IMG2_OK, xmp_of("<dc:subject><rdf:Bag><rdf:li/></rdf:Bag></dc:subject>", &x));
	ASSERT_FALSE(x.has_keywords);
	img2_xmp_clear(&x);
	static char body[3000];
	char kw[600];
	memset(kw, 'k', sizeof(kw) - 1);
	kw[sizeof(kw) - 1] = '\0';
	snprintf(body, sizeof(body), "<dc:subject><rdf:Seq><rdf:li>%s</rdf:li><rdf:li>%s</rdf:li><rdf:li>z</rdf:li></rdf:Seq></dc:subject>", kw, kw);
	ASSERT_EQUAL(IMG2_OK, xmp_of(body, &x));
	ASSERT_TRUE(x.has_keywords);
	ASSERT_EQUAL(sizeof(kw) - 1 + 3, strlen(x.keywords)); /* first keyword, ", z" */
	img2_xmp_clear(&x);
}

CTEST(exif_xmp, errors)
{
	img2_xmp_t x;
	img2_xmp_init(&x);
	ASSERT_EQUAL(IMG2_E_ARG, img2_xmp_parse_xml(NULL, "<a/>", 4));
	ASSERT_EQUAL(IMG2_E_ARG, img2_xmp_parse_xml(&x, NULL, 4));
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_xmp_parse_xml(&x, "<a>", 3));
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_xmp_parse_xml(&x, "<a>text</a>", 11)); /* no rdf:Description element */
	ASSERT_EQUAL(IMG2_E_ARG, img2_xmp_parse_jpeg(NULL, (const uint8_t *)"x", 1));
	ASSERT_EQUAL(IMG2_E_ARG, img2_xmp_parse_jpeg(&x, NULL, 1));

	/* XMP APP1 in a JPEG; a JPEG without one */
	static const char packet[] = "<x:xmpmeta><rdf:Description><dc:title>J</dc:title></rdf:Description></x:xmpmeta>";
	static uint8_t jpg[512];
	size_t n = jpeg_with(jpg, NULL, 0, "http://ns.adobe.com/xap/1.0/", 29, (const uint8_t *)packet, sizeof(packet) - 1);
	ASSERT_EQUAL(IMG2_OK, img2_xmp_parse(&x, jpg, n));
	ASSERT_STR("J", x.title);
	n = jpeg_with(jpg, NULL, 0, "Exif\0\0", 6, (const uint8_t *)"II*\0", 4);
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_xmp_parse_jpeg(&x, jpg, n));
}
