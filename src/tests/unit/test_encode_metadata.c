/**
 * @file test_encode_metadata.c
 * @brief Metadata passthrough (docs/encoder-api.md phase 3): raw EXIF / XMP retention, the orientation field, the
 *        writers of every encoder, img2_image_set_metadata(), and --keep-metadata
 *
 * Fixtures: jpg/DSCN0010.jpg (EXIF with GPS + XMP, orientation 1) and jpg/canon_hdr_NO.jpg (EXIF with GPS + XMP,
 * orientation 6), decoded without deblocking and scaled down (scaling copies the raw blocks). Per writer: encode
 * with metadata, decode, compare the raw blocks byte for byte and the parsed fields; metadata == 0 gives the bytes
 * of an image without blocks. Orientation: un-rotated pixels keep their orientation (JPEG/PNG/WebP: the EXIF tag;
 * JXL/HEIF/AVIF: also the codestream orientation / irot, so their decoders return upright pixels), and after
 * img2_apply_orientation() the written tag is 1. Limits: the 1 MiB decode cap (PNG eXIf / iTXt), the 65533-byte
 * JPEG APP1 limit, the IFD0 orientation patch (both byte orders, absent / malformed tag).
 */

#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <img2/img2.h>
#ifdef IMG2_HAVE_JXL
#include <jxl/encode.h>
#endif
#ifdef IMG2_HAVE_WEBP
#include <webp/encode.h>
#include <webp/mux.h>
#endif

#include "../../imgcat2/cli.h"
#include "../../imgcat2/pipeline.h"
#include "../../imgcat2/terminal/file.h"
#include "../../libimg2/core/image.h"
#include "../../libimg2/encoders/encoder.h"
#include "../../libimg2/metadata/meta_raw.h"
#include "../ctest.h"
#include "../test_tmpdir.h"

#ifndef IMG2_TEST_SRC_DIR
#error "IMG2_TEST_SRC_DIR must be defined by the build system"
#endif

/* ---- log capture ---------------------------------------------------------------------------------------- */

static char s_warn[8][256];
static int s_warn_count;

static void capture_log(img2_log_level_t level, const char *msg, void *user)
{
	(void)user;
	if (level == IMG2_LOG_WARN) {
		if (s_warn_count < 8) {
			snprintf(s_warn[s_warn_count], sizeof(s_warn[0]), "%s", msg);
		}
		s_warn_count++;
	}
}

static void capture_begin(void)
{
	s_warn_count = 0;
	img2_set_log_callback(capture_log, NULL);
	img2_set_log_level(IMG2_LOG_WARN);
}

static void capture_end(void)
{
	img2_set_log_callback(NULL, NULL);
	img2_set_log_level(IMG2_LOG_ERROR);
}

/* ---- helpers -------------------------------------------------------------------------------------------- */

static uint8_t *load(const char *rel, size_t *len)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/data/%s", IMG2_TEST_SRC_DIR, rel);
	FILE *f = fopen(path, "rb");
	ASSERT_NOT_NULL(f);
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *buf = malloc((size_t)n);
	ASSERT_NOT_NULL(buf);
	ASSERT_EQUAL((intmax_t)n, (intmax_t)fread(buf, 1, (size_t)n, f));
	fclose(f);
	*len = (size_t)n;
	return buf;
}

static img2_frames_t decode_buf(const uint8_t *buf, size_t len)
{
	img2_decode_opts_t dopts = IMG2_DECODE_OPTS_INIT;
	dopts.jpeg_deblock = 0;
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, img2_decode(buf, len, &dopts, &fr));
	ASSERT_EQUAL(1, fr.count);
	return fr;
}

/* decoded fixture scaled to w x h (img2_scale copies the raw blocks and the orientation); *parsed: the source's
 * parsed EXIF / XMP (owned by the returned frames) */
static img2_image_t *fixture(const char *rel, uint32_t w, uint32_t h, img2_frames_t *src)
{
	size_t len = 0;
	uint8_t *buf = load(rel, &len);
	*src = decode_buf(buf, len);
	free(buf);
	img2_image_t *out = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_scale(src->frames[0], w, h, IMG2_FILTER_DEFAULT, &out));
	ASSERT_NOT_NULL(out->exif_raw);
	ASSERT_NOT_NULL(out->xmp_raw);
	return out;
}

static img2_status_t encode(img2_image_t *img, img2_format_t fmt, unsigned metadata, bool use_opts, uint8_t **buf, size_t *len)
{
	img2_frames_t in = { &img, 1, 0 };
	img2_encode_opts_t eo = IMG2_ENCODE_OPTS_INIT;
	eo.metadata = metadata;
	return img2_encode(&in, fmt, use_opts ? &eo : NULL, buf, len);
}

static img2_frames_t encode_decode(img2_image_t *img, img2_format_t fmt, unsigned metadata)
{
	uint8_t *buf = NULL;
	size_t len = 0;
	ASSERT_EQUAL(IMG2_OK, encode(img, fmt, metadata, true, &buf, &len));
	img2_frames_t fr = decode_buf(buf, len);
	img2_free(buf);
	return fr;
}

static const img2_format_t k_formats[] = { IMG2_FORMAT_JPEG, IMG2_FORMAT_PNG, IMG2_FORMAT_WEBP, IMG2_FORMAT_JXL, IMG2_FORMAT_HEIF, IMG2_FORMAT_AVIF };
#define N_FORMATS (sizeof(k_formats) / sizeof(k_formats[0]))

/* the formats whose readers take the orientation from their own header (codestream / irot), not from EXIF */
static bool self_orienting(img2_format_t f)
{
	return f == IMG2_FORMAT_JXL || f == IMG2_FORMAT_HEIF || f == IMG2_FORMAT_AVIF;
}

static void assert_same_block(const void *a, size_t la, const void *b, size_t lb)
{
	ASSERT_NOT_NULL(a);
	ASSERT_NOT_NULL(b);
	ASSERT_DATA((const unsigned char *)a, la, (const unsigned char *)b, lb);
}

static void assert_same_parsed(const img2_image_t *src, const img2_image_t *dec)
{
	ASSERT_NOT_NULL(dec->exif);
	ASSERT_STR(src->exif->make, dec->exif->make);
	ASSERT_STR(src->exif->model, dec->exif->model);
	ASSERT_STR(src->exif->date_time, dec->exif->date_time);
	ASSERT_TRUE(src->exif->has_gps);
	ASSERT_TRUE(dec->exif->has_gps);
	ASSERT_DBL_NEAR(src->exif->gps_latitude, dec->exif->gps_latitude);
	ASSERT_DBL_NEAR(src->exif->gps_longitude, dec->exif->gps_longitude);
	/* the packet of k_xmp (set on the image by the test) */
	ASSERT_NOT_NULL(dec->xmp);
	ASSERT_STR("img2 test", dec->xmp->creator);
	ASSERT_STR("Passthrough", dec->xmp->title);
	ASSERT_EQUAL(4, dec->xmp->rating);
	ASSERT_NOT_NULL(dec->xmp->keywords);
	ASSERT_STR("one, two", dec->xmp->keywords);
}

