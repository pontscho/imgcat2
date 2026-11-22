/**
 * @file fuzz_seedgen.c
 * @brief Seed corpus generator of the fuzz targets (IMG2_FUZZ builds; run by scripts/fuzz.sh)
 *
 *   fuzz_seedgen <out_dir> [file...]
 *
 * Writes small valid inputs into <out_dir>/{decode,decode_forced,exif,xmp_xml,yuv}: images encoded with
 * libimg2's encoders (JPEG, PNG, WebP lossy/lossless, HEIF, JXL), stb_image_write (BMP, TGA, HDR), qoi.h (QOI),
 * hand-built PNM (P1-P7), ICO/CUR (DIB 1/4/8/24/32 bpp and PNG entries), GIF and SVG, JPEGs carrying EXIF
 * (every orientation) and XMP, TIFF/EXIF blobs, XMP packets and raw YUV frames in the fuzz_yuv input format.
 * Each extra <file> (e.g. src/tests/data) is copied into decode/, into decode_forced/ with the selector prefix of
 * its detected type, and (JPEG / TIFF) into exif/ and xmp_xml/ with their mode prefix.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <img2/img2.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stb_image_write.h"
#pragma GCC diagnostic pop

#define QOI_NO_STDIO
#define qoi_encode img2_qoi_encode /* libimg2 renames qoi.h's extern functions (decoder_qoi.c) */
#define qoi_decode img2_qoi_decode
#include "qoi.h"

/* Same order as fuzz_decode_forced.c's k_mimes */
static const img2_mime_t k_mimes[] = {
	IMG2_MIME_PNG, IMG2_MIME_JPEG, IMG2_MIME_GIF, IMG2_MIME_BMP, IMG2_MIME_TGA, IMG2_MIME_PSD,
	IMG2_MIME_HDR, IMG2_MIME_PNM, IMG2_MIME_WEBP, IMG2_MIME_HEIF, IMG2_MIME_TIFF, IMG2_MIME_RAW,
	IMG2_MIME_QOI, IMG2_MIME_ICO, IMG2_MIME_CUR, IMG2_MIME_JXL, IMG2_MIME_SVG, IMG2_MIME_AVIF,
};

static const char *g_out;
static int g_count;

/* ---------- byte buffer ---------- */

typedef struct {
	uint8_t *p;
	size_t n;
	size_t cap;
} buf_t;

static void b_put(buf_t *b, const void *d, size_t n)
{
	if (b->n + n > b->cap) {
		size_t cap = (b->cap + n) * 2 + 64;
		uint8_t *q = realloc(b->p, cap);
		if (q == NULL) {
			abort();
		}
		b->p = q;
		b->cap = cap;
	}
	memcpy(b->p + b->n, d, n);
	b->n += n;
}
static void b_u8(buf_t *b, unsigned v)
{
	uint8_t c = (uint8_t)v;
	b_put(b, &c, 1);
}
static void b_u16(buf_t *b, unsigned v, int be)
{
	uint8_t c[2] = { (uint8_t)(be ? v >> 8 : v), (uint8_t)(be ? v : v >> 8) };
	b_put(b, c, 2);
}
static void b_u32(buf_t *b, uint32_t v, int be)
{
	for (int i = 0; i < 4; i++) {
		b_u8(b, be ? v >> (24 - 8 * i) : v >> (8 * i));
	}
}
static void b_str(buf_t *b, const char *s)
{
	b_put(b, s, strlen(s));
}
static void b_free(buf_t *b)
{
	free(b->p);
	memset(b, 0, sizeof(*b));
}
/* stbi_write_func adapter (exact signature: -fsanitize=function checks indirect calls) */
static void stb_sink(void *ctx, void *data, int size)
{
	b_put(ctx, data, (size_t)size);
}

/* ---------- output ---------- */

