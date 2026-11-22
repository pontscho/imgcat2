/**
 * @file test_f_residual_limits.c
 * @brief [F16] [F14] residuals (CWE-770): GIF image descriptors and TIFF pages are checked against
 *        IMG2_MAX_PIXELS / IMG2_MAX_TOTAL_PIXELS before the raster buffers are allocated
 *
 * [F16] gif_walk_records() validated only the logical screen; gif_slurp_capped() then allocated width x height
 * bytes per image descriptor (bounded by INT_MAX only), for up to 200 images. [F14] the multi-page TIFF decoder
 * had no running pixel total: every page was allocated at its own (per-page checked) size.
 *
 * Proof that no large allocation happens: the executable is linked with -Wl,--wrap=malloc,--wrap=calloc,
 * --wrap=realloc (the img2 archive and the vendored giflib / libtiff are static); the wrappers fail and count
 * every request above 256 MiB (as test_sec_fix_limits.c). Each crafted file must return IMG2_E_LIMIT with that
 * count at zero.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../libimg2/core/image.h"
#include "../../libimg2/decoders/decoder.h"
#include "test_cov_b_util.h"

/* ---- allocation wrappers: fail and count every request above FR_BIG ---- */

#define FR_BIG ((size_t)256 << 20)

void *__real_malloc(size_t size);
void *__real_calloc(size_t nmemb, size_t size);
void *__real_realloc(void *ptr, size_t size);
void *__wrap_malloc(size_t size);
void *__wrap_calloc(size_t nmemb, size_t size);
void *__wrap_realloc(void *ptr, size_t size);

static long s_big; /* requests above FR_BIG since the last reset */

void *__wrap_malloc(size_t size)
{
	if (size > FR_BIG) {
		s_big++;
		return NULL;
	}
	return __real_malloc(size);
}

void *__wrap_calloc(size_t nmemb, size_t size)
{
	if (nmemb != 0 && size > FR_BIG / nmemb) {
		s_big++;
		return NULL;
	}
	return __real_calloc(nmemb, size);
}

void *__wrap_realloc(void *ptr, size_t size)
{
	if (size > FR_BIG) {
		s_big++;
		return NULL;
	}
	return __real_realloc(ptr, size);
}

typedef img2_status_t (*fr_fn_t)(const uint8_t *, size_t, const img2_decode_opts_t *, img2_frames_t *);

static int s_bad; /* failed cases of the running test (fr_check() asserts and resets) */

/* fn(data) must return IMG2_E_LIMIT without any allocation above FR_BIG */
static void fr_limit_at(int line, fr_fn_t fn, const uint8_t *data, size_t len)
{
	img2_frames_t fr = { 0 };
	s_big = 0;
	img2_set_log_callback(cb_log_sink, NULL);
	img2_status_t st = fn(data, len, NULL, &fr);
	img2_set_log_callback(NULL, NULL);
	if (s_big != 0 || st != IMG2_E_LIMIT || fr.frames != NULL) {
		fprintf(stderr, "  line %d: status %d (%s), %ld allocation(s) above 256 MiB\n", line, (int)st, img2_strerror(st), s_big);
		s_bad++;
	}
	img2_frames_free(&fr);
}

#define fr_expect_limit(fn, data, len) fr_limit_at(__LINE__, fn, data, len)

static void fr_check(void)
{
	int bad = s_bad;
	s_bad = 0;
	ASSERT_EQUAL(0, bad);
}

/* ===== [F16] GIF ===== */

#ifdef IMG2_HAVE_GIFLIB
static const uint8_t k_pal4[12] = { 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255 };

/* Image descriptor w x h with a minimal raster (one pixel, then EOI): the walk never decodes it */
static void gif_big_image(cb_buf_t *b, unsigned w, unsigned h)
{
	const uint8_t px = 1;
	gif_image(b, 0, 0, w, h, 0, -1, NULL, 2, &px, 1);
}

CTEST(f_residual_limits, gif_descriptor_beyond_max_pixels)
{
	/* 65535 x 65535 (4.3e9 pixels; the slurp's INT_MAX check made it CODEC) and 46340 x 46340 (2.1e9 < INT_MAX:
	 * a 2 GiB raster was allocated) on a 1 x 1 logical screen */
	const unsigned sides[2] = { 65535, 46340 };
	for (int i = 0; i < 2; i++) {
		cb_buf_t b = { 0 };
		gif_header(&b, 1, 1, 1, k_pal4);
		gif_big_image(&b, sides[i], sides[i]);
		cb_u8(&b, 0x3B);
		fr_expect_limit(img2_decode_gif_animated, b.d, b.n);
		/* the probe walks without size checks: one image, not animated */
		ASSERT_FALSE(img2_gif_is_animated(b.d, b.n));
		cb_free(&b);
	}

	/* the second image is the oversized one: rejected before the first raster is allocated as well */
	cb_buf_t b = { 0 };
	gif_header(&b, 1, 1, 1, k_pal4);
	gif_big_image(&b, 1, 1);
	gif_big_image(&b, 30000, 30000);
	cb_u8(&b, 0x3B);
	fr_expect_limit(img2_decode_gif_animated, b.d, b.n);
	ASSERT_TRUE(img2_gif_is_animated(b.d, b.n));
	cb_free(&b);
	fr_check();
}

