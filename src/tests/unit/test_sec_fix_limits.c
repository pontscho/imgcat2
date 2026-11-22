/**
 * @file test_sec_fix_limits.c
 * @brief [P33] (CWE-770) Header dimensions over IMG2_MAX_DIMENSION / IMG2_MAX_PIXELS are rejected before the codec
 *        allocates header-sized buffers
 *
 * Every decoder checked the decoded size with img2_image_calc_size() only after the codec had decoded the image
 * (or at least allocated for it): a tiny file announcing a huge image made libjpeg (buffered / progressive
 * mode: whole-image coefficient arrays), qoi.h, stb_image, libwebp (WebPDecodeRGBA, WebPAnimDecoderNew),
 * libheif and LibRaw allocate up to gigabytes first. Each decoder now reads the dimensions from the header
 * first and returns IMG2_E_LIMIT, as the late check did.
 *
 * Proof that no large allocation happens: the executable is linked with
 * -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc (the img2 archive and its vendored codecs are static, so their
 * calls are wrapped too); the wrappers fail and count every request above 256 MiB. Each crafted file must
 * return IMG2_E_LIMIT with that count at zero. (libheif allocates through operator new in the shared libstdc++:
 * not wrapped, the HEIF/AVIF cases check the status only.)
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jpeglib.h>

#include "../../libimg2/core/image.h"
#include "../../libimg2/decoders/decoder.h"
#include "test_cov_b_util.h"

#ifdef IMG2_HAVE_RAW
#include "test_cov_b_dng.h"
#endif
#ifdef IMG2_HAVE_WEBP
#include <webp/encode.h>
#endif
#ifdef IMG2_HAVE_HEIF
#include "cov_c_heif_util.h"
#endif

/* ---- allocation wrappers: fail and count every request above SL_BIG ---- */

#define SL_BIG ((size_t)256 << 20)

void *__real_malloc(size_t size);
void *__real_calloc(size_t nmemb, size_t size);
void *__real_realloc(void *ptr, size_t size);
void *__wrap_malloc(size_t size);
void *__wrap_calloc(size_t nmemb, size_t size);
void *__wrap_realloc(void *ptr, size_t size);

static long s_big; /* requests above SL_BIG since the last sl_reset() */

void *__wrap_malloc(size_t size)
{
	if (size > SL_BIG) {
		s_big++;
		return NULL;
	}
	return __real_malloc(size);
}

void *__wrap_calloc(size_t nmemb, size_t size)
{
	if (nmemb != 0 && size > SL_BIG / nmemb) {
		s_big++;
		return NULL;
	}
	return __real_calloc(nmemb, size);
}

void *__wrap_realloc(void *ptr, size_t size)
{
	if (size > SL_BIG) {
		s_big++;
		return NULL;
	}
	return __real_realloc(ptr, size);
}

typedef img2_status_t (*sl_fn_t)(const uint8_t *, size_t, const img2_decode_opts_t *, img2_frames_t *);

static int s_bad; /* failed cases of the running test (sl_check() asserts and resets) */

/* fn(data) must return IMG2_E_LIMIT without any allocation above SL_BIG; a miss is reported with its line and
 * counted, so one run lists every failing case of a test */
static void sl_limit_at(int line, sl_fn_t fn, const uint8_t *data, size_t len, const img2_decode_opts_t *opts)
{
	img2_frames_t fr = { 0 };
	s_big = 0;
	img2_set_log_callback(cb_log_sink, NULL);
	img2_status_t st = fn(data, len, opts, &fr);
	img2_set_log_callback(NULL, NULL);
	if (s_big != 0 || st != IMG2_E_LIMIT || fr.frames != NULL) {
		fprintf(stderr, "  line %d: status %d (%s), %ld allocation(s) above 256 MiB\n", line, (int)st, img2_strerror(st), s_big);
		s_bad++;
	}
	img2_frames_free(&fr);
}