/* an XMP packet with fields the parser reads (the fixtures' packets use structures it skips) */
static const char k_xmp[] = "<?xpacket begin='' id='W5M0MpCehiHzreSzNTczkc9d'?>\n"
                            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
                            "<rdf:Description xmlns:dc='http://purl.org/dc/elements/1.1/' xmlns:xmp='http://ns.adobe.com/xap/1.0/'>"
                            "<dc:creator>img2 test</dc:creator><dc:title>Passthrough</dc:title><xmp:Rating>4</xmp:Rating>"
                            "<dc:subject><rdf:Bag><rdf:li>one</rdf:li><rdf:li>two</rdf:li></rdf:Bag></dc:subject>"
                            "</rdf:Description></rdf:RDF>\n<?xpacket end='w'?>";

/* ---- writers -------------------------------------------------------------------------------------------- */

CTEST(encode_metadata, writers_round_trip)
{
	img2_frames_t src;
	img2_image_t *img = fixture("jpg/DSCN0010.jpg", 64, 48, &src);
	ASSERT_EQUAL(1, img->orientation);
	/* the file's own XMP is compared byte for byte in the XMP-only pass; the parsed fields with k_xmp */
	const size_t file_xmp_len = img->xmp_raw_len;
	char *file_xmp = malloc(file_xmp_len);
	ASSERT_NOT_NULL(file_xmp);
	memcpy(file_xmp, img->xmp_raw, file_xmp_len);
	ASSERT_EQUAL(IMG2_OK, img2_image_set_metadata(img, img->exif_raw, img->exif_raw_len, k_xmp, sizeof(k_xmp) - 1));

	for (size_t i = 0; i < N_FORMATS; i++) {
		if (!img2_can_encode(k_formats[i])) {
			continue;
		}
		capture_begin();
		img2_frames_t dec = encode_decode(img, k_formats[i], IMG2_META_EXIF | IMG2_META_XMP);
		capture_end();
		ASSERT_EQUAL(0, s_warn_count);
		const img2_image_t *d = dec.frames[0];
		assert_same_block(img->exif_raw, img->exif_raw_len, d->exif_raw, d->exif_raw_len);
		assert_same_block(img->xmp_raw, img->xmp_raw_len, d->xmp_raw, d->xmp_raw_len);
		ASSERT_EQUAL(0, d->xmp_raw[d->xmp_raw_len]);
		assert_same_parsed(src.frames[0], d);
		ASSERT_EQUAL(64, d->width);
		ASSERT_EQUAL(1, d->orientation);
		img2_frames_free(&dec);

		/* one bit at a time */
		dec = encode_decode(img, k_formats[i], IMG2_META_EXIF);
		ASSERT_NOT_NULL(dec.frames[0]->exif_raw);
		ASSERT_NULL(dec.frames[0]->xmp_raw);
		img2_frames_free(&dec);
		ASSERT_EQUAL(IMG2_OK, img2_image_set_metadata(img, img->exif_raw, img->exif_raw_len, file_xmp, file_xmp_len));
		dec = encode_decode(img, k_formats[i], IMG2_META_XMP);
		ASSERT_NULL(dec.frames[0]->exif_raw);
		assert_same_block(file_xmp, file_xmp_len, dec.frames[0]->xmp_raw, dec.frames[0]->xmp_raw_len);
		img2_frames_free(&dec);
		ASSERT_EQUAL(IMG2_OK, img2_image_set_metadata(img, img->exif_raw, img->exif_raw_len, k_xmp, sizeof(k_xmp) - 1));
	}
	free(file_xmp);
	img2_image_destroy(img);
	img2_frames_free(&src);
}

/* metadata == 0 (and NULL options) give exactly the bytes of the same pixels without any metadata */
CTEST(encode_metadata, no_metadata_same_bytes)
{
	img2_frames_t src;
	img2_image_t *img = fixture("jpg/DSCN0010.jpg", 40, 30, &src);
	img2_image_t *bare = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_create(40, 30, &bare));
	memcpy(bare->pixels, img->pixels, (size_t)40 * 30 * 4);

	for (size_t i = 0; i < N_FORMATS; i++) {
		if (!img2_can_encode(k_formats[i])) {
			continue;
		}
		uint8_t *a = NULL, *b = NULL, *c = NULL;
		size_t la = 0, lb = 0, lc = 0;
		ASSERT_EQUAL(IMG2_OK, encode(img, k_formats[i], 0, true, &a, &la));
		ASSERT_EQUAL(IMG2_OK, encode(img, k_formats[i], 0, false, &b, &lb));
		ASSERT_EQUAL(IMG2_OK, encode(bare, k_formats[i], IMG2_META_EXIF | IMG2_META_XMP, true, &c, &lc));
		ASSERT_DATA(a, la, b, lb);
		ASSERT_DATA(a, la, c, lc); /* metadata requested, none present: the default bytes */
		img2_free(a);
		img2_free(b);
		img2_free(c);
	}
	img2_image_destroy(bare);
	img2_image_destroy(img);
	img2_frames_free(&src);
}

/* ---- orientation ---------------------------------------------------------------------------------------- */

/* red 16x16 block at the top-left corner of the stored pixels, blue elsewhere */
static void mark_corner(img2_image_t *img)
{
	for (uint32_t y = 0; y < img->height; y++) {
		for (uint32_t x = 0; x < img->width; x++) {
			const bool red = x < 16 && y < 16;
			img2_image_set_pixel(img, x, y, red ? 255 : 0, 0, red ? 0 : 255, 255);
		}
	}
}

static void assert_red(const img2_image_t *img, uint32_t x, uint32_t y)
{
	const uint8_t *p = img2_image_get_pixel(img, x, y);
	ASSERT_NOT_NULL(p);
	if (!(p[0] > 160 && p[2] < 96)) {
		fprintf(stderr, "  pixel (%u,%u) = %u,%u,%u\n", x, y, p[0], p[1], p[2]);
	}
	ASSERT_TRUE(p[0] > 160 && p[2] < 96);
}

