/**
 * @file test_cov_b_tiff.c
 * @brief TIFF decoder coverage (decoder_tiff.c): multi-page files, the 200-page cap, pages libtiff cannot
 *        read, BigTIFF (no EXIF), unreadable headers, oversized dimensions
 *
 * Fixtures are written in-test with the vendored libtiff through an in-memory TIFFClientOpen handle.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <tiffio.h>

#include "../../libimg2/decoders/decoder.h"
#include "test_cov_b_util.h"

/* ---- in-memory libtiff writer ---- */

typedef struct {
	cb_buf_t buf;
	size_t off;
} tw_mem_t;

static tmsize_t tw_read(thandle_t h, void *p, tmsize_t n)
{
	tw_mem_t *m = (tw_mem_t *)h;
	size_t avail = m->off < m->buf.n ? m->buf.n - m->off : 0;
	size_t k = (size_t)n < avail ? (size_t)n : avail;
	memcpy(p, m->buf.d + m->off, k);
	m->off += k;
	return (tmsize_t)k;
}

static tmsize_t tw_write(thandle_t h, void *p, tmsize_t n)
{
	tw_mem_t *m = (tw_mem_t *)h;
	if (m->off + (size_t)n > m->buf.n) {
		size_t old = m->buf.n;
		size_t grow = m->off + (size_t)n - old;
		uint8_t *zero = calloc(1, grow);
		if (zero == NULL) {
			return -1;
		}
		cb_put(&m->buf, zero, grow);
		free(zero);
	}
	memcpy(m->buf.d + m->off, p, (size_t)n);
	m->off += (size_t)n;
	return n;
}

static toff_t tw_seek(thandle_t h, toff_t off, int whence)
{
	tw_mem_t *m = (tw_mem_t *)h;
	if (whence == SEEK_SET) {
		m->off = (size_t)off;
	} else if (whence == SEEK_CUR) {
		m->off += (size_t)off;
	} else {
		m->off = m->buf.n + (size_t)off;
	}
	return (toff_t)m->off;
}

static int tw_close(thandle_t h)
{
	(void)h;
	return 0;
}

static toff_t tw_size(thandle_t h)
{
	return (toff_t)((tw_mem_t *)h)->buf.n;
}

static int tw_map(thandle_t h, void **base, toff_t *size)
{
	(void)h;
	(void)base;
	(void)size;
	return 0;
}

static void tw_unmap(thandle_t h, void *base, toff_t size)
{
	(void)h;
	(void)base;
	(void)size;
}

static TIFF *tw_open(tw_mem_t *m, int bigtiff)
{
	memset(m, 0, sizeof(*m));
	return TIFFClientOpen("mem", bigtiff ? "w8" : "w", (thandle_t)m, tw_read, tw_write, tw_seek, tw_close, tw_size, tw_map, tw_unmap);
}

/** One directory: w x h, spp samples of 8 bits, photometric, rows_per_strip; strips_written strips of
 *  pattern data (all strips if < 0) */
static void tw_page(TIFF *t, uint32_t w, uint32_t h, uint16_t spp, uint16_t photometric, uint32_t rows_per_strip, uint16_t compression, long strips_written, uint8_t seed)
{
	TIFFSetField(t, TIFFTAG_IMAGEWIDTH, w);
	TIFFSetField(t, TIFFTAG_IMAGELENGTH, h);
	TIFFSetField(t, TIFFTAG_SAMPLESPERPIXEL, spp);
	TIFFSetField(t, TIFFTAG_BITSPERSAMPLE, 8);
	TIFFSetField(t, TIFFTAG_PHOTOMETRIC, photometric);
	TIFFSetField(t, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	TIFFSetField(t, TIFFTAG_ROWSPERSTRIP, rows_per_strip);
	TIFFSetField(t, TIFFTAG_COMPRESSION, compression);
	if (photometric == PHOTOMETRIC_SEPARATED) {
		TIFFSetField(t, TIFFTAG_INKSET, 2); /* multi-ink, not CMYK: TIFFRGBAImageOK refuses it */
	}
	uint32_t strips = (h + rows_per_strip - 1) / rows_per_strip;
	size_t strip_bytes = (size_t)w * spp * rows_per_strip;
	uint8_t *buf = malloc(strip_bytes);
	if (buf == NULL) {
		abort();
	}
	for (size_t i = 0; i < strip_bytes; i++) {
		buf[i] = (uint8_t)(seed + i * 7);
	}
	for (uint32_t s = 0; s < strips && (strips_written < 0 || (long)s < strips_written); s++) {
		uint32_t rows = (s + 1) * rows_per_strip <= h ? rows_per_strip : h - s * rows_per_strip;
		TIFFWriteEncodedStrip(t, s, buf, (tmsize_t)((size_t)w * spp * rows));
	}
	free(buf);
	TIFFWriteDirectory(t);
}

static void tw_finish(TIFF *t, tw_mem_t *m)
{
	TIFFClose(t);
	(void)m;
}

static img2_status_t tiff_decode(const tw_mem_t *m, img2_frames_t *out)
{
	return img2_decode_tiff(m->buf.d, m->buf.n, NULL, out);
}

/* ---- tests ---- */

CTEST(cov_b_tiff, multi_page)
{
	tw_mem_t m;
	TIFF *t = tw_open(&m, 0);
	ASSERT_NOT_NULL(t);
	for (int p = 0; p < 3; p++) {
		tw_page(t, 2, 2, 3, PHOTOMETRIC_RGB, 2, COMPRESSION_NONE, -1, (uint8_t)(p * 50));
	}
	tw_finish(t, &m);

	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, tiff_decode(&m, &fr));
	ASSERT_EQUAL(3, fr.count);
	for (int p = 0; p < 3; p++) {
		CB_ASSERT_PX(&fr, p, 0, 0, p * 50, p * 50 + 7, p * 50 + 14, 255);
	}
	ASSERT_NOT_NULL(fr.frames[0]->exif); /* the file itself is the IFD structure */
	ASSERT_NULL(fr.frames[1]->exif);
	img2_frames_free(&fr);
	cb_free(&m.buf);
}