static void write_file(const char *sub, const char *name, const uint8_t *pre, size_t pre_len, const void *d, size_t n)
{
	char path[4096];
	snprintf(path, sizeof(path), "%s/%s", g_out, sub);
	if (mkdir(path, 0755) != 0 && errno != EEXIST) {
		perror(path);
		exit(1);
	}
	snprintf(path, sizeof(path), "%s/%s/%s", g_out, sub, name);
	FILE *f = fopen(path, "wb");
	if (f == NULL) {
		perror(path);
		exit(1);
	}
	if ((pre_len > 0 && fwrite(pre, 1, pre_len, f) != pre_len) || (n > 0 && fwrite(d, 1, n, f) != n) || fclose(f) != 0) {
		perror(path);
		exit(1);
	}
	g_count++;
}

/* An image seed: decode/ as is, decode_forced/ with the selector of its type (two option variants) */
static void emit_image(const char *name, const void *d, size_t n)
{
	img2_mime_t m = img2_detect_mime(d, n);
	write_file("decode", name, NULL, 0, d, n);
	for (size_t i = 0; i < sizeof(k_mimes) / sizeof(k_mimes[0]); i++) {
		if (k_mimes[i] == m) {
			uint8_t pre[2] = { (uint8_t)i, 0x00 };
			write_file("decode_forced", name, pre, 2, d, n);
			char alt[512];
			snprintf(alt, sizeof(alt), "%s.opt", name);
			pre[1] = 0x05; /* max_frames 1, jpeg_deblock default */
			write_file("decode_forced", alt, pre, 2, d, n);
		}
	}
}

/* ---------- test image ---------- */

static img2_image_t *make_image(uint32_t w, uint32_t h)
{
	img2_image_t *img = NULL;
	if (img2_image_create(w, h, &img) != IMG2_OK) {
		abort();
	}
	for (uint32_t y = 0; y < h; y++) {
		for (uint32_t x = 0; x < w; x++) {
			img2_image_set_pixel(img, x, y, (uint8_t)(x * 255 / w), (uint8_t)(y * 255 / h), (uint8_t)((x ^ y) * 37),
			                     (uint8_t)(255 - x * 9));
		}
	}
	return img;
}

