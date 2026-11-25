/**
 * @file test_alloc_fail.c
 * @brief Allocation-failure paths: every allocation of an operation is failed once (alloc_fail.h)
 *
 * Linked with -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=strdup (Linux only, see
 * src/tests/CMakeLists.txt); nothing is compiled into libimg2. Each operation must survive every injected
 * failure: no crash, and an error status (or a documented fallback) instead of a half-built result.
 * The HEIF/AVIF decoders are not exercised here (docs/coverage.md, "Off-limits in phase 1").
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <img2/img2.h>

#include "../../libimg2/core/image.h"
#include "../../libimg2/core/log.h"
#include "../../libimg2/decoders/decoder.h"
#include "../../libimg2/metadata/exif_reader.h"
#include "../../libimg2/metadata/xml_parser.h"
#include "../../libimg2/text/font_manager.h"
#include "../ctest.h"
#include "alloc_fail.h"

#ifndef IMG2_TEST_SRC_DIR
#error "IMG2_TEST_SRC_DIR must be defined by the build system"
#endif

#define AF_MAX_RUNS 4000

/* result of the last operation run; checked after the loop (ASSERT must not fire while armed) */
static int s_bad;

static uint8_t *af_read_file(const char *rel, size_t *len)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/data/%s", IMG2_TEST_SRC_DIR, rel);
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *buf = malloc((size_t)n);
	if (buf != NULL && fread(buf, 1, (size_t)n, f) != (size_t)n) {
		free(buf);
		buf = NULL;
	}
	fclose(f);
	*len = (size_t)n;
	return buf;
}

static img2_image_t *af_pattern(uint32_t w, uint32_t h)
{
	img2_image_t *img = img2_image_alloc(w, h);
	if (img != NULL) {
		for (size_t i = 0; i < (size_t)w * h * 4; i++) {
			img->pixels[i] = (uint8_t)(i * 13 + (i >> 5));
		}
	}
	return img;
}

/* ===== image / core ===== */

static void op_create(void *ctx)
{
	(void)ctx;
	img2_image_t *img = NULL;
	img2_status_t st = img2_image_create(4, 3, &img);
	if ((st == IMG2_OK) != (img != NULL) || (st != IMG2_OK && st != IMG2_E_NOMEM)) {
		s_bad++;
	}
	img2_image_destroy(img);
}

typedef struct {
	img2_image_t *src;
	img2_filter_t filter;
	uint16_t orientation;
} af_img_ctx_t;

static void op_scale(void *ctx)
{
	af_img_ctx_t *c = ctx;
	img2_image_t *out = NULL;
	img2_status_t st = img2_scale(c->src, 7, 5, c->filter, &out);
	if ((st == IMG2_OK) != (out != NULL)) {
		s_bad++;
	}
	img2_image_destroy(out);
	st = img2_scale_fit(c->src, 9, 9, c->filter, &out);
	if ((st == IMG2_OK) != (out != NULL)) {
		s_bad++;
	}
	img2_image_destroy(out);
}

