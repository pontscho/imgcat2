/**
 * @file test_cov_b_dng.h
 * @brief Minimal little-endian TIFF / DNG writer of the coverage lane B RAW tests (test_cov_b_raw.c,
 *        test_cov_b_alloc.c): a 64x48 16-bit raw (CFA RGGB or monochrome LinearRaw) with optional previews
 *        and metadata, accepted by LibRaw
 *
 * Header-only (static inline functions); needs test_cov_b_util.h and <jpeglib.h> first.
 */

#ifndef IMG2_TESTS_COV_B_DNG_H
#define IMG2_TESTS_COV_B_DNG_H

enum { T_BYTE = 1, T_ASCII = 2, T_SHORT = 3, T_LONG = 4, T_RATIONAL = 5, T_SRATIONAL = 10 };

typedef struct {
	uint16_t tag;
	uint16_t type;
	uint32_t count;
	cb_buf_t val; /* little-endian value bytes */
} dng_ent_t;

typedef struct {
	dng_ent_t e[48];
	int n;
} dng_ifd_t;

static inline dng_ent_t *dng_add(dng_ifd_t *ifd, uint16_t tag, uint16_t type, uint32_t count)
{
	if (ifd->n >= 48) {
		abort();
	}
	dng_ent_t *e = &ifd->e[ifd->n++];
	memset(e, 0, sizeof(*e));
	e->tag = tag;
	e->type = type;
	e->count = count;
	return e;
}

static inline void dng_short(dng_ifd_t *ifd, uint16_t tag, unsigned v)
{
	cb_le16(&dng_add(ifd, tag, T_SHORT, 1)->val, v);
}

static inline void dng_long(dng_ifd_t *ifd, uint16_t tag, uint32_t v)
{
	cb_le32(&dng_add(ifd, tag, T_LONG, 1)->val, v);
}

static inline void dng_ascii(dng_ifd_t *ifd, uint16_t tag, const char *s)
{
	dng_ent_t *e = dng_add(ifd, tag, T_ASCII, (uint32_t)strlen(s) + 1);
	cb_put(&e->val, s, strlen(s) + 1);
}

static inline void dng_bytes(dng_ifd_t *ifd, uint16_t tag, uint16_t type, const void *p, uint32_t n)
{
	cb_put(&dng_add(ifd, tag, type, n)->val, p, n);
}

static inline void dng_rationals(dng_ifd_t *ifd, uint16_t tag, uint16_t type, int n, const int32_t *num, const int32_t *den)
{
	dng_ent_t *e = dng_add(ifd, tag, type, (uint32_t)n);
	for (int i = 0; i < n; i++) {
		cb_le32(&e->val, (uint32_t)num[i]);
		cb_le32(&e->val, (uint32_t)den[i]);
	}
}

static inline int dng_cmp(const void *a, const void *b)
{
	return (int)((const dng_ent_t *)a)->tag - (int)((const dng_ent_t *)b)->tag;
}

/** Append the IFD (values > 4 bytes first, then the sorted entries); returns its offset; frees the values */
static inline uint32_t dng_write_ifd(cb_buf_t *out, dng_ifd_t *ifd, uint32_t next)
{
	qsort(ifd->e, (size_t)ifd->n, sizeof(ifd->e[0]), dng_cmp);
	uint32_t ext[48];
	for (int i = 0; i < ifd->n; i++) {
		if (out->n & 1) {
			cb_u8(out, 0);
		}
		ext[i] = (uint32_t)out->n;
		if (ifd->e[i].val.n > 4) {
			cb_put(out, ifd->e[i].val.d, ifd->e[i].val.n);
		}
	}
	if (out->n & 1) {
		cb_u8(out, 0);
	}
	uint32_t at = (uint32_t)out->n;
	cb_le16(out, (unsigned)ifd->n);
	for (int i = 0; i < ifd->n; i++) {
		dng_ent_t *e = &ifd->e[i];
		cb_le16(out, e->tag);
		cb_le16(out, e->type);
		cb_le32(out, e->count);
		if (e->val.n > 4) {
			cb_le32(out, ext[i]);
		} else {
			uint8_t v[4] = { 0, 0, 0, 0 };
			memcpy(v, e->val.d, e->val.n);
			cb_put(out, v, 4);
		}
		cb_free(&e->val);
	}
	cb_le32(out, next);
	ifd->n = 0;
	return at;
}

