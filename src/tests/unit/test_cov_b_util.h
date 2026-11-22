/**
 * @file test_cov_b_util.h
 * @brief Shared helpers of the coverage lane B tests (test_cov_b_*.c): byte buffers, chunked-format writers,
 *        a heap-usage probe for leak checks
 *
 * Header-only (static functions); fixtures are generated in-test.
 */

#ifndef IMG2_TESTS_COV_B_UTIL_H
#define IMG2_TESTS_COV_B_UTIL_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include <img2/img2.h>

#include "../ctest.h"

/** Growable byte buffer */
typedef struct {
	uint8_t *d;
	size_t n;
	size_t cap;
} cb_buf_t;

static inline void cb_put(cb_buf_t *b, const void *src, size_t n)
{
	if (b->n + n > b->cap) {
		size_t cap = b->cap ? b->cap : 256;
		while (cap < b->n + n) {
			cap *= 2;
		}
		uint8_t *p = (uint8_t *)realloc(b->d, cap);
		if (p == NULL) {
			fprintf(stderr, "cb_put: out of memory\n");
			abort();
		}
		b->d = p;
		b->cap = cap;
	}
	if (n > 0) {
		memcpy(b->d + b->n, src, n);
	}
	b->n += n;
}

static inline void cb_u8(cb_buf_t *b, unsigned v)
{
	uint8_t c = (uint8_t)v;
	cb_put(b, &c, 1);
}

static inline void cb_le16(cb_buf_t *b, unsigned v)
{
	cb_u8(b, v & 0xFF);
	cb_u8(b, (v >> 8) & 0xFF);
}

static inline void cb_le32(cb_buf_t *b, uint32_t v)
{
	cb_le16(b, v & 0xFFFF);
	cb_le16(b, v >> 16);
}

static inline void cb_be16(cb_buf_t *b, unsigned v)
{
	cb_u8(b, (v >> 8) & 0xFF);
	cb_u8(b, v & 0xFF);
}

static inline void cb_be32(cb_buf_t *b, uint32_t v)
{
	cb_be16(b, v >> 16);
	cb_be16(b, v & 0xFFFF);
}

static inline void cb_str(cb_buf_t *b, const char *s)
{
	cb_put(b, s, strlen(s));
}

static inline void cb_free(cb_buf_t *b)
{
	free(b->d);
	memset(b, 0, sizeof(*b));
}

/** CRC-32 (PNG / zlib polynomial) */
static inline uint32_t cb_crc32(uint32_t crc, const uint8_t *p, size_t n)
{
	crc = ~crc;
	for (size_t i = 0; i < n; i++) {
		crc ^= p[i];
		for (int k = 0; k < 8; k++) {
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
		}
	}
	return ~crc;
}

/** zlib stream of stored (uncompressed) deflate blocks */
static inline void cb_zlib_stored(cb_buf_t *out, const uint8_t *p, size_t n)
{
	cb_u8(out, 0x78);
	cb_u8(out, 0x01);
	size_t off = 0;
	do {
		size_t len = n - off > 65535 ? 65535 : n - off;
		cb_u8(out, off + len == n ? 1 : 0);
		cb_le16(out, (unsigned)len);
		cb_le16(out, (unsigned)(~len & 0xFFFF));
		cb_put(out, p + off, len);
		off += len;
	} while (off < n);
	uint32_t a = 1, s = 0;
	for (size_t i = 0; i < n; i++) {
		a = (a + p[i]) % 65521u;
		s = (s + a) % 65521u;
	}
	cb_be32(out, (s << 16) | a);
}

/** PNG chunk: length, type, data, CRC */
static inline void cb_png_chunk(cb_buf_t *b, const char type[4], const uint8_t *data, size_t n)
{
	cb_be32(b, (uint32_t)n);
	size_t at = b->n;
	cb_put(b, type, 4);
	cb_put(b, data, n);
	cb_be32(b, cb_crc32(0, b->d + at, n + 4));
}

/* ---- PNG / APNG writer (stored deflate blocks, filter type 0) ---- */

static inline void cpng_start(cb_buf_t *b, uint32_t w, uint32_t h, unsigned depth, unsigned ctype, unsigned interlace)
{
	static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	cb_put(b, sig, sizeof(sig));
	cb_buf_t c = { 0 };
	cb_be32(&c, w);
	cb_be32(&c, h);
	cb_u8(&c, depth);
	cb_u8(&c, ctype);
	cb_u8(&c, 0);
	cb_u8(&c, 0);
	cb_u8(&c, interlace);
	cb_png_chunk(b, "IHDR", c.d, c.n);
	cb_free(&c);
}