#define sl_expect_limit(fn, data, len, opts) sl_limit_at(__LINE__, fn, data, len, opts)

static void sl_check(void)
{
	int bad = s_bad;
	s_bad = 0;
	ASSERT_EQUAL(0, bad);
}

/* ===== JPEG: SOF 65500 x 65500, truncated after SOS (jpeg_mem_src supplies the EOI) ===== */

static void jpeg_huge(cb_buf_t *b, unsigned sof)
{
	cb_be16(b, 0xFFD8);
	cb_be16(b, 0xFFDB); /* DQT: table 0, all 1 */
	cb_be16(b, 67);
	cb_u8(b, 0);
	for (int i = 0; i < 64; i++) {
		cb_u8(b, 1);
	}
	cb_be16(b, 0xFF00 | sof); /* SOF: 8 bit, 65500 x 65500, one component */
	cb_be16(b, 11);
	cb_u8(b, 8);
	cb_be16(b, 65500);
	cb_be16(b, 65500);
	cb_u8(b, 1);
	cb_u8(b, 1);
	cb_u8(b, 0x11);
	cb_u8(b, 0);
	for (unsigned cls = 0; cls < 2; cls++) { /* DHT: DC 0 and AC 0, one code of length 1 (symbol 0) */
		cb_be16(b, 0xFFC4);
		cb_be16(b, 20);
		cb_u8(b, cls << 4);
		cb_u8(b, 1);
		for (int i = 1; i < 16; i++) {
			cb_u8(b, 0);
		}
		cb_u8(b, 0);
	}
	cb_be16(b, 0xFFDA); /* SOS: baseline full spectrum, progressive DC first scan */
	cb_be16(b, 8);
	cb_u8(b, 1);
	cb_u8(b, 1);
	cb_u8(b, 0);
	cb_u8(b, 0);
	cb_u8(b, sof == 0xC2 ? 0 : 63);
	cb_u8(b, 0);
	for (int i = 0; i < 16; i++) {
		cb_u8(b, 0);
	}
}

CTEST(sec_fix_limits, jpeg_sof_beyond_max_pixels)
{
	cb_buf_t base = { 0 };
	cb_buf_t prog = { 0 };
	jpeg_huge(&base, 0xC0);
	jpeg_huge(&prog, 0xC2);
	img2_decode_opts_t o = IMG2_DECODE_OPTS_INIT;
	o.jpeg_deblock = -1; /* default: jpeg-quantsmooth if built (buffered image: full coefficient arrays) */
	sl_expect_limit(img2_decode_jpeg, base.d, base.n, &o);
	sl_expect_limit(img2_decode_jpeg, prog.d, prog.n, &o);
	o.jpeg_deblock = 0; /* plain libjpeg: a progressive file is buffered as well */
	sl_expect_limit(img2_decode_jpeg, base.d, base.n, &o);
	sl_expect_limit(img2_decode_jpeg, prog.d, prog.n, &o);
	cb_free(&base);
	cb_free(&prog);
	sl_check();
}

/* ===== QOI: 22-byte file, 20000 x 19999 (qoi.h's own cap is 400e6 pixels) ===== */

CTEST(sec_fix_limits, qoi_header_beyond_max_pixels)
{
	cb_buf_t b = { 0 };
	cb_str(&b, "qoif");
	cb_be32(&b, 20000);
	cb_be32(&b, 19999);
	cb_u8(&b, 4);
	cb_u8(&b, 0);
	cb_put(&b, "\0\0\0\0\0\0\0\1", 8);
	ASSERT_EQUAL(22, (int)b.n);
	sl_expect_limit(img2_decode_qoi, b.d, b.n, NULL);

	/* 1 x 65537: only the height is over IMG2_MAX_DIMENSION */
	b.d[7] = 1;
	b.d[4] = b.d[5] = b.d[6] = 0;
	b.d[8] = 0;
	b.d[9] = 1;
	b.d[10] = 0;
	b.d[11] = 1;
	sl_expect_limit(img2_decode_qoi, b.d, b.n, NULL);

	/* no QOI header (too short / other magic): no pre-check, qoi_decode() reports the file as before */
	img2_frames_t fr = { 0 };
	img2_set_log_callback(cb_log_sink, NULL);
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_decode_qoi(b.d, 13, NULL, &fr));
	b.d[0] = 'x';
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_decode_qoi(b.d, b.n, NULL, &fr));
	img2_set_log_callback(NULL, NULL);
	cb_free(&b);
	sl_check();
}