CTEST(cov_b_tiff, page_cap)
{
	tw_mem_t m;
	TIFF *t = tw_open(&m, 0);
	ASSERT_NOT_NULL(t);
	for (int p = 0; p < 201; p++) {
		tw_page(t, 1, 1, 1, PHOTOMETRIC_MINISBLACK, 1, COMPRESSION_NONE, -1, (uint8_t)p);
	}
	tw_finish(t, &m);

	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, tiff_decode(&m, &fr));
	ASSERT_EQUAL(200, fr.count);
	CB_ASSERT_PX(&fr, 199, 0, 0, 199, 199, 199, 255);
	img2_frames_free(&fr);
	cb_free(&m.buf);
}

CTEST(cov_b_tiff, bigtiff_has_no_exif)
{
	img2_frames_t fr;
	for (int pages = 1; pages <= 2; pages++) {
		tw_mem_t m;
		TIFF *t = tw_open(&m, 1);
		ASSERT_NOT_NULL(t);
		for (int p = 0; p < pages; p++) {
			tw_page(t, 1, 1, 3, PHOTOMETRIC_RGB, 1, COMPRESSION_NONE, -1, 9);
		}
		tw_finish(t, &m);
		/* img2_exif_parse_tiff() only reads classic TIFF (magic 42) */
		ASSERT_EQUAL(IMG2_OK, tiff_decode(&m, &fr));
		ASSERT_EQUAL(pages, fr.count);
		ASSERT_NULL(fr.frames[0]->exif);
		CB_ASSERT_PX(&fr, 0, 0, 0, 9, 16, 23, 255);
		img2_frames_free(&fr);
		cb_free(&m.buf);
	}
}