static void op_convert(void *ctx)
{
	(void)ctx;
	const uint8_t px[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
	img2_image_t *out = NULL;
	if (img2_convert_rgb_to_rgba(px, 2, 2, &out) != IMG2_OK && out != NULL) {
		s_bad++;
	}
	img2_image_destroy(out);
	out = NULL;
	if (img2_convert_gray_to_rgba(px, 3, 4, &out) != IMG2_OK && out != NULL) {
		s_bad++;
	}
	img2_image_destroy(out);
}

static void op_orient(void *ctx)
{
	af_img_ctx_t *c = ctx;
	img2_image_t *img = af_pattern(3, 2);
	if (img == NULL) {
		return;
	}
	img2_status_t st = img2_image_orient(img, c->orientation);
	if (st != IMG2_OK && (img->width != 3 || img->height != 2)) {
		s_bad++; /* a failed transform must leave the image untouched */
	}
	img2_image_destroy(img);
}

static int s_log_calls;
static void af_log_cb(img2_log_level_t level, const char *msg, void *user)
{
	(void)level;
	(void)user;
	if (msg != NULL && msg[0] == 'x') {
		s_log_calls++;
	}
}

static void op_log_long(void *ctx)
{
	(void)ctx;
	char big[2048];
	memset(big, 'x', sizeof(big) - 1);
	big[sizeof(big) - 1] = '\0';
	img2_log(IMG2_LOG_ERROR, "%s", big);
}

CTEST(alloc_fail, core_operations)
{
	ASSERT_TRUE(af_for_each_failure(op_create, NULL, AF_MAX_RUNS) >= 2);
	ASSERT_EQUAL(0, s_bad);
	ASSERT_TRUE(af_for_each_failure(op_convert, NULL, AF_MAX_RUNS) >= 4);
	ASSERT_EQUAL(0, s_bad);

	af_img_ctx_t c = { af_pattern(6, 4), IMG2_FILTER_DEFAULT, 1 };
	ASSERT_NOT_NULL(c.src);
	const img2_filter_t filters[] = { IMG2_FILTER_DEFAULT, IMG2_FILTER_MITCHELL };
	for (size_t i = 0; i < 2 && !AF_ASAN; i++) { /* AF_ASAN: alloc_fail.h */
		c.filter = filters[i];
		ASSERT_TRUE(af_for_each_failure(op_scale, &c, AF_MAX_RUNS) >= 3);
		ASSERT_EQUAL(0, s_bad);
	}
	img2_image_destroy(c.src);

	for (uint16_t o = 2; o <= 8; o++) {
		c.orientation = o;
		af_for_each_failure(op_orient, &c, AF_MAX_RUNS);
		ASSERT_EQUAL(0, s_bad);
	}

	/* a long message whose heap buffer cannot be allocated is delivered truncated */
	img2_log_level_t saved = img2_get_log_level();
	img2_set_log_level(IMG2_LOG_ERROR);
	img2_set_log_callback(af_log_cb, NULL);
	s_log_calls = 0;
	long runs = af_for_each_failure(op_log_long, NULL, AF_MAX_RUNS);
	img2_set_log_callback(NULL, NULL);
	img2_set_log_level(saved);
	ASSERT_EQUAL(1, runs); /* nothing to escape: no copy */
	ASSERT_EQUAL(2, s_log_calls);
}

static char s_log_last[2048];
static void af_log_keep(img2_log_level_t level, const char *msg, void *user)
{
	(void)level;
	(void)user;
	snprintf(s_log_last, sizeof(s_log_last), "%s", msg);
}

static void op_log_escape_long(void *ctx)
{
	(void)ctx;
	char big[700];
	memset(big, '\x1b', sizeof(big) - 1);
	big[sizeof(big) - 1] = '\0';
	img2_log(IMG2_LOG_ERROR, "%s", big);
	if (strchr(s_log_last, '\x1b') != NULL) {
		s_bad++;
	}
}

CTEST(alloc_fail, log_escape_fallback)
{
	img2_log_level_t saved = img2_get_log_level();
	img2_set_log_level(IMG2_LOG_ERROR);
	img2_set_log_callback(af_log_keep, NULL);

	/* [F22] the escaped form (812 bytes) does not fit the stack buffer and its heap copy fails: every byte
	 * that would be escaped becomes '?' in place, the rest (final '\n' included) is kept */
	char raw[256], want[256];
	memset(raw, '\x1b', 200);
	memcpy(raw + 200, "\xc2\x9b" "ok\xc3\xa9\n", 8);
	memset(want, '?', 202);
	memcpy(want + 202, "ok\xc3\xa9\n", 6);
	af_arm(0);
	img2_log(IMG2_LOG_ERROR, "%s", raw);
	bool fired = af_fired();
	af_disarm();
	ASSERT_TRUE(fired);
	ASSERT_STR(want, s_log_last);

	/* long message: the raw heap buffer (truncated text delivered) and the escape copy each fail once */
	long runs = af_for_each_failure(op_log_escape_long, NULL, AF_MAX_RUNS);
	ASSERT_EQUAL(2, runs);
	ASSERT_EQUAL(0, s_bad);

	img2_set_log_callback(NULL, NULL);
	img2_set_log_level(saved);
}

/* ===== decoders ===== */

typedef struct {
	const uint8_t *data;
	size_t len;
	img2_decode_opts_t opts;
} af_dec_ctx_t;

static void op_decode(void *ctx)
{
	af_dec_ctx_t *c = ctx;
	img2_frames_t out = { 0 };
	img2_status_t st = img2_decode(c->data, c->len, &c->opts, &out);
	if (st == IMG2_OK) {
		if (out.count < 1 || out.frames == NULL) {
			s_bad++;
		}
	} else if (out.frames != NULL || out.count != 0) {
		s_bad++;
	}
	img2_frames_free(&out);
}

static long af_decode_all(const uint8_t *data, size_t len, img2_mime_t force, long max_runs)
{
	af_dec_ctx_t c = { data, len, IMG2_DECODE_OPTS_INIT };
	c.opts.force_mime = force;
	/* vendored jpeg-quantsmooth does not check its allocations (quantsmooth.h upsample_row): deblocking off */
	c.opts.jpeg_deblock = 0;
	return af_for_each_failure(op_decode, &c, max_runs);
}

static uint8_t *af_encode(img2_format_t fmt, uint32_t w, uint32_t h, size_t *len)
{
	img2_image_t *img = af_pattern(w, h);
	if (img == NULL) {
		return NULL;
	}
	img2_image_t *arr[1] = { img };
	img2_frames_t in = { arr, 1, 0 };
	uint8_t *buf = NULL;
	if (img2_encode(&in, fmt, NULL, &buf, len) != IMG2_OK) {
		buf = NULL;
	}
	img2_image_destroy(img);
	return buf;
}

CTEST(alloc_fail, decode_generated_formats)
{
	/* PNM, QOI, BMP (stb), ICO with a DIB and with a PNG payload, raw YUV */
	const char pgm[] = "P2 2 2 255 1 2 3 4";
	ASSERT_TRUE(af_decode_all((const uint8_t *)pgm, sizeof(pgm) - 1, IMG2_MIME_UNKNOWN, AF_MAX_RUNS) >= 3);
	ASSERT_EQUAL(0, s_bad);

	uint8_t qoi[64] = { 'q', 'o', 'i', 'f', 0, 0, 0, 1, 0, 0, 0, 1, 4, 0, 0xFE, 1, 2, 3 };
	memset(qoi + 18, 0, 7);
	qoi[25] = 1;
	ASSERT_TRUE(af_decode_all(qoi, 26, IMG2_MIME_UNKNOWN, AF_MAX_RUNS) >= 3);
	ASSERT_EQUAL(0, s_bad);

	uint8_t bmp[58] = { 'B', 'M', 58, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 24 };
	bmp[54] = 9;
	ASSERT_TRUE(af_decode_all(bmp, sizeof(bmp), IMG2_MIME_UNKNOWN, AF_MAX_RUNS) >= 2);
	ASSERT_EQUAL(0, s_bad);

	uint8_t ico[22 + 40 + 4 + 4] = { 0, 0, 1, 0, 1, 0, 1, 1, 0, 0, 1, 0, 32, 0, 48, 0, 0, 0, 22, 0, 0, 0 };
	ico[22] = 40;
	ico[26] = 1;
	ico[30] = 2; /* height 2 = 1 row + AND mask */
	ico[34] = 1;
	ico[36] = 32;
	ASSERT_TRUE(af_decode_all(ico, sizeof(ico), IMG2_MIME_UNKNOWN, AF_MAX_RUNS) >= 2);
	ASSERT_EQUAL(0, s_bad);

	img2_yuv_params_t yp = { IMG2_YUV_FMT_I420, 2, 2, 0, 0 };
	uint8_t yuv[6] = { 16, 80, 160, 235, 100, 150 };
	af_dec_ctx_t yc = { yuv, sizeof(yuv), IMG2_DECODE_OPTS_INIT };
	yc.opts.force_mime = IMG2_MIME_YUV;
	yc.opts.yuv = &yp;
	ASSERT_TRUE(af_for_each_failure(op_decode, &yc, AF_MAX_RUNS) >= 2);
	ASSERT_EQUAL(0, s_bad);

	/* encoder output of each built encoder, decoded back (decoders of PNG, JPEG, WebP, JXL) */
	const img2_format_t fmts[] = { IMG2_FORMAT_PNG, IMG2_FORMAT_JPEG, IMG2_FORMAT_WEBP, IMG2_FORMAT_JXL };
	for (size_t i = 0; i < sizeof(fmts) / sizeof(fmts[0]); i++) {
		if (!img2_can_encode(fmts[i])) {
			continue;
		}
		size_t len = 0;
		uint8_t *enc = af_encode(fmts[i], 5, 3, &len);
		ASSERT_NOT_NULL(enc);
		ASSERT_TRUE(af_decode_all(enc, len, IMG2_MIME_UNKNOWN, AF_MAX_RUNS) >= 2);
		ASSERT_EQUAL(0, s_bad);
		img2_free(enc);
	}
}

CTEST(alloc_fail, decode_fixture_files)
{
	const char *files[] = { "anim/smoke_anim.gif", "anim/smoke_anim.png", "anim/smoke_anim.webp", "png/sample_exif.png", "png/pal4_trns_interlaced.png", "png/rgb16.png", "jpg/Canon_40D.jpg", "webp/sample_exif.webp" };
	/* not TIFF: vendored libtiff reads a NULL ExtraSamples array after an allocation failure inside
	 * TIFFFetchNormalTag (tif_getimage.c TIFFRGBAImageBegin) */
	for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
		size_t len = 0;
		uint8_t *data = af_read_file(files[i], &len);
		if (data == NULL) {
			CTEST_ERR("cannot read %s", files[i]);
		}
		img2_mime_t mime = img2_detect_mime(data, len);
		if (img2_can_decode(mime)) {
			ASSERT_TRUE(af_decode_all(data, len, IMG2_MIME_UNKNOWN, AF_MAX_RUNS) >= 1);
			ASSERT_EQUAL(0, s_bad);
		}
		free(data);
	}
}