/* ===== img2_decode_add_frame_pixels(): one frame alone over the total (every caller pre-checks the frame size
 * against IMG2_MAX_PIXELS now, so only a direct call reaches it) ===== */

CTEST(sec_fix_limits, add_frame_pixels_single_frame_over_total)
{
	uint64_t total = 0;
	img2_set_log_callback(cb_log_sink, NULL);
	ASSERT_EQUAL(IMG2_E_LIMIT, img2_decode_add_frame_pixels(&total, 65536, 65536));
	img2_set_log_callback(NULL, NULL);
	ASSERT_EQUAL(0, (int)total);
	ASSERT_EQUAL(IMG2_OK, img2_decode_add_frame_pixels(&total, 2, 3));
	ASSERT_EQUAL(6, (int)total);
}

/* ===== stb_image: BMP, TGA (RLE), PSD (RLE) 20000 x 20000; HDR 16000 x 16000 ===== */

CTEST(sec_fix_limits, stb_headers_beyond_max_pixels)
{
	cb_buf_t b = { 0 };

	/* BMP: BITMAPINFOHEADER, 24 bit, no pixel data */
	cb_str(&b, "BM");
	cb_le32(&b, 54);
	cb_le32(&b, 0);
	cb_le32(&b, 54);
	cb_le32(&b, 40);
	cb_le32(&b, 20000);
	cb_le32(&b, 20000);
	cb_le16(&b, 1);
	cb_le16(&b, 24);
	for (int i = 0; i < 6; i++) {
		cb_le32(&b, 0);
	}
	sl_expect_limit(img2_decode_stb, b.d, b.n, NULL);
	cb_free(&b);

	/* TGA: type 10 (RLE true colour), 32 bit, one RLE packet */
	cb_u8(&b, 0);
	cb_u8(&b, 0);
	cb_u8(&b, 10);
	cb_put(&b, "\0\0\0\0\0", 5);
	cb_le16(&b, 0);
	cb_le16(&b, 0);
	cb_le16(&b, 20000);
	cb_le16(&b, 20000);
	cb_u8(&b, 32);
	cb_u8(&b, 0x28);
	cb_put(&b, "\xFF\x10\x20\x30\x40", 5);
	sl_expect_limit(img2_decode_stb, b.d, b.n, NULL);
	cb_free(&b);

	/* PSD: RGB, 8 bit, RLE compressed, no data */
	cb_str(&b, "8BPS");
	cb_be16(&b, 1);
	cb_put(&b, "\0\0\0\0\0\0", 6);
	cb_be16(&b, 3);
	cb_be32(&b, 20000);
	cb_be32(&b, 20000);
	cb_be16(&b, 8);
	cb_be16(&b, 3);
	cb_be32(&b, 0);
	cb_be32(&b, 0);
	cb_be32(&b, 0);
	cb_be16(&b, 1);
	sl_expect_limit(img2_decode_stb, b.d, b.n, NULL);
	cb_free(&b);

	/* HDR: 16000 x 16000 = 2.56e8 pixels (stb_image fails it as "too large" itself: CODEC before) */
	cb_str(&b, "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 16000 +X 16000\n");
	cb_put(&b, "\x02\x02\x3E\x80", 4);
	sl_expect_limit(img2_decode_stb, b.d, b.n, NULL);
	cb_free(&b);
	sl_check();
}