/** IDAT (seq < 0) or fdAT (sequence number seq) holding the zlib stream of raw (filtered) scanlines */
static inline void cpng_data(cb_buf_t *b, long seq, const uint8_t *raw, size_t n)
{
	cb_buf_t c = { 0 };
	if (seq >= 0) {
		cb_be32(&c, (uint32_t)seq);
	}
	cb_zlib_stored(&c, raw, n);
	cb_png_chunk(b, seq >= 0 ? "fdAT" : "IDAT", c.d, c.n);
	cb_free(&c);
}

static inline void cpng_actl(cb_buf_t *b, uint32_t frames, uint32_t plays)
{
	cb_buf_t c = { 0 };
	cb_be32(&c, frames);
	cb_be32(&c, plays);
	cb_png_chunk(b, "acTL", c.d, c.n);
	cb_free(&c);
}

static inline void cpng_fctl(cb_buf_t *b, uint32_t seq, uint32_t w, uint32_t h, uint32_t x, uint32_t y, unsigned delay_num, unsigned delay_den, unsigned dispose, unsigned blend)
{
	cb_buf_t c = { 0 };
	cb_be32(&c, seq);
	cb_be32(&c, w);
	cb_be32(&c, h);
	cb_be32(&c, x);
	cb_be32(&c, y);
	cb_be16(&c, delay_num);
	cb_be16(&c, delay_den);
	cb_u8(&c, dispose);
	cb_u8(&c, blend);
	cb_png_chunk(b, "fcTL", c.d, c.n);
	cb_free(&c);
}

/** tEXt chunk */
static inline void cpng_text(cb_buf_t *b, const char *key, const char *text)
{
	cb_buf_t c = { 0 };
	cb_put(&c, key, strlen(key) + 1);
	cb_str(&c, text);
	cb_png_chunk(b, "tEXt", c.d, c.n);
	cb_free(&c);
}

/** Uncompressed iTXt chunk (no language tag, no translated keyword) */
static inline void cpng_itxt(cb_buf_t *b, const char *key, const char *text)
{
	cb_buf_t c = { 0 };
	cb_put(&c, key, strlen(key) + 1);
	cb_u8(&c, 0); /* compression flag */
	cb_u8(&c, 0); /* compression method */
	cb_u8(&c, 0); /* language tag "" */
	cb_u8(&c, 0); /* translated keyword "" */
	cb_str(&c, text);
	cb_png_chunk(b, "iTXt", c.d, c.n);
	cb_free(&c);
}

static inline void cpng_end(cb_buf_t *b)
{
	cb_png_chunk(b, "IEND", NULL, 0);
}

/** Filtered scanlines (filter 0) of h rows of row_bytes each, row y filled from pixels + y * row_bytes */
static inline size_t cpng_rows(uint8_t *dst, const uint8_t *pixels, size_t row_bytes, uint32_t h)
{
	size_t n = 0;
	for (uint32_t y = 0; y < h; y++) {
		dst[n++] = 0;
		memcpy(dst + n, pixels + (size_t)y * row_bytes, row_bytes);
		n += row_bytes;
	}
	return n;
}

/* ---- SVG fixtures for decoder_nanosvg.c's CSS class inliner (inline_svg_styles) ----
 *
 * The inliner allocates 3 * strlen(svg) bytes, copies the text between class attributes and replaces each
 * class="a" (rule ".a {R}") with style="R" (R + 9 bytes for a 9-byte attribute). With K such elements the
 * output outgrows the input; svg_growth() sizes a run of T spaces so that a specific copy step is the one
 * that crosses the allocation:
 *   SVG_GROW_COPY:   the text before the next class attribute (needs a realloc)
 *   SVG_GROW_REST:   the text after the last class attribute (needs a realloc)
 *   SVG_GROW_EXACT:  the text before a final class="zz" (no rule) that ends the data fills the buffer exactly
 *   SVG_GROW_STYLE:  no spaces: an inserted style attribute is the one that needs the realloc
 */
enum { SVG_GROW_COPY, SVG_GROW_REST, SVG_GROW_EXACT, SVG_GROW_STYLE };