static void seed_encoded(void)
{
	static const struct {
		img2_format_t fmt;
		int quality;
		const char *name;
	} encs[] = {
		{ IMG2_FORMAT_JPEG, 50, "enc_q50.jpg" },  { IMG2_FORMAT_JPEG, 95, "enc_q95.jpg" },
		{ IMG2_FORMAT_PNG, 60, "enc.png" },        { IMG2_FORMAT_WEBP, 70, "enc_lossy.webp" },
		{ IMG2_FORMAT_WEBP, 100, "enc_lossless.webp" }, { IMG2_FORMAT_HEIF, 40, "enc.heif" },
		{ IMG2_FORMAT_JXL, 70, "enc_lossy.jxl" },  { IMG2_FORMAT_JXL, 100, "enc_lossless.jxl" },
	};
	static const uint32_t dims[][2] = { { 17, 11 }, { 1, 1 }, { 64, 3 } };

	for (size_t d = 0; d < sizeof(dims) / sizeof(dims[0]); d++) {
		img2_image_t *img = make_image(dims[d][0], dims[d][1]);
		img2_frames_t fr = { &img, 1, 0 };
		for (size_t i = 0; i < sizeof(encs) / sizeof(encs[0]); i++) {
			if (!img2_can_encode(encs[i].fmt)) {
				continue;
			}
			img2_encode_opts_t o = { encs[i].quality };
			uint8_t *out = NULL;
			size_t len = 0;
			if (img2_encode(&fr, encs[i].fmt, &o, &out, &len) == IMG2_OK) {
				char name[128];
				snprintf(name, sizeof(name), "%ux%u_%s", dims[d][0], dims[d][1], encs[i].name);
				emit_image(name, out, len);
				img2_free(out);
			}
		}

		/* stb_image_write: BMP, TGA (raw and RLE), HDR; qoi.h: QOI */
		buf_t b = { 0 };
		char name[128];
		int w = (int)img->width, h = (int)img->height;
		stbi_write_bmp_to_func(stb_sink, &b, w, h, 4, img->pixels);
		snprintf(name, sizeof(name), "%dx%d_stb.bmp", w, h);
		emit_image(name, b.p, b.n);
		b.n = 0;
		stbi_write_bmp_to_func(stb_sink, &b, w, h, 3, img->pixels);
		snprintf(name, sizeof(name), "%dx%d_stb_rgb.bmp", w, h);
		emit_image(name, b.p, b.n);
		b.n = 0;
		stbi_write_tga_with_rle = 0;
		stbi_write_tga_to_func(stb_sink, &b, w, h, 4, img->pixels);
		snprintf(name, sizeof(name), "%dx%d_stb_raw.tga", w, h);
		emit_image(name, b.p, b.n);
		b.n = 0;
		stbi_write_tga_with_rle = 1;
		stbi_write_tga_to_func(stb_sink, &b, w, h, 4, img->pixels);
		snprintf(name, sizeof(name), "%dx%d_stb_rle.tga", w, h);
		emit_image(name, b.p, b.n);
		b.n = 0;
		float *hdr = malloc((size_t)w * h * 3 * sizeof(float));
		for (int k = 0; k < w * h * 3; k++) {
			hdr[k] = (float)img->pixels[(k / 3) * 4 + k % 3] / 64.0f;
		}
		stbi_write_hdr_to_func(stb_sink, &b, w, h, 3, hdr);
		free(hdr);
		snprintf(name, sizeof(name), "%dx%d_stb.hdr", w, h);
		emit_image(name, b.p, b.n);
		b_free(&b);

		qoi_desc desc = { (unsigned)w, (unsigned)h, 4, QOI_SRGB };
		int qlen = 0;
		void *q = qoi_encode(img->pixels, &desc, &qlen);
		if (q != NULL) {
			snprintf(name, sizeof(name), "%dx%d.qoi", w, h);
			emit_image(name, q, (size_t)qlen);
			free(q);
		}
		img2_image_destroy(img);
	}
}

/* ---------- PNM ---------- */

static void seed_pnm(void)
{
	buf_t b = { 0 };
	static const char p1[] = "P1\n# c\n3 2\n0 1 0\n1 0 1\n", p2[] = "P2\n3 2\n255\n0 128 255\n1 2 3\n",
	                  p3[] = "P3\n2 1\n15\n15 0 0 0 15 7\n";
	emit_image("p1.pbm", p1, strlen(p1));
	emit_image("p2.pgm", p2, strlen(p2));
	emit_image("p3.ppm", p3, strlen(p3));
	b_str(&b, "P4\n9 2\n");
	b_put(&b, "\xA5\x80\x5A\x00", 4);
	emit_image("p4.pbm", b.p, b.n);
	b.n = 0;
	b_str(&b, "P5\n3 2\n65535\n");
	b_put(&b, "\x00\x01\x80\x00\xff\xff\x12\x34\x00\x00\xff\x00", 12);
	emit_image("p5_16.pgm", b.p, b.n);
	b.n = 0;
	b_str(&b, "P6\n2 2\n255\n");
	b_put(&b, "\xff\x00\x00\x00\xff\x00\x00\x00\xff\x80\x80\x80", 12);
	emit_image("p6.ppm", b.p, b.n);
	b.n = 0;
	b_str(&b, "P7\nWIDTH 2\nHEIGHT 1\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n");
	b_put(&b, "\x01\x02\x03\x04\x05\x06\x07\x08", 8);
	emit_image("p7_rgba.pam", b.p, b.n);
	b.n = 0;
	b_str(&b, "P7\nWIDTH 2\nHEIGHT 2\nDEPTH 1\nMAXVAL 1\nTUPLTYPE BLACKANDWHITE\nENDHDR\n");
	b_put(&b, "\x00\x01\x01\x00", 4);
	emit_image("p7_bw.pam", b.p, b.n);
	b_free(&b);
}