/* ===== encoders ===== */

typedef struct {
	img2_format_t fmt;
	img2_frames_t in;
} af_enc_ctx_t;

static void op_encode(void *ctx)
{
	af_enc_ctx_t *c = ctx;
	uint8_t *buf = NULL;
	size_t len = 0;
	img2_status_t st = img2_encode(&c->in, c->fmt, NULL, &buf, &len);
	if ((st == IMG2_OK) != (buf != NULL)) {
		s_bad++;
	}
	img2_free(buf);
}

CTEST(alloc_fail, encode_every_format)
{
	img2_image_t *img = af_pattern(6, 5);
	ASSERT_NOT_NULL(img);
	img2_image_t *arr[1] = { img };
	for (size_t i = 0; i < img2_encoder_count(); i++) {
		af_enc_ctx_t c = { img2_encoder_info(i)->format, { arr, 1, 0 } };
		if (c.fmt == IMG2_FORMAT_HEIF) {
			continue; /* vendored libheif's kvazaar plugin dereferences a failed kvz_config_alloc() */
		}
		ASSERT_TRUE(af_for_each_failure(op_encode, &c, AF_MAX_RUNS) >= 1);
		ASSERT_EQUAL(0, s_bad);
	}
	img2_image_destroy(img);
}

/* ===== metadata passthrough (docs/encoder-api.md phase 3) ===== */