CTEST(cov_b_tiff, unreadable_pages)
{
	img2_frames_t fr;
	tw_mem_t m;

	/* single page libtiff's RGBA interface refuses (separated, InkSet multi-ink) */
	TIFF *t = tw_open(&m, 0);
	ASSERT_NOT_NULL(t);
	tw_page(t, 2, 1, 4, PHOTOMETRIC_SEPARATED, 1, COMPRESSION_NONE, -1, 1);
	tw_finish(t, &m);
	ASSERT_EQUAL(IMG2_E_CODEC, tiff_decode(&m, &fr));
	ASSERT_NULL(fr.frames);
	cb_free(&m.buf);

	/* the same as the second page of two */
	t = tw_open(&m, 0);
	ASSERT_NOT_NULL(t);
	tw_page(t, 2, 1, 3, PHOTOMETRIC_RGB, 1, COMPRESSION_NONE, -1, 1);
	tw_page(t, 2, 1, 4, PHOTOMETRIC_SEPARATED, 1, COMPRESSION_NONE, -1, 1);
	tw_finish(t, &m);
	ASSERT_EQUAL(IMG2_E_CODEC, tiff_decode(&m, &fr));
	ASSERT_NULL(fr.frames);
	cb_free(&m.buf);

	/* a second directory without ImageLength: counted by TIFFNumberOfDirectories, TIFFSetDirectory fails */
	t = tw_open(&m, 0);
	ASSERT_NOT_NULL(t);
	tw_page(t, 2, 1, 3, PHOTOMETRIC_RGB, 1, COMPRESSION_NONE, -1, 1);
	tw_page(t, 2, 1, 3, PHOTOMETRIC_RGB, 1, COMPRESSION_NONE, -1, 1);
	tw_finish(t, &m);
	size_t hit = 0;
	for (size_t i = 8; i + 8 <= m.buf.n; i++) {
		/* IFD entry: tag 257 (ImageLength), type SHORT or LONG, count 1 */
		if (m.buf.d[i] == 0x01 && m.buf.d[i + 1] == 0x01 && (m.buf.d[i + 2] == 3 || m.buf.d[i + 2] == 4) && m.buf.d[i + 3] == 0 && m.buf.d[i + 4] == 1 && m.buf.d[i + 5] == 0 && m.buf.d[i + 6] == 0 && m.buf.d[i + 7] == 0) {
			hit = i; /* keep the last one: the second directory */
		}
	}
	ASSERT_TRUE(hit > 0);
	m.buf.d[hit] = 0x50; /* tag 0xC350: unknown (and out of order: a warning) */
	m.buf.d[hit + 1] = 0xC3;
	ASSERT_EQUAL(IMG2_E_CODEC, tiff_decode(&m, &fr));
	ASSERT_NULL(fr.frames);
	cb_free(&m.buf);

	/* the only strip starts exactly at the end of the data: the seek succeeds, the read finds nothing */
	t = tw_open(&m, 0);
	ASSERT_NOT_NULL(t);
	tw_page(t, 2, 1, 3, PHOTOMETRIC_RGB, 1, COMPRESSION_NONE, -1, 1);
	tw_finish(t, &m);
	hit = 0;
	for (size_t i = 8; i + 12 <= m.buf.n; i++) {
		/* IFD entry: tag 273 (StripOffsets), LONG, count 1 */
		if (m.buf.d[i] == 0x11 && m.buf.d[i + 1] == 0x01 && m.buf.d[i + 2] == 4 && m.buf.d[i + 3] == 0 && m.buf.d[i + 4] == 1 && m.buf.d[i + 5] == 0 && m.buf.d[i + 6] == 0 && m.buf.d[i + 7] == 0) {
			hit = i;
		}
	}
	ASSERT_TRUE(hit > 0);
	uint32_t end = (uint32_t)m.buf.n;
	memcpy(m.buf.d + hit + 8, &end, 4); /* little-endian host, as libtiff wrote the file */
	ASSERT_EQUAL(IMG2_E_CODEC, tiff_decode(&m, &fr));
	ASSERT_NULL(fr.frames);
	cb_free(&m.buf);
}

CTEST(cov_b_tiff, unreadable_header)
{
	/* first IFD offset beyond the end of the data: the seek is clamped, libtiff cannot open the file */
	static const uint8_t bad[16] = { 'I', 'I', 42, 0, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_E_CODEC, img2_decode_tiff(bad, sizeof(bad), NULL, &fr));
	ASSERT_NULL(fr.frames);
}

CTEST(cov_b_tiff, oversized_dimensions_are_rejected)
{
	/* 65537 x 65536: width * height wraps to 65536 in 32 bits. Only the first strip (2 rows) holds data. */
	img2_frames_t fr;
	tw_mem_t m;
	TIFF *t = tw_open(&m, 0);
	ASSERT_NOT_NULL(t);
	tw_page(t, 65537, 65536, 1, PHOTOMETRIC_MINISBLACK, 2, COMPRESSION_PACKBITS, 1, 0);
	tw_finish(t, &m);
	ASSERT_EQUAL(IMG2_E_LIMIT, tiff_decode(&m, &fr));
	ASSERT_NULL(fr.frames);
	cb_free(&m.buf);

	/* the same as the second page of a multi-page file */
	t = tw_open(&m, 0);
	ASSERT_NOT_NULL(t);
	tw_page(t, 1, 1, 1, PHOTOMETRIC_MINISBLACK, 1, COMPRESSION_NONE, -1, 0);
	tw_page(t, 65537, 65536, 1, PHOTOMETRIC_MINISBLACK, 2, COMPRESSION_PACKBITS, 1, 0);
	tw_finish(t, &m);
	ASSERT_EQUAL(IMG2_E_LIMIT, tiff_decode(&m, &fr));
	ASSERT_NULL(fr.frames);
	cb_free(&m.buf);
}

CTEST(cov_b_tiff, argument_checks)
{
	const uint8_t one = 'I';
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_tiff(&one, 1, NULL, NULL));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_tiff(NULL, 1, NULL, &fr));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_tiff(&one, 0, NULL, &fr));
}