/* ---------- ICO / CUR ---------- */

static void dib(buf_t *b, int w, int h, int bpp, int with_mask)
{
	int pal = bpp <= 8 ? 1 << bpp : 0;
	uint32_t stride = (uint32_t)((w * bpp + 31) / 32) * 4;
	uint32_t mstride = (uint32_t)((w + 31) / 32) * 4;
	b_u32(b, 40, 0);
	b_u32(b, (uint32_t)w, 0);
	b_u32(b, (uint32_t)(with_mask ? 2 * h : h), 0);
	b_u16(b, 1, 0);
	b_u16(b, (unsigned)bpp, 0);
	for (int i = 0; i < 6; i++) {
		b_u32(b, 0, 0);
	}
	for (int i = 0; i < pal; i++) {
		b_u32(b, (uint32_t)(i * 0x010203u), 0);
	}
	for (uint32_t i = 0; i < stride * (uint32_t)h; i++) {
		b_u8(b, i * 7u);
	}
	if (with_mask) {
		for (uint32_t i = 0; i < mstride * (uint32_t)h; i++) {
			b_u8(b, i & 1 ? 0xF0 : 0x0F);
		}
	}
}

static void ico_file(const char *name, int type, int n, const int *bpp, const int *wh, const int *png)
{
	buf_t head = { 0 }, body = { 0 }, tmp = { 0 };
	b_u16(&head, 0, 0);
	b_u16(&head, (unsigned)type, 0);
	b_u16(&head, (unsigned)n, 0);
	uint32_t off = 6 + 16u * (uint32_t)n;
	for (int i = 0; i < n; i++) {
		tmp.n = 0;
		if (png[i]) {
			img2_image_t *img = make_image((uint32_t)wh[i], (uint32_t)wh[i]);
			img2_frames_t fr = { &img, 1, 0 };
			uint8_t *out = NULL;
			size_t len = 0;
			if (img2_encode(&fr, IMG2_FORMAT_PNG, NULL, &out, &len) != IMG2_OK) {
				abort();
			}
			b_put(&tmp, out, len);
			img2_free(out);
			img2_image_destroy(img);
		} else {
			dib(&tmp, wh[i], wh[i], bpp[i], 1);
		}
		b_u8(&head, (unsigned)wh[i] & 0xFF);
		b_u8(&head, (unsigned)wh[i] & 0xFF);
		b_u8(&head, 0);
		b_u8(&head, 0);
		b_u16(&head, 1, 0);
		b_u16(&head, (unsigned)bpp[i], 0);
		b_u32(&head, (uint32_t)tmp.n, 0);
		b_u32(&head, off, 0);
		off += (uint32_t)tmp.n;
		b_put(&body, tmp.p, tmp.n);
	}
	b_put(&head, body.p, body.n);
	emit_image(name, head.p, head.n);
	b_free(&head);
	b_free(&body);
	b_free(&tmp);
}

static void seed_ico(void)
{
	static const int bpp_a[] = { 32, 8, 24 }, wh_a[] = { 5, 16, 3 }, png_a[] = { 0, 0, 0 };
	static const int bpp_b[] = { 4, 1 }, wh_b[] = { 7, 9 }, png_b[] = { 0, 0 };
	static const int bpp_c[] = { 32, 32 }, wh_c[] = { 2, 6 }, png_c[] = { 0, 1 };
	ico_file("dib_32_8_24.ico", 1, 3, bpp_a, wh_a, png_a);
	ico_file("dib_4_1.cur", 2, 2, bpp_b, wh_b, png_b);
	ico_file("png.ico", 1, 2, bpp_c, wh_c, png_c);
	ico_file("dib_4.ico", 1, 1, bpp_b, wh_b, png_b);
	ico_file("dib_1.ico", 1, 1, bpp_b + 1, wh_b + 1, png_b + 1);
}