static const uint8_t k_af_tiff[26] = { 'I', 'I', 42, 0, 8, 0, 0, 0, 1, 0, 0x12, 0x01, 3, 0, 1, 0, 0, 0, 6, 0 };
static const char k_af_xmp[] = "<rdf:RDF><rdf:Description><dc:title>t</dc:title></rdf:Description></rdf:RDF>";

/* the setter fails as a whole: on error the image keeps its old blocks */
static void op_set_metadata(void *ctx)
{
	img2_image_t *img = ctx;
	const uint8_t *old_exif = img->exif_raw;
	const char *old_xmp = img->xmp_raw;
	img2_status_t st = img2_image_set_metadata(img, k_af_tiff, sizeof(k_af_tiff), k_af_xmp, sizeof(k_af_xmp) - 1);
	if (st == IMG2_OK) {
		if (img->exif_raw_len != sizeof(k_af_tiff) || img->xmp_raw_len != sizeof(k_af_xmp) - 1) {
			s_bad++;
		}
	} else if (st != IMG2_E_NOMEM || img->exif_raw != old_exif || img->xmp_raw != old_xmp) {
		s_bad++;
	}
}

/* img2_apply_orientation: a failed transform keeps the image and its orientation field */
static void op_apply_orientation(void *ctx)
{
	(void)ctx;
	img2_image_t *img = af_pattern(3, 2);
	if (img == NULL) {
		return;
	}
	img->orientation = 6;
	img2_status_t st = img2_apply_orientation(&img);
	if (st == IMG2_OK ? (img->orientation != 1 || img->width != 2) : (st != IMG2_E_NOMEM || img->orientation != 6 || img->width != 3)) {
		s_bad++;
	}
	img2_image_destroy(img);
}