/* the API encodes un-rotated pixels: the file keeps orientation 6 (decoded upright where the reader applies it) */
CTEST(encode_metadata, orientation_unrotated_pixels)
{
	img2_frames_t src;
	img2_image_t *img = fixture("jpg/canon_hdr_NO.jpg", 64, 48, &src);
	ASSERT_EQUAL(6, src.frames[0]->orientation);
	ASSERT_EQUAL(6, img->orientation); /* copied by the scale */
	mark_corner(img);

	for (size_t i = 0; i < N_FORMATS; i++) {
		if (!img2_can_encode(k_formats[i])) {
			continue;
		}
		img2_frames_t dec = encode_decode(img, k_formats[i], IMG2_META_EXIF | IMG2_META_XMP);
		const img2_image_t *d = dec.frames[0];
		ASSERT_EQUAL(6, d->exif->orientation);
		assert_same_block(img->exif_raw, img->exif_raw_len, d->exif_raw, d->exif_raw_len); /* tag already 6 */
		if (self_orienting(k_formats[i])) {
			ASSERT_EQUAL(1, d->orientation); /* the codec applied the codestream orientation / irot */
			ASSERT_EQUAL(48, d->width);
			ASSERT_EQUAL(64, d->height);
			assert_red(d, 40, 8); /* rotated 90 CW: the corner is now top right */
		} else {
			ASSERT_EQUAL(6, d->orientation);
			ASSERT_EQUAL(64, d->width);
			assert_red(d, 4, 4);
		}
		img2_frames_free(&dec);

		/* without the EXIF bit nothing carries the orientation (XMP only): as before, stored pixels */
		dec = encode_decode(img, k_formats[i], IMG2_META_XMP);
		ASSERT_EQUAL(64, dec.frames[0]->width);
		ASSERT_EQUAL(1, dec.frames[0]->orientation);
		img2_frames_free(&dec);
	}
	img2_image_destroy(img);
	img2_frames_free(&src);
}

/* after img2_apply_orientation the written EXIF says 1; everything else in the block is unchanged */
CTEST(encode_metadata, orientation_after_apply)
{
	img2_frames_t src;
	img2_image_t *img = fixture("jpg/canon_hdr_NO.jpg", 64, 48, &src);
	ASSERT_EQUAL(IMG2_OK, img2_apply_orientation(&img));
	ASSERT_EQUAL(1, img->orientation);
	ASSERT_EQUAL(48, img->width);
	ASSERT_NOT_NULL(img->exif_raw); /* the blocks stay */

	uint8_t *want = malloc(img->exif_raw_len);
	ASSERT_NOT_NULL(want);
	memcpy(want, img->exif_raw, img->exif_raw_len);
	ASSERT_TRUE(img2_meta_patch_orientation(want, img->exif_raw_len, 1));

	for (size_t i = 0; i < N_FORMATS; i++) {
		if (!img2_can_encode(k_formats[i])) {
			continue;
		}
		img2_frames_t dec = encode_decode(img, k_formats[i], IMG2_META_EXIF);
		const img2_image_t *d = dec.frames[0];
		ASSERT_EQUAL(1, d->exif->orientation);
		ASSERT_EQUAL(1, d->orientation);
		ASSERT_EQUAL(48, d->width);
		assert_same_block(want, img->exif_raw_len, d->exif_raw, d->exif_raw_len);
		img2_frames_free(&dec);
	}
	free(want);
	/* the image's own block is not modified by the encoders, and the parsed EXIF keeps the file's value */
	img2_exif_t own;
	img2_exif_init(&own);
	ASSERT_EQUAL(IMG2_OK, img2_exif_parse_tiff(&own, img->exif_raw, img->exif_raw_len));
	ASSERT_EQUAL(6, own.orientation);
	ASSERT_EQUAL(6, src.frames[0]->exif->orientation);
	img2_image_destroy(img);
	img2_frames_free(&src);
}

/* CLI: --keep-metadata parsing / validation, and a conversion of the rotated JPEG (pipeline orients: tag 1) */
CTEST(encode_metadata, cli_keep_metadata)
{
	cli_options_t opts;
	memset(&opts, 0, sizeof(opts));
	opts.fps = 15;
	char a0[] = "imgcat2", a1[] = "--keep-metadata", a2[] = "--jpeg", a3[] = "85", a4[] = "x.png";
	char *argv[] = { a0, a1, a2, a3, a4, NULL };
	optind = 0;
	ASSERT_EQUAL(0, parse_arguments(5, argv, &opts));
	ASSERT_TRUE(opts.enc_keep_metadata);
	ASSERT_EQUAL(0, validate_options(&opts));
	opts.convert_mode = false;
	ASSERT_EQUAL(-1, validate_options(&opts)); /* outside a conversion: error */

	/* a small rotated JPEG: canon_hdr_NO.jpg scaled to 64 x 48, its EXIF (Orientation 6) and XMP written back */
	img2_frames_t src;
	img2_image_t *small = fixture("jpg/canon_hdr_NO.jpg", 64, 48, &src);
	uint8_t *jpg = NULL;
	size_t len = 0;
	ASSERT_EQUAL(IMG2_OK, encode(small, IMG2_FORMAT_JPEG, IMG2_META_EXIF | IMG2_META_XMP, true, &jpg, &len));
	img2_image_destroy(small);
	img2_frames_free(&src);

	cli_options_t co;
	memset(&co, 0, sizeof(co));
	co.silent = true;
	co.convert_mode = true;
	co.output_format = IMG2_FORMAT_PNG;
	co.png_compression = 1;
	co.enc_keep_metadata = true;
	char out[512];
	co.output_file = test_tmp_path(out, sizeof(out), "encode_metadata_cli.png");
	img2_image_t **frames = NULL;
	int count = 0;
	ASSERT_EQUAL(0, pipeline_decode(&co, jpg, len, &frames, &count));
	img2_free(jpg);
	ASSERT_EQUAL(1, frames[0]->orientation); /* applied by the pipeline */
	ASSERT_EQUAL(0, file_render(frames, count, &co));
	img2_frames_t fr = { .frames = frames, .count = count };

	uint8_t *png = NULL;
	FILE *f = fopen(out, "rb");
	ASSERT_NOT_NULL(f);
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	png = malloc((size_t)n);
	ASSERT_EQUAL((intmax_t)n, (intmax_t)fread(png, 1, (size_t)n, f));
	fclose(f);
	remove(out);
	img2_frames_t dec = decode_buf(png, (size_t)n);
	free(png);
	ASSERT_EQUAL(48, dec.frames[0]->width);
	ASSERT_EQUAL(64, dec.frames[0]->height);
	ASSERT_EQUAL(1, dec.frames[0]->exif->orientation);
	ASSERT_STR("Canon", dec.frames[0]->exif->make);
	ASSERT_EQUAL(frames[0]->xmp_raw_len, dec.frames[0]->xmp_raw_len);
	img2_frames_free(&dec);
	img2_frames_free(&fr);
}

static size_t tiff_one_entry(uint8_t *t, bool be, uint16_t tag, uint16_t type, uint32_t count, uint16_t value);

/* file_render() of frame `index` to a PNG in the test temp dir, decoded back */
static img2_frames_t render_frame(img2_image_t **frames, int count, int index)
{
	cli_options_t co;
	memset(&co, 0, sizeof(co));
	co.silent = true;
	co.convert_mode = true;
	co.output_format = IMG2_FORMAT_PNG;
	co.png_compression = 1;
	co.enc_keep_metadata = true;
	co.frame_index = index;
	char out[512];
	co.output_file = test_tmp_path(out, sizeof(out), "encode_metadata_frame.png");
	ASSERT_EQUAL(0, file_render(frames, count, &co));
	FILE *f = fopen(out, "rb");
	ASSERT_NOT_NULL(f);
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *png = malloc((size_t)n);
	ASSERT_NOT_NULL(png);
	ASSERT_EQUAL((intmax_t)n, (intmax_t)fread(png, 1, (size_t)n, f));
	fclose(f);
	remove(out);
	img2_frames_t dec = decode_buf(png, (size_t)n);
	free(png);
	return dec;
}