/* ---------- GIF / SVG ---------- */

static void seed_gif_svg(void)
{
	static const uint8_t gif[] = { 'G', 'I', 'F', '8', '9', 'a', 2, 0, 2, 0, 0x80, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff,
		0x21, 0xF9, 4, 1, 10, 0, 1, 0, 0x2C, 0, 0, 0, 0, 2, 0, 2, 0, 0, 2, 2, 0x44, 0x01, 0, 0x3B };
	emit_image("tiny.gif", gif, sizeof(gif));
	static const char *svgs[] = {
		"<svg xmlns='http://www.w3.org/2000/svg' width='8' height='6'><rect x='1' y='1' width='5' height='3' fill='#f00'/></svg>",
		"<?xml version='1.0'?><svg viewBox='0 0 10 10' width='12'><circle cx='5' cy='5' r='4' stroke='blue' "
		"fill='none' stroke-width='2'/><path d='M1 1 L9 9 Q5 0 1 9 Z' fill='green' opacity='0.5'/></svg>",
		"<svg width='9' height='9'><defs><linearGradient id='g'><stop offset='0' stop-color='red'/>"
		"<stop offset='1' stop-color='blue'/></linearGradient></defs><ellipse cx='4' cy='4' rx='4' ry='2' "
		"fill='url(#g)' transform='rotate(30)'/><polygon points='0,0 8,0 4,8'/></svg>",
		"<svg width='40' height='12'><text x='0' y='10' font-size='10'>Hi<tspan font-weight='bold'>!</tspan></text></svg>",
	};
	for (size_t i = 0; i < sizeof(svgs) / sizeof(svgs[0]); i++) {
		char name[32];
		snprintf(name, sizeof(name), "s%zu.svg", i);
		emit_image(name, svgs[i], strlen(svgs[i]));
	}
}

/* ---------- EXIF / XMP ---------- */

typedef struct {
	uint16_t tag, type;
	uint32_t count;
	const void *data; /* NULL: value is inline */
	uint32_t inline_v;
} ent_t;

static size_t type_size(uint16_t t)
{
	return t == 3 || t == 8 ? 2 : t == 4 || t == 9 ? 4 : t == 5 || t == 10 ? 8 : 1;
}

/* Append an IFD at the current end of b (offsets relative to b's start); returns its offset */
static uint32_t put_ifd(buf_t *b, const ent_t *e, int n, int be, uint32_t *patch_at, int npatch)
{
	(void)npatch;
	uint32_t ifd = (uint32_t)b->n;
	uint32_t data_off = ifd + 2 + 12u * (uint32_t)n + 4;
	buf_t data = { 0 };
	b_u16(b, (unsigned)n, be);
	for (int i = 0; i < n; i++) {
		size_t sz = type_size(e[i].type) * e[i].count;
		b_u16(b, e[i].tag, be);
		b_u16(b, e[i].type, be);
		b_u32(b, e[i].count, be);
		if (patch_at != NULL && e[i].data == NULL && e[i].inline_v == 0xFFFFFFFFu) {
			*patch_at++ = (uint32_t)b->n; /* sub-IFD pointer, patched by the caller */
			b_u32(b, 0, be);
		} else if (sz <= 4 || e[i].data == NULL) {
			if (e[i].type == 3 && e[i].count == 1) {
				b_u16(b, e[i].inline_v, be);
				b_u16(b, 0, be);
			} else if (e[i].type == 2 && e[i].data != NULL) {
				uint8_t v[4] = { 0 };
				memcpy(v, e[i].data, sz);
				b_put(b, v, 4);
			} else {
				b_u32(b, e[i].inline_v, be);
			}
		} else {
			b_u32(b, data_off + (uint32_t)data.n, be);
			if (e[i].type == 5 || e[i].type == 10) {
				const uint32_t *r = e[i].data;
				for (uint32_t k = 0; k < 2 * e[i].count; k++) {
					b_u32(&data, r[k], be);
				}
			} else {
				b_put(&data, e[i].data, sz);
			}
			if (data.n & 1) {
				b_u8(&data, 0);
			}
		}
	}
	b_u32(b, 0, be); /* next IFD */
	b_put(b, data.p, data.n);
	b_free(&data);
	return ifd;
}