static void op_encode_meta(void *ctx)
{
	af_enc_ctx_t *c = ctx;
	img2_encode_opts_t eo = IMG2_ENCODE_OPTS_INIT;
	eo.metadata = IMG2_META_EXIF | IMG2_META_XMP;
	uint8_t *buf = NULL;
	size_t len = 0;
	img2_status_t st = img2_encode(&c->in, c->fmt, &eo, &buf, &len);
	if ((st == IMG2_OK) != (buf != NULL)) {
		s_bad++;
	}
	img2_free(buf);
}

CTEST(alloc_fail, metadata_passthrough)
{
	img2_image_t *img = af_pattern(6, 5);
	ASSERT_NOT_NULL(img);
	ASSERT_EQUAL(IMG2_OK, img2_image_set_metadata(img, k_af_tiff, 8, "<a>", 3));
	ASSERT_EQUAL(2, af_for_each_failure(op_set_metadata, img, AF_MAX_RUNS));
	ASSERT_EQUAL(0, s_bad);
	ASSERT_TRUE(af_for_each_failure(op_apply_orientation, NULL, AF_MAX_RUNS) >= 3);
	ASSERT_EQUAL(0, s_bad);

	/* scaling copies the blocks (the copy's allocations fail too) */
	ASSERT_EQUAL(IMG2_OK, img2_image_set_metadata(img, k_af_tiff, sizeof(k_af_tiff), k_af_xmp, sizeof(k_af_xmp) - 1));
	img->orientation = 6;
	af_img_ctx_t sc = { img, IMG2_FILTER_BOX, 1 };
	if (!AF_ASAN) { /* alloc_fail.h: stb_image_resize2's ASan-only allocation mode */
		ASSERT_TRUE(af_for_each_failure(op_scale, &sc, AF_MAX_RUNS) >= 5);
		ASSERT_EQUAL(0, s_bad);
	}

	/* every writer with both blocks, and their output decoded back (the decoders keep the blocks) */
	img2_image_t *arr[1] = { img };
	for (size_t i = 0; i < img2_encoder_count(); i++) {
		af_enc_ctx_t c = { img2_encoder_info(i)->format, { arr, 1, 0 } };
		if (c.fmt == IMG2_FORMAT_HEIF) {
			continue; /* vendored libheif's kvazaar plugin dereferences a failed kvz_config_alloc() */
		}
		if (c.fmt == IMG2_FORMAT_PPM) {
			continue; /* no metadata container and a single allocation (encode_every_format covers its failure) */
		}
		ASSERT_TRUE(af_for_each_failure(op_encode_meta, &c, AF_MAX_RUNS) >= 2);
		ASSERT_EQUAL(0, s_bad);
		if (c.fmt == IMG2_FORMAT_AVIF) {
			continue; /* not exercised here: the HEIF/AVIF decoders (file comment) */
		}
		img2_encode_opts_t eo = IMG2_ENCODE_OPTS_INIT;
		eo.metadata = IMG2_META_EXIF | IMG2_META_XMP;
		uint8_t *buf = NULL;
		size_t len = 0;
		ASSERT_EQUAL(IMG2_OK, img2_encode(&c.in, c.fmt, &eo, &buf, &len));
		ASSERT_TRUE(af_decode_all(buf, len, IMG2_MIME_UNKNOWN, AF_MAX_RUNS) >= 2);
		ASSERT_EQUAL(0, s_bad);
		img2_free(buf);
	}
	img2_image_destroy(img);
}