#ifdef IMG2_HAVE_WEBP
/* 16 x 16 lossy WebP from the vendored encoder (WebPFree() the result) */
static uint8_t *webp_small(size_t *len)
{
	uint8_t rgba[16 * 16 * 4];
	memset(rgba, 0x80, sizeof(rgba));
	for (size_t i = 3; i < sizeof(rgba); i += 4) {
		rgba[i] = 0xFF; /* opaque: a simple "VP8 " file, no VP8X / ALPH */
	}
	uint8_t *out = NULL;
	*len = WebPEncodeRGBA(rgba, 16, 16, 16 * 4, 50.0f, &out);
	return *len > 0 ? out : NULL;
}

static void riff_chunk_hdr(cb_buf_t *b, const char *fourcc, uint32_t size)
{
	cb_str(b, fourcc);
	cb_le32(b, size);
}

static void le24(cb_buf_t *b, uint32_t v)
{
	cb_u8(b, v & 0xFF);
	cb_u8(b, (v >> 8) & 0xFF);
	cb_u8(b, (v >> 16) & 0xFF);
}

CTEST(sec_fix_limits, webp_canvas_beyond_max_pixels)
{
	size_t sl = 0;
	uint8_t *small = webp_small(&sl);
	ASSERT_NOT_NULL(small);
	ASSERT_TRUE(sl > 30 && memcmp(small + 12, "VP8 ", 4) == 0);

	/* still: the VP8 key frame header patched to 16383 x 16383 (WebPDecodeRGBA allocated 1 GiB) */
	uint8_t *still = malloc(sl);
	ASSERT_NOT_NULL(still);
	memcpy(still, small, sl);
	/* chunk payload at 20: 3-byte frame tag, start code 9d 01 2a, then 14-bit width / height + 2-bit scale */
	ASSERT_TRUE(still[23] == 0x9d && still[24] == 0x01 && still[25] == 0x2a);
	still[26] = 0xFF;
	still[27] = 0x3F;
	still[28] = 0xFF;
	still[29] = 0x3F;
	sl_expect_limit(img2_decode_webp, still, sl, NULL);
	free(still);

	/* animated: VP8X canvas 16383 x 16383, one 16 x 16 ANMF frame (WebPAnimDecoderNew calloc'ed two canvases) */
	const uint8_t *vp8 = small + 12; /* "VP8 " chunk with its header */
	const size_t vp8_len = sl - 12;
	cb_buf_t b = { 0 };
	cb_str(&b, "RIFF");
	cb_le32(&b, 0); /* patched */
	cb_str(&b, "WEBP");
	riff_chunk_hdr(&b, "VP8X", 10);
	cb_u8(&b, 0x02); /* animation */
	le24(&b, 0);
	le24(&b, 16383 - 1);
	le24(&b, 16383 - 1);
	riff_chunk_hdr(&b, "ANIM", 6);
	cb_le32(&b, 0);
	cb_le16(&b, 0);
	riff_chunk_hdr(&b, "ANMF", (uint32_t)(16 + vp8_len));
	le24(&b, 0);
	le24(&b, 0);
	le24(&b, 16 - 1);
	le24(&b, 16 - 1);
	le24(&b, 100);
	cb_u8(&b, 0);
	cb_put(&b, vp8, vp8_len);
	uint32_t riff = (uint32_t)(b.n - 8);
	b.d[4] = (uint8_t)riff;
	b.d[5] = (uint8_t)(riff >> 8);
	b.d[6] = (uint8_t)(riff >> 16);
	b.d[7] = (uint8_t)(riff >> 24);
	ASSERT_TRUE(img2_webp_is_animated(b.d, b.n));
	sl_expect_limit(img2_decode_webp, b.d, b.n, NULL);
	cb_free(&b);
	WebPFree(small);
	sl_check();
}
#endif

#ifdef IMG2_HAVE_HEIF
static long sl_find4(const uint8_t *d, size_t len, const char *cc, size_t from)
{
	for (size_t i = from; i + 4 <= len; i++) {
		if (memcmp(d + i, cc, 4) == 0) {
			return (long)i;
		}
	}
	return -1;
}