static void patch32(buf_t *b, uint32_t at, uint32_t v, int be)
{
	for (int i = 0; i < 4; i++) {
		b->p[at + i] = (uint8_t)(be ? v >> (24 - 8 * i) : v >> (8 * i));
	}
}

static void make_tiff(buf_t *b, int be, unsigned orientation)
{
	static const uint32_t expo[] = { 1, 250 }, fnum[] = { 28, 10 }, focal[] = { 50, 1 }, bias[] = { 0xFFFFFFFFu, 3 };
	static const uint32_t lat[] = { 47, 1, 29, 1, 3012, 100 }, lon[] = { 19, 1, 2, 1, 5, 1 }, alt[] = { 120, 1 };
	static const uint32_t res[] = { 72, 1 };
	ent_t ifd0[] = {
		{ 0x010F, 2, 6, "Canon", 0 }, { 0x0110, 2, 14, "EOS 5D Mark  ", 0 }, { 0x0112, 3, 1, NULL, orientation },
		{ 0x0131, 2, 4, "img", 0 }, { 0x0132, 2, 20, "2024:01:02 03:04:05", 0 }, { 0x013B, 2, 5, "Anna", 0 },
		{ 0x8298, 2, 9, "(c) 2024", 0 }, { 0x010E, 2, 8, "A photo", 0 }, { 0x0100, 4, 1, NULL, 640 },
		{ 0x0101, 3, 1, NULL, 480 }, { 0x011A, 5, 1, res, 0 }, { 0x011B, 5, 1, res, 0 }, { 0x0128, 3, 1, NULL, 2 },
		{ 0x8769, 4, 1, NULL, 0xFFFFFFFFu }, { 0x8825, 4, 1, NULL, 0xFFFFFFFFu },
	};
	ent_t exif[] = {
		{ 0x829A, 5, 1, expo, 0 }, { 0x829D, 5, 1, fnum, 0 }, { 0x8827, 3, 1, NULL, 400 },
		{ 0x9003, 2, 20, "2024:01:02 03:04:05", 0 }, { 0x9204, 10, 1, bias, 0 }, { 0x920A, 5, 1, focal, 0 },
		{ 0x9286, 7, 16, "ASCII\0\0\0comment!", 0 }, { 0x9209, 3, 1, NULL, 16 }, { 0xA405, 3, 1, NULL, 75 },
		{ 0xA434, 2, 12, "EF50mm f/1.8", 0 },
	};
	ent_t gps[] = {
		{ 0x0001, 2, 2, "N", 0 }, { 0x0002, 5, 3, lat, 0 }, { 0x0003, 2, 2, "W", 0 }, { 0x0004, 5, 3, lon, 0 },
		{ 0x0005, 1, 1, NULL, 1 }, { 0x0006, 5, 1, alt, 0 }, { 0x001D, 2, 11, "2024:01:02", 0 },
	};
	uint32_t patches[2];
	b_str(b, be ? "MM" : "II");
	b_u16(b, 42, be);
	b_u32(b, 8, be);
	put_ifd(b, ifd0, (int)(sizeof(ifd0) / sizeof(ifd0[0])), be, patches, 2);
	uint32_t e_off = put_ifd(b, exif, (int)(sizeof(exif) / sizeof(exif[0])), be, NULL, 0);
	uint32_t g_off = put_ifd(b, gps, (int)(sizeof(gps) / sizeof(gps[0])), be, NULL, 0);
	patch32(b, patches[0], e_off, be);
	patch32(b, patches[1], g_off, be);
}