/* ===== metadata ===== */

static void op_xml(void *ctx)
{
	const char *xml = ctx;
	img2_xml_document_t *doc = img2_xml_document_create();
	if (doc == NULL) {
		return;
	}
	if (img2_xml_document_parse(doc, xml, strlen(xml)) == 0) {
		const img2_xml_node_t *root = img2_xml_document_root_element(doc);
		if (root == NULL || img2_xml_element_name(root) == NULL) {
			s_bad++;
		}
	}
	img2_xml_document_destroy(doc);
}

static void op_exif(void *ctx)
{
	af_dec_ctx_t *c = ctx;
	img2_exif_t exif;
	img2_exif_init(&exif);
	img2_exif_parse(&exif, c->data, c->len);
	img2_exif_clear(&exif);
	img2_xmp_t xmp;
	img2_xmp_init(&xmp);
	img2_xmp_parse(&xmp, c->data, c->len);
	img2_xmp_clear(&xmp);
}

CTEST(alloc_fail, metadata_parsers)
{
	static char xml[70000];
	/* enough attributes to grow the attribute array, a comment, text, and a value past one arena block */
	int n = snprintf(xml, sizeof(xml), "<?xml version='1.0'?><!-- c --><r a='1' b='2' c='3' d='4' e='5'><!-- in -->text<k/><v x=\"");
	memset(xml + n, 'v', 66000);
	snprintf(xml + n + 66000, sizeof(xml) - (size_t)n - 66000, "\"/></r>");
	af_for_each_failure(op_xml, xml, AF_MAX_RUNS);
	ASSERT_EQUAL(0, s_bad);

	size_t len = 0;
	uint8_t *jpg = af_read_file("jpg/Canon_40D.jpg", &len);
	ASSERT_NOT_NULL(jpg);
	af_dec_ctx_t c = { jpg, len, IMG2_DECODE_OPTS_INIT };
	af_for_each_failure(op_exif, &c, AF_MAX_RUNS); /* the EXIF/XMP readers may not allocate at all */
	free(jpg);
}

/* ===== fonts ===== */

static void op_fonts(void *ctx)
{
	(void)ctx;
	img2_init();
	img2_font_t *font = NULL;
	if (img2_font_load("DejaVu Sans Mono", IMG2_FONT_WEIGHT_BOLD, IMG2_FONT_STYLE_ITALIC, &font) == IMG2_OK) {
		img2_font_face_t *face = img2_font_face_open(font);
		if (face != NULL) {
			img2_font_vertex_t *v = NULL;
			img2_font_face_glyph_shape(face, 'A', &v);
			img2_font_face_free_shape(v);
			img2_font_face_close(face);
		}
	}
	img2_font_load_fallback(&font);
	img2_shutdown();
}

static int af_count_font(const img2_font_info_t *info, void *user)
{
	(void)info;
	(*(int *)user)++;
	return 0;
}

static void op_font_foreach(void *ctx)
{
	img2_init();
	img2_font_foreach(af_count_font, ctx);
	img2_shutdown();
}

CTEST(alloc_fail, font_manager)
{
	/* Every run rebuilds the font cache, which scans the host's font directories first: fail the first
	 * allocations of the scan and every allocation after it (embedded entries, fallback, handles, faces). */
	long total = af_count_allocs(op_fonts, NULL);
	ASSERT_TRUE(total > 10);
	af_fail_range(op_fonts, NULL, 0, 8);
	af_fail_range(op_fonts, NULL, total - 24, total);

	int count = 0;
	total = af_count_allocs(op_font_foreach, &count);
	ASSERT_TRUE(count > 0);
	af_fail_range(op_font_foreach, &count, total - 12, total);
}