/* --keep-metadata --frame N (N > 0): the file's metadata (on frame 0) is written; frame N keeps its own orientation */
CTEST(encode_metadata, cli_keep_metadata_later_frame)
{
	uint8_t t[32];
	const size_t n = tiff_one_entry(t, false, 0x0112, 3, 1, 1);
	img2_image_t *fr[2] = { NULL, NULL };
	ASSERT_EQUAL(IMG2_OK, img2_image_create(6, 4, &fr[0]));
	ASSERT_EQUAL(IMG2_OK, img2_image_create(6, 4, &fr[1]));
	ASSERT_EQUAL(IMG2_OK, img2_image_set_metadata(fr[0], t, n, k_xmp, sizeof(k_xmp) - 1));
	fr[1]->orientation = 6; /* this frame's pixels still need the rotation: the written tag says so */

	img2_frames_t dec = render_frame(fr, 2, 1);
	ASSERT_NOT_NULL(dec.frames[0]->exif_raw);
	ASSERT_EQUAL(6, dec.frames[0]->exif->orientation);
	assert_same_block(k_xmp, sizeof(k_xmp) - 1, dec.frames[0]->xmp_raw, dec.frames[0]->xmp_raw_len);
	img2_frames_free(&dec);
	ASSERT_EQUAL(6, fr[1]->orientation);
	ASSERT_EQUAL(1, fr[0]->orientation);

	/* a frame with blocks of its own keeps them */
	const size_t n2 = tiff_one_entry(t, true, 0x0112, 3, 1, 1);
	ASSERT_EQUAL(IMG2_OK, img2_image_set_metadata(fr[1], t, n2, NULL, 0));
	dec = render_frame(fr, 2, 1);
	ASSERT_EQUAL('M', dec.frames[0]->exif_raw[0]);
	ASSERT_NULL(dec.frames[0]->xmp_raw);
	img2_frames_free(&dec);

	/* frame 0 without metadata: nothing to copy */
	ASSERT_EQUAL(IMG2_OK, img2_image_set_metadata(fr[0], NULL, 0, NULL, 0));
	ASSERT_EQUAL(IMG2_OK, img2_image_set_metadata(fr[1], NULL, 0, NULL, 0));
	dec = render_frame(fr, 2, 1);
	ASSERT_NULL(dec.frames[0]->exif_raw);
	ASSERT_NULL(dec.frames[0]->xmp_raw);
	img2_frames_free(&dec);
	img2_image_destroy(fr[0]);
	img2_image_destroy(fr[1]);

#ifdef IMG2_HAVE_JXL
	/* end to end: an animated JPEG XL with Exif and xml boxes (decoded metadata on frame 0 only), --frame 1 */
	JxlEncoder *enc = JxlEncoderCreate(NULL);
	ASSERT_NOT_NULL(enc);
	ASSERT_EQUAL(JXL_ENC_SUCCESS, JxlEncoderUseBoxes(enc));
	JxlBasicInfo bi;
	JxlEncoderInitBasicInfo(&bi);
	bi.xsize = 8;
	bi.ysize = 4;
	bi.bits_per_sample = 8;
	bi.num_color_channels = 3;
	bi.uses_original_profile = JXL_TRUE;
	bi.have_animation = JXL_TRUE;
	bi.animation.tps_numerator = 10;
	bi.animation.tps_denominator = 1;
	ASSERT_EQUAL(JXL_ENC_SUCCESS, JxlEncoderSetBasicInfo(enc, &bi));
	JxlColorEncoding ce;
	JxlColorEncodingSetToSRGB(&ce, JXL_FALSE);
	ASSERT_EQUAL(JXL_ENC_SUCCESS, JxlEncoderSetColorEncoding(enc, &ce));
	uint8_t box[4 + 26] = { 0 };
	tiff_one_entry(box + 4, false, 0x0112, 3, 1, 1);
	ASSERT_EQUAL(JXL_ENC_SUCCESS, JxlEncoderAddBox(enc, "Exif", box, sizeof(box), JXL_FALSE));
	ASSERT_EQUAL(JXL_ENC_SUCCESS, JxlEncoderAddBox(enc, "xml ", (const uint8_t *)k_xmp, sizeof(k_xmp) - 1, JXL_FALSE));
	JxlEncoderCloseBoxes(enc);
	JxlEncoderFrameSettings *fs = JxlEncoderFrameSettingsCreate(enc, NULL);
	ASSERT_NOT_NULL(fs);
	ASSERT_EQUAL(JXL_ENC_SUCCESS, JxlEncoderSetFrameLossless(fs, JXL_TRUE));
	uint8_t pix[8 * 4 * 3];
	JxlPixelFormat pf = { 3, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0 };
	for (int i = 0; i < 2; i++) {
		JxlFrameHeader fh;
		JxlEncoderInitFrameHeader(&fh);
		fh.duration = 1;
		ASSERT_EQUAL(JXL_ENC_SUCCESS, JxlEncoderSetFrameHeader(fs, &fh));
		memset(pix, 40 + 100 * i, sizeof(pix));
		ASSERT_EQUAL(JXL_ENC_SUCCESS, JxlEncoderAddImageFrame(fs, &pf, pix, sizeof(pix)));
	}
	JxlEncoderCloseInput(enc);
	uint8_t *jxl = malloc(65536);
	ASSERT_NOT_NULL(jxl);
	uint8_t *next = jxl;
	size_t avail = 65536;
	ASSERT_EQUAL(JXL_ENC_SUCCESS, JxlEncoderProcessOutput(enc, &next, &avail));
	JxlEncoderDestroy(enc);

	cli_options_t co;
	memset(&co, 0, sizeof(co));
	co.silent = true;
	img2_image_t **frames = NULL;
	int count = 0;
	ASSERT_EQUAL(0, pipeline_decode(&co, jxl, (size_t)(next - jxl), &frames, &count));
	free(jxl);
	ASSERT_EQUAL(2, count);
	ASSERT_NOT_NULL(frames[0]->exif_raw);
	ASSERT_NULL(frames[1]->exif_raw); /* the decoder attaches the metadata to frame 0 */
	dec = render_frame(frames, count, 1);
	assert_same_block(box + 4, 26, dec.frames[0]->exif_raw, dec.frames[0]->exif_raw_len);
	assert_same_block(k_xmp, sizeof(k_xmp) - 1, dec.frames[0]->xmp_raw, dec.frames[0]->xmp_raw_len);
	ASSERT_EQUAL(140, dec.frames[0]->pixels[0]); /* frame 1's pixels */
	img2_frames_free(&dec);
	img2_frames_t all = { .frames = frames, .count = count };
	img2_frames_free(&all);
#endif
}