static const char k_xmp[] =
	"<?xpacket begin='' id='W5M0MpCehiHzreSzNTczkc9d'?><x:xmpmeta xmlns:x='adobe:ns:meta/'>"
	"<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:Description rdf:about=''>"
	"<dc:creator><rdf:Seq><rdf:li>Anna</rdf:li></rdf:Seq></dc:creator><dc:rights>(c) A &amp; B</dc:rights>"
	"<dc:description>Lake</dc:description><dc:title>T</dc:title><xmp:Rating>4</xmp:Rating>"
	"<xmp:CreateDate>2024-01-02T03:04:05</xmp:CreateDate><xmp:ModifyDate>2024-02-02</xmp:ModifyDate>"
	"<xmp:Label>Red</xmp:Label><photoshop:City>Pecs</photoshop:City><photoshop:Country>HU</photoshop:Country>"
	"<photoshop:Credit>c</photoshop:Credit><photoshop:Source>s</photoshop:Source>"
	"<dc:subject><rdf:Bag><rdf:li>one</rdf:li><rdf:li>two</rdf:li><!-- x --><rdf:li/></rdf:Bag></dc:subject>"
	"</rdf:Description></rdf:RDF></x:xmpmeta><?xpacket end='w'?>";

/* JPEG: SOI, APP1 segment(s), then the rest of an encoded JPEG (or a bare SOS/EOI) */
static void jpeg_with_app1(buf_t *out, const char *id, size_t id_len, const uint8_t *payload, size_t n,
                           const uint8_t *jpeg, size_t jpeg_len)
{
	b_put(out, "\xFF\xD8\xFF\xE1", 4);
	b_u16(out, (unsigned)(2 + id_len + n), 1);
	b_put(out, id, id_len);
	b_put(out, payload, n);
	if (jpeg != NULL) {
		b_put(out, jpeg + 2, jpeg_len - 2);
	} else {
		b_put(out, "\xFF\xDA\x00\x02\xFF\xD9", 6);
	}
}

static void seed_metadata(void)
{
	uint8_t *jpg = NULL;
	size_t jlen = 0;
	img2_image_t *img = make_image(5, 3);
	img2_frames_t fr = { &img, 1, 0 };
	if (img2_encode(&fr, IMG2_FORMAT_JPEG, NULL, &jpg, &jlen) != IMG2_OK) {
		abort();
	}
	img2_image_destroy(img);

	static const uint8_t m_tiff = 0x00, m_jpeg = 0x01, m_xml = 0x00, m_xmp_jpeg = 0x01, m_dom = 0x02;
	for (unsigned o = 1; o <= 8; o++) {
		buf_t t = { 0 }, j = { 0 };
		char name[64];
		make_tiff(&t, o & 1, o);
		snprintf(name, sizeof(name), "tiff_o%u_%s", o, o & 1 ? "be" : "le");
		write_file("exif", name, &m_tiff, 1, t.p, t.n);
		jpeg_with_app1(&j, "Exif\0\0", 6, t.p, t.n, jpg, jlen);
		snprintf(name, sizeof(name), "jpeg_exif_o%u.jpg", o);
		write_file("exif", name, &m_jpeg, 1, j.p, j.n);
		emit_image(name, j.p, j.n);
		b_free(&t);
		b_free(&j);
	}

	buf_t x = { 0 };
	write_file("xmp_xml", "packet.xml", &m_xml, 1, k_xmp, strlen(k_xmp));
	write_file("xmp_xml", "packet_dom.xml", &m_dom, 1, k_xmp, strlen(k_xmp));
	jpeg_with_app1(&x, "http://ns.adobe.com/xap/1.0/\0", 29, (const uint8_t *)k_xmp, strlen(k_xmp), jpg, jlen);
	write_file("xmp_xml", "xmp.jpg", &m_xmp_jpeg, 1, x.p, x.n);
	emit_image("xmp.jpg", x.p, x.n);
	b_free(&x);
	img2_free(jpg);
}