static inline void svg_growth(cb_buf_t *b, int mode)
{
	enum { R = 100, K = 60 };
	char rule[R + 1];
	memset(rule, 'x', R);
	memcpy(rule, "fill:#00ff00;", 13); /* the rest: unknown property characters nanosvg ignores */
	rule[R] = '\0';
	static const char pre[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"2\" height=\"2\">";
	static const char head[] = "<rect width=\"2\" height=\"2\" "; /* copied text before each class attribute */
	static const char elem[] = "<rect width=\"2\" height=\"2\" class=\"a\"/>";
	static const char tail_elem[] = "<rect width=\"2\" height=\"2\" class=\"a\"/></svg>";
	static const char tail_rest[] = "</svg>";
	static const char tail_exact[] = "<rect width=\"2\" height=\"2\" class=\"zz\"";
	const char *tail = mode == SVG_GROW_COPY ? tail_elem : (mode == SVG_GROW_EXACT ? tail_exact : tail_rest);
	const size_t H = strlen(head);

	for (size_t pad = 0; pad < 64; pad++) {
		/* spaces inside <style> (not copied to the output) shift 3 * C against X */
		size_t P = strlen(pre);
		/* output before the critical copy: pre, head + style, then K - 1 times "/>" + head + style */
		size_t X = P + H + (size_t)(K - 1) * (H + 2) + (size_t)K * (R + 9);
		/* input without the T spaces: pre, <style>.a {R}</style>, K elements, tail */
		size_t C = P + 7 + pad + 4 + R + 1 + 8 + (size_t)K * strlen(elem) + strlen(tail);
		/* the critical copy is "/>" + T spaces + head (COPY, EXACT) or "/>" + T + "</svg>" + NUL (REST) */
		size_t extra = mode == SVG_GROW_REST ? 9 : H + 2;
		size_t T;
		if (mode == SVG_GROW_STYLE) {
			/* replay the inliner's checks: the first one to fail must be a style insertion's */
			T = 0;
			size_t out_pos = P;
			int style_first = 0;
			for (int k = 0; k < K; k++) {
				size_t copy = k == 0 ? H : H + 2;
				if (out_pos + copy >= 3 * C) {
					break;
				}
				out_pos += copy;
				if (out_pos + 10 + R > 3 * C) {
					style_first = 1;
					break;
				}
				out_pos += R + 9;
			}
			if (!style_first) {
				continue;
			}
		} else if (mode == SVG_GROW_EXACT) {
			/* X + T + extra == 3 * (C + T) */
			if ((X + extra - 3 * C) % 2 != 0) {
				continue;
			}
			T = (X + extra - 3 * C) / 2;
		} else {
			/* X + 1 <= 3 * (C + T) < X + T + extra: no realloc for the styles, one for the copy */
			T = (X + 1 + 2 - 3 * C) / 3;
			if (X + 1 > 3 * (C + T) || 3 * (C + T) >= X + T + extra) {
				abort();
			}
		}
		cb_str(b, pre);
		cb_str(b, "<style>");
		for (size_t s = 0; s < pad; s++) {
			cb_u8(b, ' ');
		}
		cb_str(b, ".a {");
		cb_str(b, rule);
		cb_str(b, "}</style>");
		for (int k = 0; k < K; k++) {
			cb_str(b, elem);
		}
		for (size_t t = 0; t < T; t++) {
			cb_u8(b, ' ');
		}
		cb_str(b, tail);
		if (b->n != C + T) {
			abort();
		}
		return;
	}
	abort();
}

/* ---- GIF writer ---- */

/** Header + logical screen descriptor; gct_bits < 0: no global colour table, else 2^(gct_bits+1) entries */
static inline void gif_header(cb_buf_t *b, unsigned w, unsigned h, int gct_bits, const uint8_t *pal)
{
	cb_str(b, "GIF89a");
	cb_le16(b, w);
	cb_le16(b, h);
	cb_u8(b, gct_bits < 0 ? 0x70 : (0xF0 | (unsigned)gct_bits));
	cb_u8(b, 0); /* background */
	cb_u8(b, 0); /* aspect */
	if (gct_bits >= 0) {
		cb_put(b, pal, 3u << (gct_bits + 1));
	}
}

/** Graphics control extension; transparent < 0: none */
static inline void gif_gce(cb_buf_t *b, unsigned disposal, unsigned delay, int transparent)
{
	const uint8_t gce[] = { 0x21, 0xF9, 4, (uint8_t)((disposal << 2) | (transparent >= 0 ? 1 : 0)), (uint8_t)(delay & 0xFF), (uint8_t)(delay >> 8), (uint8_t)(transparent >= 0 ? transparent : 0), 0 };
	cb_put(b, gce, sizeof(gce));
}

/** Application extension: id of id_len bytes, then one data sub-block (data, n), then the terminator */
static inline void gif_app(cb_buf_t *b, const char *id, size_t id_len, const uint8_t *data, size_t n)
{
	cb_u8(b, 0x21);
	cb_u8(b, 0xFF);
	cb_u8(b, (unsigned)id_len);
	cb_put(b, id, id_len);
	cb_u8(b, (unsigned)n);
	cb_put(b, data, n);
	cb_u8(b, 0);
}

/** LZW raster data (min code size m) of n pixels (stream order), literals only (a CLEAR code before the code
 *  table would grow, so the code width stays m + 1); eoi: append the EOI code */
static inline void gif_lzw(cb_buf_t *b, unsigned m, const uint8_t *px, size_t n, int eoi)
{
	cb_u8(b, m);
	const unsigned clear = 1u << m;
	const unsigned width = m + 1;
	const unsigned max_literals = (1u << m) - 2; /* the next CLEAR is still read at width m + 1 */
	cb_buf_t raw = { 0 };
	uint32_t acc = 0;
	unsigned nbits = 0;
	unsigned since = 0;
#define GIF_EMIT(code)                          \
	do {                                        \
		acc |= (uint32_t)(code) << nbits;       \
		nbits += width;                         \
		while (nbits >= 8) {                    \
			cb_u8(&raw, acc & 0xFF);            \
			acc >>= 8;                          \
			nbits -= 8;                         \
		}                                       \
	} while (0)
	GIF_EMIT(clear);
	for (size_t i = 0; i < n; i++) {
		if (since == max_literals) {
			GIF_EMIT(clear);
			since = 0;
		}
		GIF_EMIT(px[i]);
		since++;
	}
	if (eoi) {
		GIF_EMIT(clear + 1);
	}
#undef GIF_EMIT
	if (nbits > 0) {
		cb_u8(&raw, acc & 0xFF);
	}
	for (size_t off = 0; off < raw.n;) {
		size_t len = raw.n - off > 255 ? 255 : raw.n - off;
		cb_u8(b, (unsigned)len);
		cb_put(b, raw.d + off, len);
		off += len;
	}
	cb_u8(b, 0);
	cb_free(&raw);
}

/** Image descriptor (+ local colour table if lct_bits >= 0) + raster */
static inline void gif_image(cb_buf_t *b, unsigned l, unsigned t, unsigned w, unsigned h, int interlace, int lct_bits, const uint8_t *lpal, unsigned m, const uint8_t *px, size_t n)
{
	cb_u8(b, 0x2C);
	cb_le16(b, l);
	cb_le16(b, t);
	cb_le16(b, w);
	cb_le16(b, h);
	cb_u8(b, (interlace ? 0x40 : 0) | (lct_bits >= 0 ? (0x80 | (unsigned)lct_bits) : 0));
	if (lct_bits >= 0) {
		cb_put(b, lpal, 3u << (lct_bits + 1));
	}
	gif_lzw(b, m, px, n, 1);
}

/** w x h image of one colour index (code size 2) */
static inline void gif_fill_image(cb_buf_t *b, unsigned l, unsigned t, unsigned w, unsigned h, uint8_t index)
{
	uint8_t *px = (uint8_t *)malloc((size_t)w * h + 1);
	if (px == NULL) {
		abort();
	}
	memset(px, index, (size_t)w * h);
	gif_image(b, l, t, w, h, 0, -1, NULL, 2, px, (size_t)w * h);
	free(px);
}

/** Bytes of heap in use (glibc), 0 elsewhere: a leak check compares it around repeated operations */
static inline size_t cb_heap_in_use(void)
{
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
	struct mallinfo2 mi = mallinfo2();
	return mi.uordblks + mi.hblkhd;
#else
	return 0;
#endif
}

/** Log callback that drops every message (img2_set_log_callback(cb_log_sink, NULL) silences a loop) */
static inline void cb_log_sink(img2_log_level_t level, const char *msg, void *user)
{
	(void)level;
	(void)msg;
	(void)user;
}

/** img2_decode via the dispatcher (MIME sniffed); returns the status, frames in *out */
static inline img2_status_t cb_decode(const uint8_t *data, size_t len, img2_frames_t *out)
{
	img2_decode_opts_t opts = IMG2_DECODE_OPTS_INIT;
	return img2_decode(data, len, &opts, out);
}

/** RGBA of pixel (x, y) of frame f */
static inline const uint8_t *cb_px(const img2_frames_t *fr, int f, uint32_t x, uint32_t y)
{
	const img2_image_t *img = fr->frames[f];
	return img->pixels + ((size_t)y * img->width + x) * 4;
}

#define CB_ASSERT_PX(fr, f, x, y, r, g, b, a)            \
	do {                                                  \
		const uint8_t *cb_p_ = cb_px((fr), (f), (x), (y)); \
		ASSERT_EQUAL((r), cb_p_[0]);                      \
		ASSERT_EQUAL((g), cb_p_[1]);                      \
		ASSERT_EQUAL((b), cb_p_[2]);                      \
		ASSERT_EQUAL((a), cb_p_[3]);                      \
	} while (0)

#endif /* IMG2_TESTS_COV_B_UTIL_H */