/* ---- the IFD0 orientation patch ------------------------------------------------------------------------- */

/* TIFF block (8-byte header, IFD0 at 8 with one entry tag / type / count / value, no next IFD); 26 bytes */
static size_t tiff_one_entry(uint8_t *t, bool be, uint16_t tag, uint16_t type, uint32_t count, uint16_t value)
{
	memset(t, 0, 26);
	if (be) {
		const uint8_t h[] = { 'M', 'M', 0, 42, 0, 0, 0, 8, 0, 1 };
		memcpy(t, h, sizeof(h));
		t[10] = (uint8_t)(tag >> 8); t[11] = (uint8_t)tag;
		t[12] = (uint8_t)(type >> 8); t[13] = (uint8_t)type;
		t[14] = (uint8_t)(count >> 24); t[15] = (uint8_t)(count >> 16); t[16] = (uint8_t)(count >> 8); t[17] = (uint8_t)count;
		t[18] = (uint8_t)(value >> 8); t[19] = (uint8_t)value;
	} else {
		const uint8_t h[] = { 'I', 'I', 42, 0, 8, 0, 0, 0, 1, 0 };
		memcpy(t, h, sizeof(h));
		t[10] = (uint8_t)tag; t[11] = (uint8_t)(tag >> 8);
		t[12] = (uint8_t)type; t[13] = (uint8_t)(type >> 8);
		t[14] = (uint8_t)count; t[15] = (uint8_t)(count >> 8); t[16] = (uint8_t)(count >> 16); t[17] = (uint8_t)(count >> 24);
		t[18] = (uint8_t)value; t[19] = (uint8_t)(value >> 8);
	}
	return 26;
}

CTEST(encode_metadata, patch_orientation)
{
	uint8_t t[32], before[32];
	for (int be = 0; be < 2; be++) {
		size_t n = tiff_one_entry(t, be, 0x0112, 3, 1, 6);
		ASSERT_TRUE(img2_meta_patch_orientation(t, n, 1));
		ASSERT_EQUAL(1, be ? t[19] : t[18]);
		ASSERT_EQUAL(0, be ? t[18] : t[19]);
		ASSERT_TRUE(img2_meta_patch_orientation(t, n, 0x0208)); /* both bytes in the right order */
		ASSERT_EQUAL(be ? 0x02 : 0x08, t[18]);
		ASSERT_EQUAL(be ? 0x08 : 0x02, t[19]);

		/* not patched (block unchanged): other tag, wrong type, count 2, truncated IFD, IFD offset out of range */
		const struct {
			uint16_t tag, type;
			uint32_t count;
		} bad[] = { { 0x010F, 3, 1 }, { 0x0112, 4, 1 }, { 0x0112, 3, 2 } };
		for (size_t i = 0; i < 3; i++) {
			n = tiff_one_entry(t, be, bad[i].tag, bad[i].type, bad[i].count, 6);
			memcpy(before, t, n);
			ASSERT_FALSE(img2_meta_patch_orientation(t, n, 1));
			ASSERT_DATA(before, n, t, n);
		}
		n = tiff_one_entry(t, be, 0x0112, 3, 1, 6);
		ASSERT_FALSE(img2_meta_patch_orientation(t, 21, 1)); /* entry cut short */
		ASSERT_FALSE(img2_meta_patch_orientation(t, 9, 1));  /* no room for the entry count */
		t[be ? 7 : 4] = 200;                                   /* IFD0 beyond the block */
		ASSERT_FALSE(img2_meta_patch_orientation(t, n, 1));
	}
	ASSERT_FALSE(img2_meta_patch_orientation((uint8_t *)"Exif\0\0II*\0\x08\0\0\0", 14, 1));
	ASSERT_FALSE(img2_meta_is_tiff(NULL, 8));
	ASSERT_FALSE(img2_meta_is_tiff((const uint8_t *)"II*\0\x08\0\0", 7));
	ASSERT_TRUE(img2_meta_is_tiff((const uint8_t *)"II*\0\x08\0\0\0", 8));
	ASSERT_TRUE(img2_meta_is_tiff((const uint8_t *)"MM\0*\0\0\0\x08", 8));
	/* each byte of both headers */
	static const char *const not_tiff[] = { "IM*\0\x08\0\0\0", "II+\0\x08\0\0\0", "II*\1\x08\0\0\0", "MI\0*\0\0\0\x08", "MM\1*\0\0\0\x08", "MM\0+\0\0\0\x08", "XX\0*\0\0\0\x08" };
	for (size_t i = 0; i < sizeof(not_tiff) / sizeof(not_tiff[0]); i++) {
		ASSERT_FALSE(img2_meta_is_tiff((const uint8_t *)not_tiff[i], 8));
	}
	ASSERT_EQUAL(1, img2_meta_orientation(NULL));
}

/* inconsistent public fields (a non-NULL block of length 0, a claimed length over the cap, orientation 9), and the
 * AVIF orientation together with the lossless encoding options */
CTEST(encode_metadata, field_edge_cases)
{
	img2_image_t *img = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_create(16, 16, &img));
	img->exif_raw = malloc(1);
	img->xmp_raw = malloc(1);
	ASSERT_NOT_NULL(img->exif_raw);
	ASSERT_NOT_NULL(img->xmp_raw);

	/* length 0: nothing to copy, write or encode */
	img2_image_t *s = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_scale(img, 4, 4, IMG2_FILTER_DEFAULT, &s));
	ASSERT_NULL(s->exif_raw);
	ASSERT_NULL(s->xmp_raw);
	img2_image_destroy(s);
	uint8_t *out = (uint8_t *)1;
	size_t len = 1;
	ASSERT_EQUAL(IMG2_OK, img2_meta_exif_for_write(img, &out, &len));
	ASSERT_NULL(out);
	img2_encode_opts_t eo = IMG2_ENCODE_OPTS_INIT;
	eo.metadata = IMG2_META_EXIF | IMG2_META_XMP;
	img2_encode_meta_t m;
	img->orientation = 9;
	capture_begin();
	ASSERT_EQUAL(IMG2_OK, img2_encode_meta_prepare(img, &eo, &m));
	capture_end();
	ASSERT_EQUAL(1, s_warn_count); /* the length-0 EXIF block is not TIFF */
	ASSERT_NULL(m.exif);
	ASSERT_NULL(m.xmp);
	ASSERT_EQUAL(1, m.orientation); /* 9 is not an orientation */
	img2_encode_meta_release(&m);

	/* a TIFF header with a claimed length over the cap (never read past the header) */
	free(img->exif_raw);
	img->exif_raw = malloc(8);
	ASSERT_NOT_NULL(img->exif_raw);
	memcpy(img->exif_raw, "II*\0\x08\0\0\0", 8);
	img->exif_raw_len = IMG2_MAX_METADATA_SIZE + 1;
	capture_begin();
	ASSERT_EQUAL(IMG2_OK, img2_encode_meta_prepare(img, &eo, &m));
	capture_end();
	ASSERT_EQUAL(1, s_warn_count);
	ASSERT_NULL(m.exif);
	img2_encode_meta_release(&m);
	img2_image_destroy(img);