/* ---------- YUV (fuzz_yuv input format) ---------- */

static void seed_yuv(void)
{
	static const char *specs[] = { "i420:4:4", "yv12:3:3:4:2", "nv12:5:2", "nv21:2:2", "i422:3:2:3:2",
		"yuv444p:2:2", "yuyv:3:2", "UYVY:2:1:8", "gray:3:3:5", "y800:1:1" };
	uint8_t frame[256];
	for (size_t i = 0; i < sizeof(frame); i++) {
		frame[i] = (uint8_t)(16 + (i * 29) % 224);
	}
	for (size_t i = 0; i < sizeof(specs) / sizeof(specs[0]); i++) {
		buf_t b = { 0 };
		char name[32];
		b_u8(&b, 0x80);
		b_str(&b, specs[i]);
		b_u8(&b, '\n');
		b_put(&b, frame, 96);
		snprintf(name, sizeof(name), "spec_%zu", i);
		write_file("yuv", name, NULL, 0, b.p, b.n);
		b_free(&b);
	}
	for (unsigned fmt = 1; fmt <= 9; fmt++) {
		buf_t b = { 0 };
		char name[32];
		b_u8(&b, 0x00);
		b_u8(&b, fmt);
		b_u16(&b, 3, 0);
		b_u16(&b, 2, 0);
		b_u16(&b, 0, 0);
		b_u16(&b, 0, 0);
		b_put(&b, frame, 64);
		snprintf(name, sizeof(name), "bin_%u", fmt);
		write_file("yuv", name, NULL, 0, b.p, b.n);
		b.n = 0;
		b_u8(&b, 0x01);
		b_u8(&b, fmt);
		b_u32(&b, 2, 0);
		b_u32(&b, 2, 0);
		b_u32(&b, 8, 0);
		b_u32(&b, 0, 0);
		b_u32(&b, fmt == 7 || fmt == 8 || fmt == 9 ? 0 : 4, 0);
		b_u32(&b, 0, 0);
		b_put(&b, frame, 64);
		snprintf(name, sizeof(name), "wide_%u", fmt);
		write_file("yuv", name, NULL, 0, b.p, b.n);
		b_free(&b);
	}
}

/* ---------- extra files ---------- */

static void seed_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		perror(path);
		return;
	}
	buf_t b = { 0 };
	uint8_t chunk[65536];
	size_t n;
	while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
		b_put(&b, chunk, n);
	}
	fclose(f);
	if (b.n == 0) {
		return;
	}
	const char *base = strrchr(path, '/');
	base = base != NULL ? base + 1 : path;
	emit_image(base, b.p, b.n);
	img2_mime_t m = img2_detect_mime(b.p, b.n);
	static const uint8_t one = 0x01, zero = 0x00;
	if (m == IMG2_MIME_JPEG) {
		write_file("exif", base, &one, 1, b.p, b.n);
		write_file("xmp_xml", base, &one, 1, b.p, b.n);
	} else if (m == IMG2_MIME_TIFF) {
		write_file("exif", base, &zero, 1, b.p, b.n < 65536 ? b.n : 65536);
	}
	b_free(&b);
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <out_dir> [file...]\n", argv[0]);
		return 2;
	}
	g_out = argv[1];
	if (mkdir(g_out, 0755) != 0 && errno != EEXIST) {
		perror(g_out);
		return 1;
	}
	seed_encoded();
	seed_pnm();
	seed_ico();
	seed_gif_svg();
	seed_metadata();
	seed_yuv();
	for (int i = 2; i < argc; i++) {
		seed_file(argv[i]);
	}
	printf("fuzz_seedgen: %d files in %s\n", g_count, g_out);
	return 0;
}