/** Preview kinds of dng_build() */
enum { PREV_NONE, PREV_RGB, PREV_GRAY, PREV_BAD_JPEG, PREV_GRAY_ALPHA, PREV_SHORT, PREV_JPEG_XMP };

#define DNG_W 64
#define DNG_H 48
#define PREV_W 16
#define PREV_H 12

typedef struct {
	int preview;       /* PREV_* */
	int full_meta;     /* EXIF / GPS / text / XMP tags */
	unsigned orientation;
	const char *xmp;   /* XMLPacket, NULL: none */
	const char *desc;  /* ImageDescription, NULL: none */
	int mono;          /* LinearRaw, one sample (instead of a CFA) */
	int truncate_raw;  /* raw strip offset past the end of the file */
	int gps_ne;        /* GPS references N / E instead of S / W */
	int no_camera;     /* no Make, Model, UniqueCameraModel */
	int not_dng;       /* no DNGVersion: a plain CFA TIFF */
} dng_opts_t;

/** JPEG preview (16x12) with an XMP APP1 segment, encoded in-test; free() the result */
static inline uint8_t *dng_jpeg_preview(size_t *len)
{
	struct jpeg_compress_struct c;
	struct jpeg_error_mgr e;
	c.err = jpeg_std_error(&e);
	jpeg_create_compress(&c);
	unsigned char *out = NULL;
	unsigned long out_len = 0;
	jpeg_mem_dest(&c, &out, &out_len);
	c.image_width = PREV_W;
	c.image_height = PREV_H;
	c.input_components = 3;
	c.in_color_space = JCS_RGB;
	jpeg_set_defaults(&c);
	jpeg_start_compress(&c, TRUE);
	static const char xmp_app1[] = "http://ns.adobe.com/xap/1.0/\0<r><d><dc:creator>Preview</dc:creator></d></r>";
	jpeg_write_marker(&c, JPEG_APP0 + 1, (const JOCTET *)xmp_app1, (unsigned)sizeof(xmp_app1) - 1);
	JSAMPLE row[PREV_W * 3];
	memset(row, 0x80, sizeof(row));
	JSAMPROW rp[1] = { row };
	while (c.next_scanline < c.image_height) {
		jpeg_write_scanlines(&c, rp, 1);
	}
	jpeg_finish_compress(&c);
	jpeg_destroy_compress(&c);
	*len = out_len;
	return out;
}