#ifdef IMG2_HAVE_HEIF
	/* lossless AVIF already passes encoding options (the identity NCLX): the orientation is added to them */
	if (img2_can_encode(IMG2_FORMAT_AVIF)) {
		ASSERT_EQUAL(IMG2_OK, img2_image_create(32, 16, &img));
		mark_corner(img);
		uint8_t t[32];
		const size_t n = tiff_one_entry(t, false, 0x0112, 3, 1, 1);
		ASSERT_EQUAL(IMG2_OK, img2_image_set_metadata(img, t, n, NULL, 0));
		img->orientation = 6;
		eo.lossless = true;
		img2_frames_t in = { &img, 1, 0 };
		uint8_t *buf = NULL;
		ASSERT_EQUAL(IMG2_OK, img2_encode(&in, IMG2_FORMAT_AVIF, &eo, &buf, &len));
		img2_frames_t dec = decode_buf(buf, len);
		img2_free(buf);
		ASSERT_EQUAL(16, dec.frames[0]->width); /* irot applied by libheif */
		ASSERT_EQUAL(32, dec.frames[0]->height);
		ASSERT_EQUAL(6, dec.frames[0]->exif->orientation);
		const uint8_t *p = img2_image_get_pixel(dec.frames[0], 15, 0); /* the stored top-left, now top-right */
		ASSERT_EQUAL(255, p[0]);
		ASSERT_EQUAL(0, p[2]);
		img2_frames_free(&dec);
		img2_image_destroy(img);
	}
#endif
}

/* an XMP APP1 segment with nothing after its identifier: no XMP block */
CTEST(encode_metadata, jpeg_empty_xmp_segment)
{
	if (!img2_can_encode(IMG2_FORMAT_JPEG)) {
		return;
	}
	img2_image_t *img = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_create(8, 8, &img));
	uint8_t *jpg = NULL;
	size_t jlen = 0;
	ASSERT_EQUAL(IMG2_OK, encode(img, IMG2_FORMAT_JPEG, 0, false, &jpg, &jlen));
	img2_image_destroy(img);
	static const char id[] = "http://ns.adobe.com/xap/1.0/";
	uint8_t *out = malloc(jlen + 4 + sizeof(id));
	ASSERT_NOT_NULL(out);
	memcpy(out, jpg, 2);
	out[2] = 0xFF;
	out[3] = 0xE1;
	out[4] = 0;
	out[5] = (uint8_t)(2 + sizeof(id)); /* length field + identifier with its NUL */
	memcpy(out + 6, id, sizeof(id));
	memcpy(out + 6 + sizeof(id), jpg + 2, jlen - 2);
	img2_frames_t dec = decode_buf(out, jlen + 4 + sizeof(id));
	ASSERT_NULL(dec.frames[0]->xmp_raw);
	img2_frames_free(&dec);
	free(out);
	img2_free(jpg);
}

/* the encoder's copy: orientation outside 1..8 is written as 1; a block without the tag is copied unchanged; a big-
 * endian block round-trips through a writer */
CTEST(encode_metadata, exif_for_write)
{
	img2_image_t *img = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_create(8, 8, &img));
	uint8_t t[32];
	size_t n = tiff_one_entry(t, true, 0x0112, 3, 1, 6);
	ASSERT_EQUAL(IMG2_OK, img2_image_set_metadata(img, t, n, NULL, 0));

	const uint8_t values[] = { 0, 9, 3 };
	const uint8_t want[] = { 1, 1, 3 };
	for (size_t i = 0; i < 3; i++) {
		img->orientation = values[i];
		uint8_t *out = NULL;
		size_t len = 0;
		ASSERT_EQUAL(IMG2_OK, img2_meta_exif_for_write(img, &out, &len));
		ASSERT_EQUAL(n, len);
		ASSERT_EQUAL(want[i], out[19]);
		free(out);
	}
	ASSERT_EQUAL(6, img->exif_raw[19]); /* the image's block is never patched */

	/* JPEG writer, big-endian block, orientation 1 */
	img->orientation = 1;
	img2_frames_t dec = encode_decode(img, IMG2_FORMAT_JPEG, IMG2_META_EXIF);
	ASSERT_EQUAL(1, dec.frames[0]->exif->orientation);
	ASSERT_EQUAL('M', dec.frames[0]->exif_raw[0]);
	img2_frames_free(&dec);

	/* a block without an Orientation tag is written as is */
	n = tiff_one_entry(t, false, 0x010F, 2, 1, 0);
	ASSERT_EQUAL(IMG2_OK, img2_image_set_metadata(img, t, n, NULL, 0));
	img->orientation = 6;
	uint8_t *out = NULL;
	size_t len = 0;
	ASSERT_EQUAL(IMG2_OK, img2_meta_exif_for_write(img, &out, &len));
	ASSERT_DATA(t, n, out, len);
	free(out);

	/* no block: nothing */
	ASSERT_EQUAL(IMG2_OK, img2_image_set_metadata(img, NULL, 0, NULL, 0));
	out = (uint8_t *)1;
	ASSERT_EQUAL(IMG2_OK, img2_meta_exif_for_write(img, &out, &len));
	ASSERT_NULL(out);
	ASSERT_EQUAL(0, len);
	img2_image_destroy(img);
}

/* a block put straight into the public fields that is not TIFF or too big is not written (one WARN) */
CTEST(encode_metadata, invalid_public_fields_not_written)
{
	img2_image_t *img = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_create(8, 8, &img));
	img->exif_raw = malloc(16);
	ASSERT_NOT_NULL(img->exif_raw);
	memcpy(img->exif_raw, "Exif\0\0II*\0\x08\0\0\0\0\0", 16);
	img->exif_raw_len = 16;
	img->xmp_raw = calloc(1, 4);
	ASSERT_NOT_NULL(img->xmp_raw);
	img->xmp_raw_len = IMG2_MAX_METADATA_SIZE + 1; /* claimed length only: never read */

	for (size_t i = 0; i < N_FORMATS; i++) {
		if (!img2_can_encode(k_formats[i])) {
			continue;
		}
		capture_begin();
		img2_frames_t dec = encode_decode(img, k_formats[i], IMG2_META_EXIF | IMG2_META_XMP);
		capture_end();
		ASSERT_EQUAL(2, s_warn_count);
		ASSERT_STR("encode: the image's EXIF block is not a TIFF-header block of at most 1 MiB (not written)", s_warn[0]);
		ASSERT_STR("encode: the image's XMP packet exceeds 1 MiB (not written)", s_warn[1]);
		ASSERT_NULL(dec.frames[0]->exif_raw);
		ASSERT_NULL(dec.frames[0]->xmp_raw);
		img2_frames_free(&dec);
	}
	img2_image_destroy(img); /* frees the caller-set blocks */
}