static void sl_be32_at(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

/* every 'ispe' box: side x side */
static int sl_patch_ispe(uint8_t *d, size_t len, uint32_t side)
{
	int n = 0;
	for (long p = sl_find4(d, len, "ispe", 0); p >= 0; p = sl_find4(d, len, "ispe", (size_t)p + 4)) {
		sl_be32_at(d + p + 8, side);
		sl_be32_at(d + p + 12, side);
		n++;
	}
	return n;
}

CTEST(sec_fix_limits, heif_ispe_beyond_max_pixels)
{
	heif_init(NULL);
	for (int count = 1; count <= 2; count++) { /* primary image, top-level image list */
		size_t len = 0;
		uint8_t *d = hb_list(heif_compression_uncompressed, count, 16, 16, NULL, 0, &len);
		ASSERT_NOT_NULL(d);
		/* 20000 x 20000 = 4e8 pixels: above IMG2_MAX_PIXELS, a list of two stays below 2^30 */
		ASSERT_TRUE(sl_patch_ispe(d, len, 20000) > 0);
		ASSERT_EQUAL(count == 1 ? IMG2_HEIF_LAYOUT_SINGLE : IMG2_HEIF_LAYOUT_LIST, img2_heif_layout(d, len, NULL));
		sl_expect_limit(img2_decode_heif, d, len, NULL);
		sl_expect_limit(img2_decode_avif, d, len, NULL);
		free(d);
	}

	/* six images of 14000 x 14000 (1.96e8 each, within IMG2_MAX_PIXELS): the list total (1.18e9 > 2^30) */
	size_t len = 0;
	uint8_t *d = hb_list(heif_compression_uncompressed, 6, 16, 16, NULL, 0, &len);
	ASSERT_NOT_NULL(d);
	ASSERT_TRUE(sl_patch_ispe(d, len, 14000) > 0);
	sl_expect_limit(img2_decode_heif, d, len, NULL);
	sl_expect_limit(img2_decode_avif, d, len, NULL);
	free(d);
	heif_deinit();
	sl_check();
}
#endif

#ifdef IMG2_HAVE_RAW
/* IFD0 of a dng_build() file: ImageWidth / ImageLength (LONG, inline) set to w x h */
static int sl_patch_dng_size(cb_buf_t *b, uint32_t w, uint32_t h)
{
	uint32_t ifd = (uint32_t)b->d[4] | (uint32_t)b->d[5] << 8 | (uint32_t)b->d[6] << 16 | (uint32_t)b->d[7] << 24;
	unsigned n = (unsigned)b->d[ifd] | (unsigned)b->d[ifd + 1] << 8;
	int patched = 0;
	for (unsigned i = 0; i < n; i++) {
		uint8_t *e = b->d + ifd + 2 + 12 * i;
		unsigned tag = (unsigned)e[0] | (unsigned)e[1] << 8;
		uint32_t v = tag == 256 ? w : tag == 257 ? h : 0;
		if (v != 0) {
			e[8] = (uint8_t)v;
			e[9] = (uint8_t)(v >> 8);
			e[10] = (uint8_t)(v >> 16);
			e[11] = (uint8_t)(v >> 24);
			patched++;
		}
	}
	return patched;
}

CTEST(sec_fix_limits, raw_size_beyond_max_pixels)
{
	/* 20000 x 15000 16-bit CFA without a preview (LibRaw's raw buffer alone is 600 MB) */
	cb_buf_t b = { 0 };
	dng_opts_t o = { .preview = PREV_NONE };
	dng_build(&b, &o);
	ASSERT_EQUAL(2, sl_patch_dng_size(&b, 20000, 15000));
	sl_expect_limit(img2_decode_raw, b.d, b.n, NULL);
	cb_free(&b);
	sl_check();
}
#endif
