/**
 * @file test_cov_b_alloc.c
 * @brief Allocation failures in the GIF, PNG, WebP, TIFF, nanosvg and RAW decoders (coverage lane B)
 *
 * Linked with -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=strdup (alloc_fail.h, Linux only).
 * Every allocation of a decode is failed once: the decode must not crash, and must either succeed or fail
 * with *out zeroed. Fixtures are built before arming.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <img2/img2.h>
#include <jpeglib.h>

#include "../../libimg2/decoders/decoder.h"
#include "alloc_fail.h"
#include "test_cov_b_util.h"
#include "test_cov_b_dng.h"

#ifdef IMG2_HAVE_WEBP
#include <webp/encode.h>
#endif

#define AF_MAX_RUNS 20000

/* result of the last operation run; checked after the loop (ASSERT must not fire while armed) */
static int s_bad;

typedef img2_status_t (*dec_fn_t)(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out);

typedef struct {
	const uint8_t *data;
	size_t len;
	dec_fn_t fn;
} dec_ctx_t;

static void op_decode(void *ctx)
{
	dec_ctx_t *c = ctx;
	img2_frames_t out;
	img2_status_t st = c->fn(c->data, c->len, NULL, &out);
	if (st == IMG2_OK) {
		if (out.count < 1 || out.frames == NULL) {
			s_bad++;
		}
	} else if (out.frames != NULL || out.count != 0) {
		s_bad++;
	}
	img2_frames_free(&out);
}

/** Fail each allocation of the decode once, from allocation `from` on; returns the number of runs */
static long fail_each(dec_fn_t fn, const cb_buf_t *b, long from)
{
	dec_ctx_t c = { b->d, b->n, fn };
	long total = af_count_allocs(op_decode, &c);
	af_fail_range(op_decode, &c, from, total);
	return total;
}

static void quiet(void)
{
	img2_set_log_callback(cb_log_sink, NULL);
}

static void loud(void)
{
	img2_set_log_callback(NULL, NULL);
}

/* ===== GIF ===== */

#ifdef IMG2_HAVE_GIFLIB
CTEST(cov_b_alloc, gif)
{
	static const uint8_t pal[12] = { 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255 };
	cb_buf_t b = { 0 };
	gif_header(&b, 2, 2, 1, pal);
	const uint8_t loop[3] = { 1, 0, 0 };
	gif_app(&b, "NETSCAPE2.0", 11, loop, 3);
	gif_gce(&b, 3, 1, -1); /* restore to previous: the backup canvas */
	gif_fill_image(&b, 0, 0, 2, 2, 1);
	gif_gce(&b, 3, 1, 0);
	gif_fill_image(&b, 0, 0, 1, 1, 2);
	gif_gce(&b, 2, 1, -1);
	gif_fill_image(&b, 1, 1, 1, 1, 3);
	cb_u8(&b, 0x3B);
	quiet();
	ASSERT_TRUE(fail_each(img2_decode_gif_animated, &b, 0) > 10);
	loud();
	ASSERT_EQUAL(0, s_bad);
	cb_free(&b);
}
#endif

/* ===== PNG ===== */

CTEST(cov_b_alloc, png)
{
	static const uint8_t plte[12] = { 10, 10, 10, 255, 0, 0, 0, 255, 0, 0, 0, 255 };
	uint8_t raw[32];
	cb_buf_t b = { 0 };

	/* APNG: dispose PREVIOUS (backup canvas), BACKGROUND, blend OVER */
	cpng_start(&b, 2, 2, 8, 3, 0);
	cb_png_chunk(&b, "PLTE", plte, sizeof(plte));
	cpng_actl(&b, 3, 0);
	const uint8_t idx0[4] = { 1, 2, 3, 0 };
	cpng_fctl(&b, 0, 2, 2, 0, 0, 1, 0, 2, 0);
	cpng_data(&b, -1, raw, cpng_rows(raw, idx0, 2, 2));
	const uint8_t idx1[1] = { 3 };
	cpng_fctl(&b, 1, 1, 1, 1, 1, 3, 100, 2, 1);
	cpng_data(&b, 2, raw, cpng_rows(raw, idx1, 1, 1));
	cpng_fctl(&b, 3, 1, 1, 0, 0, 0, 0, 1, 1);
	cpng_data(&b, 4, raw, cpng_rows(raw, idx1, 1, 1));
	cpng_end(&b);
	quiet();
	ASSERT_TRUE(fail_each(img2_decode_png, &b, 0) > 10);
	loud();
	ASSERT_EQUAL(0, s_bad);
	cb_free(&b);

	/* still image with eXIf, tEXt and an iTXt XMP packet (each metadata reader allocates) */
	cpng_start(&b, 1, 1, 8, 2, 0);
	cpng_text(&b, "Artist", "Ann");
	const uint8_t exif[26] = { 'I', 'I', 42, 0, 8, 0, 0, 0, 1, 0, 0x12, 0x01, 3, 0, 1, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0 };
	cb_png_chunk(&b, "eXIf", exif, sizeof(exif));
	cpng_itxt(&b, "XML:com.adobe.xmp", "<r><d><dc:creator>Bob</dc:creator></d></r>");
	const uint8_t px[3] = { 7, 8, 9 };
	cpng_data(&b, -1, raw, cpng_rows(raw, px, 3, 1));
	cpng_end(&b);
	quiet();
	ASSERT_TRUE(fail_each(img2_decode_png, &b, 0) > 10);
	loud();
	ASSERT_EQUAL(0, s_bad);
	cb_free(&b);
}