/* ---- JPEG APP1 limit ------------------------------------------------------------------------------------ */

/* a TIFF block of n bytes (IFD0 with no entries, zero padding) */
static uint8_t *tiff_block(size_t n)
{
	uint8_t *t = calloc(1, n);
	ASSERT_NOT_NULL(t);
	memcpy(t, "II*\0\x08\0\0\0", 8);
	return t;
}

/* an XMP packet of n bytes (minimal packet, then spaces) */
static char *xmp_packet(size_t n)
{
	static const char head[] = "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
	                           "<rdf:Description xmlns:dc='http://purl.org/dc/elements/1.1/'><dc:title>T</dc:title></rdf:Description>"
	                           "</rdf:RDF>";
	char *x = malloc(n);
	ASSERT_NOT_NULL(x);
	memset(x, ' ', n);
	memcpy(x, head, sizeof(head) - 1);
	return x;
}

CTEST(encode_metadata, jpeg_marker_limit)
{
	if (!img2_can_encode(IMG2_FORMAT_JPEG)) {
		return;
	}
	img2_image_t *img = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_create(16, 16, &img));
	const size_t exif_sizes[] = { 65527, 65528 };
	const size_t xmp_sizes[] = { 65504, 65505 };
	for (int over = 0; over < 2; over++) {
		uint8_t *t = tiff_block(exif_sizes[over]);
		char *x = xmp_packet(xmp_sizes[over]);
		ASSERT_EQUAL(IMG2_OK, img2_image_set_metadata(img, t, exif_sizes[over], x, xmp_sizes[over]));
		capture_begin();
		img2_frames_t dec = encode_decode(img, IMG2_FORMAT_JPEG, IMG2_META_EXIF | IMG2_META_XMP);
		capture_end();
		if (over) {
			ASSERT_EQUAL(2, s_warn_count);
			ASSERT_STR("encode: EXIF block of 65528 bytes exceeds the JPEG APP1 limit (65533 bytes with its identifier), not written", s_warn[0]);
			ASSERT_STR("encode: XMP packet of 65505 bytes exceeds the JPEG APP1 limit (65533 bytes with its identifier), not written", s_warn[1]);
			ASSERT_NULL(dec.frames[0]->exif_raw);
			ASSERT_NULL(dec.frames[0]->xmp_raw);
		} else {
			ASSERT_EQUAL(0, s_warn_count);
			assert_same_block(t, exif_sizes[0], dec.frames[0]->exif_raw, dec.frames[0]->exif_raw_len);
			assert_same_block(x, xmp_sizes[0], dec.frames[0]->xmp_raw, dec.frames[0]->xmp_raw_len);
		}
		img2_frames_free(&dec);
		free(t);
		free(x);
	}
	img2_image_destroy(img);
}

/* ---- decode cap ----------------------------------------------------------------------------------------- */

static uint32_t crc_table[256];

static uint32_t png_crc(const uint8_t *p, size_t n)
{
	if (crc_table[1] == 0) {
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++) {
				c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			}
			crc_table[i] = c;
		}
	}
	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < n; i++) {
		c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
	}
	return c ^ 0xFFFFFFFFu;
}

static void put_be32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

/* 4 x 4 PNG with a chunk (type, payload) inserted after IHDR; free() */
static uint8_t *png_with_chunk(const char *type, const uint8_t *payload, size_t n, size_t *len)
{
	img2_image_t *img = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_create(4, 4, &img));
	uint8_t *png = NULL;
	size_t plen = 0;
	ASSERT_EQUAL(IMG2_OK, encode(img, IMG2_FORMAT_PNG, 0, false, &png, &plen));
	img2_image_destroy(img);
	uint8_t *out = malloc(plen + n + 12);
	ASSERT_NOT_NULL(out);
	memcpy(out, png, 33); /* signature + IHDR */
	put_be32(out + 33, (uint32_t)n);
	memcpy(out + 37, type, 4);
	memcpy(out + 41, payload, n);
	put_be32(out + 41 + n, png_crc(out + 37, n + 4));
	memcpy(out + 45 + n, png + 33, plen - 33);
	*len = plen + n + 12;
	img2_free(png);
	return out;
}

CTEST(encode_metadata, decode_cap)
{
	/* EXIF: exactly 1 MiB is kept, one byte more is not (WARN); the parsed struct is there either way */
	for (int over = 0; over < 2; over++) {
		const size_t n = IMG2_MAX_METADATA_SIZE + (size_t)over;
		uint8_t *t = tiff_block(n);
		size_t len = 0;
		uint8_t *png = png_with_chunk("eXIf", t, n, &len);
		capture_begin();
		img2_frames_t dec = decode_buf(png, len);
		capture_end();
		ASSERT_NOT_NULL(dec.frames[0]->exif);
		if (over) {
			ASSERT_EQUAL(1, s_warn_count);
			ASSERT_STR("PNG: EXIF block of 1048577 bytes exceeds the 1 MiB metadata cap (raw block not kept)", s_warn[0]);
			ASSERT_NULL(dec.frames[0]->exif_raw);
		} else {
			ASSERT_EQUAL(0, s_warn_count);
			assert_same_block(t, n, dec.frames[0]->exif_raw, dec.frames[0]->exif_raw_len);
		}
		img2_frames_free(&dec);
		free(png);
		free(t);
	}

	/* XMP (iTXt, uncompressed): over the cap -> not kept (WARN), the parsed struct still read */
	const size_t n = IMG2_MAX_METADATA_SIZE + 1;
	char *x = xmp_packet(n);
	static const char key[] = "XML:com.adobe.xmp\0\0\0\0\0"; /* keyword, NUL, compression flag + method, lang NUL, key NUL */
	uint8_t *itxt = malloc(sizeof(key) - 1 + n);
	ASSERT_NOT_NULL(itxt);
	memcpy(itxt, key, sizeof(key) - 1);
	memcpy(itxt + sizeof(key) - 1, x, n);
	size_t len = 0;
	uint8_t *png = png_with_chunk("iTXt", itxt, sizeof(key) - 1 + n, &len);
	capture_begin();
	img2_frames_t dec = decode_buf(png, len);
	capture_end();
	ASSERT_EQUAL(1, s_warn_count);
	ASSERT_STR("PNG: XMP packet of 1048577 bytes exceeds the 1 MiB metadata cap (raw block not kept)", s_warn[0]);
	ASSERT_NULL(dec.frames[0]->xmp_raw);
	ASSERT_NOT_NULL(dec.frames[0]->xmp);
	ASSERT_STR("T", dec.frames[0]->xmp->title);
	img2_frames_free(&dec);
	free(png);
	free(itxt);
	free(x);

	/* a block that is not in TIFF-header form is not kept (silently) */
	uint8_t junk[16] = "not a TIFF block";
	png = png_with_chunk("eXIf", junk, sizeof(junk), &len);
	capture_begin();
	dec = decode_buf(png, len);
	capture_end();
	ASSERT_NULL(dec.frames[0]->exif_raw);
	img2_frames_free(&dec);
	free(png);
}