CTEST(f_residual_limits, gif_descriptors_beyond_total_pixels)
{
	/* six 14000 x 14000 descriptors (1.96e8 each, within IMG2_MAX_PIXELS; 1.18e9 together > 2^30) on a 16 x 16
	 * screen: the canvas total is tiny, the rasters' total is not */
	cb_buf_t b = { 0 };
	gif_header(&b, 16, 16, 1, k_pal4);
	for (int i = 0; i < 6; i++) {
		gif_gce(&b, 1, 10, -1);
		gif_big_image(&b, 14000, 14000);
	}
	cb_u8(&b, 0x3B);
	fr_expect_limit(img2_decode_gif_animated, b.d, b.n);
	cb_free(&b);
	fr_check();
}

CTEST(f_residual_limits, gif_descriptor_beyond_frame_cap_not_checked)
{
	/* 200 1 x 1 images, then a 65535 x 65535 one: beyond the 200-frame cap it is never read by the decode */
	cb_buf_t b = { 0 };
	gif_header(&b, 1, 1, 1, k_pal4);
	for (int i = 0; i < 200; i++) {
		gif_gce(&b, 1, 10, -1);
		gif_big_image(&b, 1, 1);
	}
	gif_big_image(&b, 65535, 65535);
	cb_u8(&b, 0x3B);
	img2_frames_t fr = { 0 };
	s_big = 0;
	img2_set_log_callback(cb_log_sink, NULL);
	ASSERT_EQUAL(IMG2_OK, img2_decode_gif_animated(b.d, b.n, NULL, &fr));
	img2_set_log_callback(NULL, NULL);
	ASSERT_EQUAL(200, fr.count);
	ASSERT_EQUAL(0, (int)s_big);
	img2_frames_free(&fr);
	cb_free(&b);
}
#endif

/* ===== [F14] TIFF ===== */

#ifdef IMG2_HAVE_TIFF
static void tiff_entry(cb_buf_t *b, unsigned tag, unsigned type, uint32_t count, uint32_t value)
{
	cb_le16(b, tag);
	cb_le16(b, type);
	cb_le32(b, count);
	if (type == 3) { /* SHORT, left-justified */
		cb_le16(b, value);
		cb_le16(b, 0);
	} else {
		cb_le32(b, value);
	}
}

/* Little-endian TIFF of `pages` 8-bit grey pages of w[p] x h[p] (one uncompressed strip each, no strip data:
 * the directories are valid, a read of the pixels would fail) */
static void tiff_pages(cb_buf_t *b, int pages, const uint32_t *w, const uint32_t *h)
{
	enum { N_ENTRIES = 9, IFD_SIZE = 2 + N_ENTRIES * 12 + 4 };
	cb_str(b, "II");
	cb_le16(b, 42);
	cb_le32(b, 8);
	for (int p = 0; p < pages; p++) {
		uint32_t at = 8u + (uint32_t)p * IFD_SIZE;
		cb_le16(b, N_ENTRIES);
		tiff_entry(b, 256, 4, 1, w[p]);        /* ImageWidth */
		tiff_entry(b, 257, 4, 1, h[p]);        /* ImageLength */
		tiff_entry(b, 258, 3, 1, 8);           /* BitsPerSample */
		tiff_entry(b, 259, 3, 1, 1);           /* Compression: none */
		tiff_entry(b, 262, 3, 1, 1);           /* Photometric: min-is-black */
		tiff_entry(b, 273, 4, 1, 8);           /* StripOffsets */
		tiff_entry(b, 277, 3, 1, 1);           /* SamplesPerPixel */
		tiff_entry(b, 278, 4, 1, h[p]);        /* RowsPerStrip */
		tiff_entry(b, 279, 4, 1, w[p] * h[p]); /* StripByteCounts */
		cb_le32(b, p + 1 < pages ? at + IFD_SIZE : 0);
	}
}

CTEST(f_residual_limits, tiff_pages_beyond_total_pixels)
{
	/* six 14000 x 14000 pages (1.96e8 each, within IMG2_MAX_PIXELS; 1.18e9 together > 2^30): the first page's
	 * 784 MB raster used to be allocated before any total was looked at */
	const uint32_t big[6] = { 14000, 14000, 14000, 14000, 14000, 14000 };
	cb_buf_t b = { 0 };
	tiff_pages(&b, 6, big, big);
	fr_expect_limit(img2_decode_tiff, b.d, b.n);
	cb_free(&b);

	/* a page over IMG2_MAX_PIXELS after a small one: rejected before the first page is decoded */
	const uint32_t mixed[2] = { 4, 15000 };
	tiff_pages(&b, 2, mixed, mixed);
	fr_expect_limit(img2_decode_tiff, b.d, b.n);
	cb_free(&b);
	fr_check();
}
#endif