/* ===== WebP ===== */

#ifdef IMG2_HAVE_WEBP
static void webp_chunk(cb_buf_t *b, const char fourcc[4], const void *payload, size_t n)
{
	cb_put(b, fourcc, 4);
	cb_le32(b, (uint32_t)n);
	cb_put(b, payload, n);
	if (n & 1) {
		cb_u8(b, 0);
	}
}

static void webp_container(cb_buf_t *b, int anim)
{
	static const uint8_t rgba[4] = { 0x20, 0x40, 0x80, 0xFF };
	uint8_t *still = NULL;
	size_t still_len = WebPEncodeLosslessRGBA(rgba, 1, 1, 4, &still);
	if (still_len < 20 || still == NULL) {
		abort();
	}
	cb_str(b, "RIFF");
	cb_le32(b, 0);
	cb_str(b, "WEBP");
	uint8_t vp8x[10] = { (uint8_t)(anim ? 0x12 : 0x1C), 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	webp_chunk(b, "VP8X", vp8x, sizeof(vp8x));
	if (anim) {
		const uint8_t a[6] = { 0, 0, 0, 0, 0, 0 };
		webp_chunk(b, "ANIM", a, sizeof(a));
		cb_buf_t f = { 0 };
		const uint8_t hdr[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20, 0, 0, 0 };
		cb_put(&f, hdr, sizeof(hdr));
		cb_put(&f, still + 12, still_len - 12);
		for (int i = 0; i < 3; i++) {
			webp_chunk(b, "ANMF", f.d, f.n);
		}
		cb_free(&f);
	} else {
		cb_put(b, still + 12, still_len - 12);
		const uint8_t exif[26] = { 'I', 'I', 42, 0, 8, 0, 0, 0, 1, 0, 0x12, 0x01, 3, 0, 1, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0 };
		webp_chunk(b, "EXIF", exif, sizeof(exif));
		static const char xmp[] = "<r><d><dc:creator>Bob</dc:creator></d></r>";
		webp_chunk(b, "XMP ", xmp, sizeof(xmp) - 1);
	}
	WebPFree(still);
	uint32_t riff = (uint32_t)(b->n - 8);
	memcpy(b->d + 4, &riff, 4); /* little-endian host */
}

CTEST(cov_b_alloc, webp)
{
	for (int anim = 0; anim < 2; anim++) {
		cb_buf_t b = { 0 };
		webp_container(&b, anim);
		quiet();
		ASSERT_TRUE(fail_each(img2_decode_webp, &b, 0) > 3);
		loud();
		ASSERT_EQUAL(0, s_bad);
		cb_free(&b);
	}
}
#endif

/* ===== nanosvg ===== */

CTEST(cov_b_alloc, svg)
{
	/* nanosvg.h itself does not survive a failed allocation while it builds paths (nsvg__addPoint reallocs
	 * without a NULL check): the SVGs failed at every allocation have no shapes; with CSS and without */
	static const char *svgs[2] = {
		"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"4\" height=\"2\"><style>.r {fill:#ff0000;}</style><g class=\"r\"/></svg>",
		"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"4\" height=\"2\"><g/></svg>",
	};
	cb_buf_t b = { 0 };
	for (int i = 0; i < 2; i++) {
		cb_str(&b, svgs[i]);
		quiet();
		ASSERT_TRUE(fail_each(img2_decode_svg_nanosvg, &b, 0) > 5);
		loud();
		ASSERT_EQUAL(0, s_bad);
		cb_free(&b);
	}

	/* the three growth paths of the class inliner (realloc of the output buffer): only the allocations
	 * before nanosvg's path building fail (copy, css, output, realloc, parser, image) */
	const int modes[3] = { SVG_GROW_COPY, SVG_GROW_REST, SVG_GROW_STYLE };
	for (int i = 0; i < 3; i++) {
		svg_growth(&b, modes[i]);
		dec_ctx_t c = { b.d, b.n, img2_decode_svg_nanosvg };
		quiet();
		af_fail_range(op_decode, &c, 0, 6);
		loud();
		ASSERT_EQUAL(0, s_bad);
		cb_free(&b);
	}
}

/* ===== RAW ===== */

#ifdef IMG2_HAVE_RAW
CTEST(cov_b_alloc, raw)
{
	/* From the second allocation on: the first one is LibRaw's memory manager table, which LibRaw's
	 * constructor memsets without a NULL check (vendor crash, see the lane notes). No JPEG preview: the
	 * preview is decoded with jpeg-quantsmooth, which crashes after a failed allocation (upsample_row); the
	 * bitmap preview takes the same exit (metadata, frames array). */
	const dng_opts_t variants[2] = {
		{ .preview = PREV_NONE, .full_meta = 1, .xmp = "<r><d><dc:creator>X</dc:creator></d></r>" },
		{ .preview = PREV_RGB, .xmp = "<r><d><dc:creator>X</dc:creator></d></r>" },
	};
	for (int i = 0; i < 2; i++) {
		cb_buf_t b = { 0 };
		dng_build(&b, &variants[i]);
		quiet();
		ASSERT_TRUE(fail_each(img2_decode_raw, &b, 1) > 5);
		loud();
		ASSERT_EQUAL(0, s_bad);
		cb_free(&b);
	}
}
#endif

/* ===== TIFF ===== */

#ifdef IMG2_HAVE_TIFF
/* Classic TIFF, RGB 8-bit, no ExtraSamples (see docs/coverage.md: libtiff dereferences a NULL ExtraSamples
 * array after an allocation failure), one strip per page, `pages` 2x2 pages */
static void tiff_rgb(cb_buf_t *b, int pages)
{
	cb_str(b, "II");
	cb_le16(b, 42);
	cb_le32(b, 8);
	for (int p = 0; p < pages; p++) {
		uint32_t ifd = (uint32_t)b->n;
		uint32_t data = ifd + 2 + 10 * 12 + 4;
		uint32_t bps = data + 12;
		uint32_t next = p + 1 < pages ? bps + 6 + 2 : 0;
		cb_le16(b, 10);
		const uint32_t e[10][4] = {
			{ 256, 3, 1, 2 }, { 257, 3, 1, 2 }, { 258, 3, 3, bps }, { 259, 3, 1, 1 }, { 262, 3, 1, 2 },
			{ 273, 4, 1, data }, { 277, 3, 1, 3 }, { 278, 3, 1, 2 }, { 279, 4, 1, 12 }, { 284, 3, 1, 1 },
		};
		for (int i = 0; i < 10; i++) {
			cb_le16(b, e[i][0]);
			cb_le16(b, e[i][1]);
			cb_le32(b, e[i][2]);
			cb_le32(b, e[i][3]);
		}
		cb_le32(b, next);
		for (int i = 0; i < 12; i++) {
			cb_u8(b, (unsigned)(p * 40 + i));
		}
		cb_le16(b, 8);
		cb_le16(b, 8);
		cb_le16(b, 8);
		cb_le16(b, 0); /* pad to the next IFD */
	}
}

CTEST(cov_b_alloc, tiff)
{
	for (int pages = 1; pages <= 2; pages++) {
		cb_buf_t b = { 0 };
		tiff_rgb(&b, pages);
		img2_frames_t fr;
		ASSERT_EQUAL(IMG2_OK, img2_decode_tiff(b.d, b.n, NULL, &fr));
		ASSERT_EQUAL(pages, fr.count);
		img2_frames_free(&fr);
		quiet();
		ASSERT_TRUE(fail_each(img2_decode_tiff, &b, 0) > 10);
		loud();
		ASSERT_EQUAL(0, s_bad);
		cb_free(&b);
	}
}
#endif