/* ---- decoder details ------------------------------------------------------------------------------------ */

#ifdef IMG2_HAVE_WEBP
/* WebP EXIF chunk with the JPEG-style "Exif\0\0" prefix: the raw block is the TIFF part */
CTEST(encode_metadata, webp_exif_prefix_stripped)
{
	img2_image_t *img = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_create(4, 4, &img));
	uint8_t *webp = NULL;
	size_t wlen = 0;
	ASSERT_EQUAL(IMG2_OK, encode(img, IMG2_FORMAT_WEBP, 0, false, &webp, &wlen));
	img2_image_destroy(img);

	uint8_t chunk[6 + 26];
	memcpy(chunk, "Exif\0\0", 6);
	tiff_one_entry(chunk + 6, false, 0x0112, 3, 1, 3);
	WebPData image = { webp, wlen };
	WebPMux *mux = WebPMuxCreate(&image, 0);
	ASSERT_NOT_NULL(mux);
	WebPData exif = { chunk, sizeof(chunk) };
	ASSERT_EQUAL(WEBP_MUX_OK, WebPMuxSetChunk(mux, "EXIF", &exif, 0));
	WebPData out;
	WebPDataInit(&out);
	ASSERT_EQUAL(WEBP_MUX_OK, WebPMuxAssemble(mux, &out));
	WebPMuxDelete(mux);
	img2_free(webp);

	img2_frames_t dec = decode_buf(out.bytes, out.size);
	assert_same_block(chunk + 6, 26, dec.frames[0]->exif_raw, dec.frames[0]->exif_raw_len);
	ASSERT_EQUAL(3, dec.frames[0]->orientation); /* libwebp does not orient */
	img2_frames_free(&dec);
	WebPDataClear(&out);
}
#endif

/* ---- setter / copy -------------------------------------------------------------------------------------- */

CTEST(encode_metadata, set_metadata)
{
	img2_image_t *img = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_create(4, 4, &img));
	ASSERT_EQUAL(1, img->orientation);
	ASSERT_NULL(img->exif_raw);
	ASSERT_NULL(img->xmp_raw);

	uint8_t t[32];
	const size_t n = tiff_one_entry(t, false, 0x0112, 3, 1, 8);
	const char xmp[] = "<x/>";
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_set_metadata(NULL, t, n, xmp, 4));
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_set_metadata(img, (const uint8_t *)"Exif\0\0II*\0", 10, NULL, 0));
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_set_metadata(img, t, 7, NULL, 0)); /* shorter than a TIFF header */
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_set_metadata(img, NULL, 0, xmp, 0));
	ASSERT_EQUAL(IMG2_E_LIMIT, img2_image_set_metadata(img, t, IMG2_MAX_METADATA_SIZE + 1, NULL, 0));
	ASSERT_EQUAL(IMG2_E_LIMIT, img2_image_set_metadata(img, NULL, 0, xmp, IMG2_MAX_METADATA_SIZE + 1));
	ASSERT_NULL(img->exif_raw);

	ASSERT_EQUAL(IMG2_OK, img2_image_set_metadata(img, t, n, xmp, 4));
	assert_same_block(t, n, img->exif_raw, img->exif_raw_len);
	assert_same_block(xmp, 4, img->xmp_raw, img->xmp_raw_len);
	ASSERT_EQUAL(0, img->xmp_raw[4]);
	ASSERT_EQUAL(1, img->orientation); /* not derived from the block */
	ASSERT_NULL(img->exif);            /* the parsed structs are not touched */

	/* an error leaves the image unchanged */
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_set_metadata(img, (const uint8_t *)"junkjunk", 8, NULL, 0));
	assert_same_block(t, n, img->exif_raw, img->exif_raw_len);

	/* the arguments may be the image's own blocks */
	ASSERT_EQUAL(IMG2_OK, img2_image_set_metadata(img, img->exif_raw, img->exif_raw_len, img->xmp_raw, img->xmp_raw_len));
	assert_same_block(t, n, img->exif_raw, img->exif_raw_len);
	assert_same_block(xmp, 4, img->xmp_raw, img->xmp_raw_len);

	/* NULL clears each block */
	ASSERT_EQUAL(IMG2_OK, img2_image_set_metadata(img, NULL, 123, img->xmp_raw, img->xmp_raw_len));
	ASSERT_NULL(img->exif_raw);
	ASSERT_EQUAL(0, img->exif_raw_len);
	ASSERT_NOT_NULL(img->xmp_raw);
	ASSERT_EQUAL(IMG2_OK, img2_image_set_metadata(img, NULL, 0, NULL, 0));
	ASSERT_NULL(img->xmp_raw);
	ASSERT_EQUAL(0, img->xmp_raw_len);
	img2_image_destroy(img);
}

CTEST(encode_metadata, scale_and_orient_keep_blocks)
{
	img2_image_t *img = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_create(6, 4, &img));
	uint8_t t[32];
	const size_t n = tiff_one_entry(t, true, 0x0112, 3, 1, 8);
	ASSERT_EQUAL(IMG2_OK, img2_image_set_metadata(img, t, n, "<x/>", 4));
	img->orientation = 8;

	img2_image_t *s = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_scale(img, 3, 2, IMG2_FILTER_BOX, &s));
	assert_same_block(t, n, s->exif_raw, s->exif_raw_len);
	assert_same_block("<x/>", 4, s->xmp_raw, s->xmp_raw_len);
	ASSERT_EQUAL(8, s->orientation);
	img2_image_destroy(s);
	ASSERT_EQUAL(IMG2_OK, img2_scale_fit(img, 3, 3, IMG2_FILTER_DEFAULT, &s));
	assert_same_block(t, n, s->exif_raw, s->exif_raw_len);
	ASSERT_EQUAL(8, s->orientation);

	/* img2_apply_orientation: pixels rotated in place, blocks kept, field reset */
	ASSERT_EQUAL(IMG2_OK, img2_apply_orientation(&s));
	ASSERT_EQUAL(1, s->orientation);
	ASSERT_EQUAL(2, s->width);
	assert_same_block(t, n, s->exif_raw, s->exif_raw_len);
	img2_image_destroy(s);

	/* a source without blocks gives a copy without blocks */
	ASSERT_EQUAL(IMG2_OK, img2_image_set_metadata(img, NULL, 0, NULL, 0));
	ASSERT_EQUAL(IMG2_OK, img2_scale(img, 3, 2, IMG2_FILTER_DEFAULT, &s));
	ASSERT_NULL(s->exif_raw);
	ASSERT_NULL(s->xmp_raw);
	img2_image_destroy(s);
	img2_image_destroy(img);
}
