/**
 * @file cov_c_util.h
 * @brief Coverage lane C test helpers: big-endian writers, a TIFF/EXIF block builder, noise
 *
 * Header-only (static functions), test code only.
 */

#ifndef IMG2_TESTS_COV_C_UTIL_H
#define IMG2_TESTS_COV_C_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline void cov_put_be16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

static inline void cov_put_be32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

/* xorshift noise (incompressible test images) */
static inline void cov_fill_noise(uint8_t *p, size_t n, uint32_t seed)
{
	uint32_t x = seed ? seed : 1u;
	for (size_t i = 0; i < n; i++) {
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		p[i] = (uint8_t)x;
	}
}

/* Fields of cov_tiff_build() (each sets one of the "has data" conditions of the decoders) */
enum {
	COV_T_MAKE = 1u << 0,     /* IFD0 Make "Make1" */
	COV_T_MODEL = 1u << 1,    /* IFD0 Model "Model" */
	COV_T_SOFTWARE = 1u << 2, /* IFD0 Software "Soft1" */
	COV_T_DESC = 1u << 3,     /* IFD0 ImageDescription "Desc1" */
	COV_T_ORIENT6 = 1u << 4,  /* IFD0 Orientation 6 */
	COV_T_ORIENT1 = 1u << 5,  /* IFD0 Orientation 1 (no information) */
	COV_T_ISO = 1u << 6,      /* ExifIFD ISOSpeed 200 */
	COV_T_EXPO = 1u << 7,     /* ExifIFD ExposureTime 1/100 */
	COV_T_FNUM = 1u << 8,     /* ExifIFD FNumber 28/10 */
	COV_T_FOCAL = 1u << 9,    /* ExifIFD FocalLength 50/1 */
	COV_T_GPS = 1u << 10,     /* GPS IFD LatitudeRef 'N' */
};

typedef struct {
	uint8_t *t;
	size_t data; /* next free byte of the value area */
	int be;
} cov_tiff_t;

static inline void cov_tw16(const cov_tiff_t *c, size_t at, uint16_t v)
{
	if (c->be) {
		c->t[at] = (uint8_t)(v >> 8);
		c->t[at + 1] = (uint8_t)v;
	} else {
		c->t[at] = (uint8_t)v;
		c->t[at + 1] = (uint8_t)(v >> 8);
	}
}

static inline void cov_tw32(const cov_tiff_t *c, size_t at, uint32_t v)
{
	if (c->be) {
		cov_put_be32(c->t + at, v);
	} else {
		c->t[at] = (uint8_t)v;
		c->t[at + 1] = (uint8_t)(v >> 8);
		c->t[at + 2] = (uint8_t)(v >> 16);
		c->t[at + 3] = (uint8_t)(v >> 24);
	}
}

/* entry e of the IFD at ifd: tag, type, count, value (inline value or offset) */
static inline void cov_tentry(const cov_tiff_t *c, size_t ifd, int e, uint16_t tag, uint16_t type, uint32_t count, uint32_t value)
{
	size_t at = ifd + 2 + (size_t)e * 12;
	cov_tw16(c, at, tag);
	cov_tw16(c, at + 2, type);
	cov_tw32(c, at + 4, count);
	if (type == 3 && count == 1) {
		cov_tw16(c, at + 8, (uint16_t)value); /* SHORT inline: left-justified */
		cov_tw16(c, at + 10, 0);
	} else {
		cov_tw32(c, at + 8, value);
	}
}

/* ASCII value stored out of line (count > 4); returns its offset */
static inline uint32_t cov_tstr(cov_tiff_t *c, const char *s)
{
	size_t n = strlen(s) + 1;
	memcpy(c->t + c->data, s, n);
	uint32_t off = (uint32_t)c->data;
	c->data += (n + 1) & ~(size_t)1;
	return off;
}

static inline uint32_t cov_trational(cov_tiff_t *c, uint32_t num, uint32_t den)
{
	uint32_t off = (uint32_t)c->data;
	cov_tw32(c, c->data, num);
	cov_tw32(c, c->data + 4, den);
	c->data += 8;
	return off;
}

/*
 * TIFF block ("II*\0" or "MM\0*") with the fields of mask (COV_T_*) in IFD0 / ExifIFD / GPS IFD. A Compression
 * tag is always present, so even mask 0 parses. Needs up to 512 bytes at out; returns the length.
 */
static inline size_t cov_tiff_build(uint8_t *out, unsigned mask, int big_endian)
{
	cov_tiff_t c = { out, 0, big_endian };
	memset(out, 0, 512);
	out[0] = out[1] = big_endian ? 'M' : 'I';
	cov_tw16(&c, 2, 42);
	cov_tw32(&c, 4, 8);

	/* IFD0 at 8 with up to 9 entries, ExifIFD at 128 with up to 4, GPS IFD at 192 with 1, values from 224 */
	const size_t ifd0 = 8;
	const size_t exif_ifd = 128;
	const size_t gps_ifd = 192;
	c.data = 224;
	int n = 0;
	cov_tentry(&c, ifd0, n++, 0x0103, 3, 1, 1); /* Compression */
	if (mask & COV_T_MAKE) {
		cov_tentry(&c, ifd0, n++, 0x010F, 2, 6, cov_tstr(&c, "Make1"));
	}
	if (mask & COV_T_MODEL) {
		cov_tentry(&c, ifd0, n++, 0x0110, 2, 6, cov_tstr(&c, "Model"));
	}
	if (mask & COV_T_SOFTWARE) {
		cov_tentry(&c, ifd0, n++, 0x0131, 2, 6, cov_tstr(&c, "Soft1"));
	}
	if (mask & COV_T_DESC) {
		cov_tentry(&c, ifd0, n++, 0x010E, 2, 6, cov_tstr(&c, "Desc1"));
	}
	if (mask & (COV_T_ORIENT6 | COV_T_ORIENT1)) {
		cov_tentry(&c, ifd0, n++, 0x0112, 3, 1, (mask & COV_T_ORIENT6) ? 6 : 1);
	}
	int ne = 0;
	if (mask & COV_T_ISO) {
		cov_tentry(&c, exif_ifd, ne++, 0x8827, 3, 1, 200);
	}
	if (mask & COV_T_EXPO) {
		cov_tentry(&c, exif_ifd, ne++, 0x829A, 5, 1, cov_trational(&c, 1, 100));
	}
	if (mask & COV_T_FNUM) {
		cov_tentry(&c, exif_ifd, ne++, 0x829D, 5, 1, cov_trational(&c, 28, 10));
	}
	if (mask & COV_T_FOCAL) {
		cov_tentry(&c, exif_ifd, ne++, 0x920A, 5, 1, cov_trational(&c, 50, 1));
	}
	if (ne > 0) {
		cov_tw16(&c, exif_ifd, (uint16_t)ne);
		cov_tentry(&c, ifd0, n++, 0x8769, 4, 1, (uint32_t)exif_ifd);
	}
	if (mask & COV_T_GPS) {
		cov_tw16(&c, gps_ifd, 1);
		cov_tentry(&c, gps_ifd, 0, 0x0001, 2, 2, big_endian ? 0x4E000000u : 0x4Eu);
		cov_tentry(&c, ifd0, n++, 0x8825, 4, 1, (uint32_t)gps_ifd);
	}
	cov_tw16(&c, ifd0, (uint16_t)n);
	return c.data;
}

#endif /* IMG2_TESTS_COV_C_UTIL_H */