/** Minimal DNG: IFD0 = raw (16-bit CFA RGGB, or LinearRaw mono), IFD1 = optional preview */
static inline void dng_build(cb_buf_t *b, const dng_opts_t *o)
{
	cb_str(b, "II");
	cb_le16(b, 42);
	cb_le32(b, 0); /* IFD0 offset, patched */

	/* raw pixels: a gentle gradient */
	uint32_t raw_off = (uint32_t)b->n;
	for (int y = 0; y < DNG_H; y++) {
		for (int x = 0; x < DNG_W; x++) {
			cb_le16(b, (unsigned)(2000 + x * 300 + y * 200));
		}
	}
	uint32_t raw_len = DNG_W * DNG_H * 2;

	/* preview pixels */
	uint32_t prev_off = (uint32_t)b->n;
	uint32_t prev_len = 0;
	const int prev_ch = (o->preview == PREV_GRAY) ? 1 : (o->preview == PREV_GRAY_ALPHA ? 2 : 3);
	if (o->preview == PREV_RGB || o->preview == PREV_GRAY || o->preview == PREV_GRAY_ALPHA || o->preview == PREV_SHORT) {
		for (int i = 0; i < PREV_W * PREV_H * prev_ch; i++) {
			cb_u8(b, (unsigned)(i % 251));
		}
		/* PREV_SHORT: the strip byte count is 7 bytes short of width x height x 3 */
		prev_len = (uint32_t)(PREV_W * PREV_H * prev_ch) - (o->preview == PREV_SHORT ? 7u : 0u);
	} else if (o->preview == PREV_BAD_JPEG || o->preview == PREV_JPEG_XMP) {
		size_t jl = 0;
		uint8_t *j = dng_jpeg_preview(&jl);
		if (j == NULL) {
			abort();
		}
		if (o->preview == PREV_BAD_JPEG) {
			/* SOF0 sample precision 8 -> 12: LibRaw passes the preview on, libjpeg cannot decode it */
			for (size_t i = 0; i + 4 < jl; i++) {
				if (j[i] == 0xFF && j[i + 1] == 0xC0) {
					j[i + 4] = 12;
					break;
				}
			}
		}
		cb_put(b, j, jl);
		free(j);
		prev_len = (uint32_t)jl;
	}

	uint32_t exif_off = 0;
	uint32_t gps_off = 0;
	dng_ifd_t ifd;
	ifd.n = 0;
	if (o->full_meta) {
		const int32_t one[1] = { 1 };
		dng_rationals(&ifd, 33434, T_RATIONAL, 1, one, (const int32_t[]){ 250 });   /* ExposureTime 1/250 */
		dng_rationals(&ifd, 33437, T_RATIONAL, 1, (const int32_t[]){ 28 }, (const int32_t[]){ 10 }); /* FNumber */
		dng_short(&ifd, 34855, 400);                                                     /* ISO */
		dng_rationals(&ifd, 37386, T_RATIONAL, 1, (const int32_t[]){ 50 }, one);       /* FocalLength */
		dng_short(&ifd, 41989, 75);                                                      /* FocalLengthIn35mmFilm */
		dng_rationals(&ifd, 42034, T_RATIONAL, 4, (const int32_t[]){ 24, 70, 28, 28 }, (const int32_t[]){ 1, 1, 10, 10 });
		dng_ascii(&ifd, 42035, "LensCo");                                                /* LensMake */
		dng_ascii(&ifd, 42036, "Zoom 24-70");                                            /* LensModel */
		exif_off = dng_write_ifd(b, &ifd, 0);

		dng_bytes(&ifd, 0, T_BYTE, "\x02\x03\x00\x00", 4);                             /* GPSVersionID */
		dng_ascii(&ifd, 1, o->gps_ne ? "N" : "S");
		dng_rationals(&ifd, 2, T_RATIONAL, 3, (const int32_t[]){ 33, 30, 0 }, (const int32_t[]){ 1, 1, 1 });
		dng_ascii(&ifd, 3, o->gps_ne ? "E" : "W");
		dng_rationals(&ifd, 4, T_RATIONAL, 3, (const int32_t[]){ 70, 15, 36 }, (const int32_t[]){ 1, 1, 1 });
		dng_bytes(&ifd, 5, T_BYTE, "\x00", 1);
		dng_rationals(&ifd, 6, T_RATIONAL, 1, (const int32_t[]){ 120 }, one);
		gps_off = dng_write_ifd(b, &ifd, 0);
	}

	uint32_t ifd1 = 0;
	if (o->preview == PREV_BAD_JPEG || o->preview == PREV_JPEG_XMP) {
		/* old-style JPEG preview (JPEGInterchangeFormat): LibRaw passes the bytes on as a JPEG */
		dng_long(&ifd, 254, 1);
		dng_long(&ifd, 256, PREV_W);
		dng_long(&ifd, 257, PREV_H);
		dng_short(&ifd, 259, 6);
		dng_long(&ifd, 513, prev_off);
		dng_long(&ifd, 514, prev_len);
		ifd1 = dng_write_ifd(b, &ifd, 0);
	} else if (o->preview != PREV_NONE) {
		const int grey = o->preview == PREV_GRAY || o->preview == PREV_GRAY_ALPHA;
		dng_long(&ifd, 254, 1);
		dng_long(&ifd, 256, PREV_W);
		dng_long(&ifd, 257, PREV_H);
		dng_short(&ifd, 258, 8);
		dng_short(&ifd, 259, 1);
		dng_short(&ifd, 262, grey ? 1 : 2);
		dng_long(&ifd, 273, prev_off);
		dng_short(&ifd, 277, (unsigned)prev_ch);
		dng_long(&ifd, 278, PREV_H);
		dng_long(&ifd, 279, prev_len);
		dng_short(&ifd, 284, 1);
		ifd1 = dng_write_ifd(b, &ifd, 0);
	}

	dng_long(&ifd, 254, 0);
	dng_long(&ifd, 256, DNG_W);
	dng_long(&ifd, 257, DNG_H);
	dng_short(&ifd, 258, 16);
	dng_short(&ifd, 259, 1);
	dng_short(&ifd, 262, o->mono ? 34892 : 32803);
	if (o->full_meta) {
		dng_ascii(&ifd, 271, "Img2Cam");
		dng_ascii(&ifd, 272, "Model 1");
		dng_ascii(&ifd, 305, "img2-test");
		dng_ascii(&ifd, 306, "2021:06:15 12:34:56");
		dng_ascii(&ifd, 315, "Ann Artist");
		dng_long(&ifd, 34665, exif_off);
		dng_long(&ifd, 34853, gps_off);
	}
	if (o->desc != NULL) {
		dng_ascii(&ifd, 270, o->desc);
	}
	dng_long(&ifd, 273, o->truncate_raw ? 0x7FFFFF00u : raw_off);
	if (o->orientation != 0) {
		dng_short(&ifd, 274, o->orientation);
	}
	dng_short(&ifd, 277, 1);
	dng_long(&ifd, 278, DNG_H);
	dng_long(&ifd, 279, raw_len);
	dng_short(&ifd, 284, 1);
	if (o->xmp != NULL) {
		dng_bytes(&ifd, 700, T_BYTE, o->xmp, (uint32_t)strlen(o->xmp));
	}
	if (!o->mono) {
		dng_ent_t *dim = dng_add(&ifd, 33421, T_SHORT, 2); /* CFARepeatPatternDim 2x2 */
		cb_le16(&dim->val, 2);
		cb_le16(&dim->val, 2);
		dng_bytes(&ifd, 33422, T_BYTE, "\x00\x01\x01\x02", 4); /* CFAPattern RGGB */
	}
	if (!o->not_dng) {
		dng_bytes(&ifd, 50706, T_BYTE, "\x01\x04\x00\x00", 4);              /* DNGVersion */
	}
	if (!o->no_camera) {
		dng_ascii(&ifd, 50708, "Img2 Test Camera");                          /* UniqueCameraModel */
	}
	dng_long(&ifd, 50717, 65535);                                            /* WhiteLevel */
	dng_rationals(&ifd, 50721, T_SRATIONAL, 9, (const int32_t[]){ 10, -3, -1, -4, 12, 1, -1, 2, 6 }, (const int32_t[]){ 10, 10, 10, 10, 10, 10, 10, 10, 10 });
	dng_rationals(&ifd, 50728, T_RATIONAL, 3, (const int32_t[]){ 5, 1, 6 }, (const int32_t[]){ 10, 1, 10 }); /* AsShotNeutral */
	uint32_t ifd0 = dng_write_ifd(b, &ifd, ifd1);
	b->d[4] = (uint8_t)ifd0;
	b->d[5] = (uint8_t)(ifd0 >> 8);
	b->d[6] = (uint8_t)(ifd0 >> 16);
	b->d[7] = (uint8_t)(ifd0 >> 24);
}

#endif /* IMG2_TESTS_COV_B_DNG_H */
