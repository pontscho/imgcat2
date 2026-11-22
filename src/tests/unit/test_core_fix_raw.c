/**
 * @file test_core_fix_raw.c
 * @brief RAW decoder (decoder_raw.c): an empty camera make, a 2-colour (grey + alpha) bitmap preview of a
 *        plain CFA TIFF raw, a Canon CR3 with an H.265 (HEIF capture) preview; minimal files written in-test
 *        (DNG / TIFF with test_cov_b_dng.h, CR3 boxes below)
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <img2/metadata.h>
#include <jpeglib.h>

#include "../../libimg2/decoders/decoder.h"
#include "test_cov_b_util.h"
#include "test_cov_b_dng.h"
#include <libraw/libraw.h>

CTEST(core_fix_raw, unique_camera_model_with_empty_make_is_rejected)
{
	/* No Make / Model: LibRaw copies UniqueCameraModel into make and cuts it at the first space (make before
	 * it, model after it). " X" overwrites the DNGVersion "DNG" fallback with make "": LibRaw does not open a
	 * raw whose make is empty while it identifies the camera */
	cb_buf_t b = { 0 };
	dng_opts_t o = { .preview = PREV_RGB, .ucm = " X" };
	dng_build(&b, &o);
	img2_frames_t fr;
	img2_set_log_callback(cb_log_sink, NULL);
	ASSERT_EQUAL(IMG2_E_CODEC, img2_decode_raw(b.d, b.n, NULL, &fr));
	img2_set_log_callback(NULL, NULL);
	ASSERT_NULL(fr.frames);
	cb_free(&b);
}

CTEST(core_fix_raw, blank_make_tag_leaves_make_empty)
{
	/* Make "   " (no UniqueCameraModel): non-empty while the IFD is parsed (no "DNG" fallback), then LibRaw
	 * strips the trailing spaces */
	cb_buf_t b = { 0 };
	dng_opts_t o = { .preview = PREV_RGB, .no_camera = 1, .make = "   " };
	dng_build(&b, &o);
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, img2_decode_raw(b.d, b.n, NULL, &fr));
	ASSERT_EQUAL(1, fr.count);
	ASSERT_NOT_NULL(fr.frames[0]->exif);
	ASSERT_STR("", fr.frames[0]->exif->make);
	ASSERT_STR("", fr.frames[0]->exif->model);
	img2_frames_free(&fr);
	cb_free(&b);
}

/* ---- minimal Canon CR3 (ISO base media file, big-endian boxes) ---- */

static size_t box_begin(cb_buf_t *b, const char *type)
{
	size_t at = b->n;
	cb_be32(b, 0);
	cb_put(b, type, 4);
	return at;
}

static void box_end(cb_buf_t *b, size_t at)
{
	uint32_t n = (uint32_t)(b->n - at);
	b->d[at] = (uint8_t)(n >> 24);
	b->d[at + 1] = (uint8_t)(n >> 16);
	b->d[at + 2] = (uint8_t)(n >> 8);
	b->d[at + 3] = (uint8_t)n;
}

static void put_zeros(cb_buf_t *b, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		cb_u8(b, 0);
	}
}

#define CR3_W 64
#define CR3_H 48
#define CR3_PREVIEW_LEN 256 /* LibRaw checks for an H.265 preview only above 0xE0 bytes */
#define CR3_MDAT_LEN 512

/**
 * ftyp "crx ", moov/trak (handler "vide", sample description CRAW with a valid CMP1 image header: 64x48,
 * 4 planes, 14 bits, one sample in the mdat), a top-level Canon preview uuid whose PRVW payload starts with
 * 00 00 00 01 "CISZ" (the HEIF-capture H.265 preview signature), mdat
 */
static void cr3_build(cb_buf_t *b)
{
	static const uint8_t uuid_preview[16] = { 0xea, 0xf4, 0x2b, 0x5e, 0x1c, 0x98, 0x4b, 0x88,
	                                          0xb9, 0xfb, 0xb7, 0xdc, 0x40, 0x6e, 0x4d, 0x16 };
	size_t at = box_begin(b, "ftyp");
	cb_str(b, "crx ");
	cb_be32(b, 1);
	cb_str(b, "crx isom");
	box_end(b, at);

	size_t moov = box_begin(b, "moov");
	size_t trak = box_begin(b, "trak");
	size_t mdia = box_begin(b, "mdia");
	at = box_begin(b, "hdlr");
	put_zeros(b, 8);
	cb_str(b, "vide");
	put_zeros(b, 13);
	box_end(b, at);
	size_t minf = box_begin(b, "minf");
	size_t stbl = box_begin(b, "stbl");
	size_t stsd = box_begin(b, "stsd");
	cb_be32(b, 0); /* version, flags */
	cb_be32(b, 1); /* entry count */
	size_t craw = box_begin(b, "CRAW");
	put_zeros(b, 82); /* sample entry fields LibRaw skips */
	at = box_begin(b, "CMP1");
	uint8_t cmp1[48];
	memset(cmp1, 0, sizeof(cmp1));
	cmp1[4] = 0x01;                 /* version 0x100 */
	cmp1[11] = CR3_W;               /* f_width */
	cmp1[15] = CR3_H;               /* f_height */
	cmp1[19] = CR3_W;               /* tile width */
	cmp1[23] = CR3_H;               /* tile height */
	cmp1[24] = 14;                  /* bits */
	cmp1[25] = 0x40;                /* 4 planes, CFA layout 0 */
	cmp1[31] = 1;                   /* mdat header size */
	cb_put(b, cmp1, sizeof(cmp1));
	box_end(b, at);
	box_end(b, craw);
	box_end(b, stsd);
	at = box_begin(b, "stsz");
	cb_be32(b, 0);
	cb_be32(b, CR3_MDAT_LEN); /* fixed sample size */
	cb_be32(b, 1);
	box_end(b, at);
	at = box_begin(b, "co64");
	cb_be32(b, 0);
	cb_be32(b, 1);
	size_t co64_off = b->n;
	cb_be32(b, 0);
	cb_be32(b, 0); /* patched: mdat payload offset */
	box_end(b, at);
	box_end(b, stbl);
	box_end(b, minf);
	box_end(b, mdia);
	box_end(b, trak);
	box_end(b, moov);

	at = box_begin(b, "uuid");
	cb_put(b, uuid_preview, sizeof(uuid_preview));
	uint8_t xdata[32];
	memset(xdata, 0, sizeof(xdata));
	memcpy(xdata + 12, "PRVW", 4);
	xdata[23] = 16; /* width */
	xdata[25] = 12; /* height */
	cb_put(b, xdata, sizeof(xdata));
	uint8_t prev[CR3_PREVIEW_LEN];
	memset(prev, 0, sizeof(prev));
	prev[3] = 1;
	memcpy(prev + 4, "CISZ", 4);
	cb_put(b, prev, sizeof(prev));
	box_end(b, at);

	at = box_begin(b, "mdat");
	uint32_t mdat_off = (uint32_t)b->n;
	put_zeros(b, CR3_MDAT_LEN);
	box_end(b, at);
	b->d[co64_off + 4] = (uint8_t)(mdat_off >> 24);
	b->d[co64_off + 5] = (uint8_t)(mdat_off >> 16);
	b->d[co64_off + 6] = (uint8_t)(mdat_off >> 8);
	b->d[co64_off + 7] = (uint8_t)mdat_off;
}

CTEST(core_fix_raw, cr3_h265_preview_is_not_used)
{
	cb_buf_t b = { 0 };
	cr3_build(&b);

	/* the fixture is what it claims: LibRaw opens it as a CR3 (crxLoadRaw) and hands out the preview as an
	 * H.265 image */
	libraw_data_t *raw = libraw_init(0);
	ASSERT_NOT_NULL(raw);
	ASSERT_EQUAL(LIBRAW_SUCCESS, libraw_open_buffer(raw, b.d, b.n));
	ASSERT_STR("Canon", raw->idata.make);
	ASSERT_EQUAL(CR3_W, raw->sizes.raw_width);
	ASSERT_EQUAL(LIBRAW_SUCCESS, libraw_unpack_thumb(raw));
	int ret = 0;
	libraw_processed_image_t *t = libraw_dcraw_make_mem_thumb(raw, &ret);
	ASSERT_NOT_NULL(t);
	ASSERT_EQUAL(LIBRAW_IMAGE_H265, t->type);
	ASSERT_EQUAL(CR3_PREVIEW_LEN, (int)t->data_size);
	libraw_dcraw_clear_mem(t);
	libraw_close(raw);

	/* the decoder cannot decode an H.265 preview and processes the raw data instead, which fails on the
	 * zero-filled CRX sample */
	img2_frames_t fr;
	img2_set_log_callback(cb_log_sink, NULL);
	img2_status_t st = img2_decode_raw(b.d, b.n, NULL, &fr);
	img2_set_log_callback(NULL, NULL);
	ASSERT_EQUAL(IMG2_E_CODEC, st);
	ASSERT_NULL(fr.frames);
	ASSERT_EQUAL(0, fr.count);
	cb_free(&b);
}

CTEST(core_fix_raw, two_colour_preview_falls_back_to_raw)
{
	/* A plain CFA TIFF (no DNGVersion) with a camera make and an 8-bit 2-sample (grey + alpha) preview IFD:
	 * outside DNG LibRaw selects it and hands out a bitmap with colors == 2, which the decoder does not
	 * convert (1 or 3 colours only); the raw data is processed instead */
	cb_buf_t b = { 0 };
	dng_opts_t o = { .preview = PREV_GRAY_ALPHA, .full_meta = 1, .not_dng = 1 };
	dng_build(&b, &o);
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, img2_decode_raw(b.d, b.n, NULL, &fr));
	ASSERT_EQUAL(1, fr.count);
	ASSERT_EQUAL_U((uint32_t)DNG_W, fr.frames[0]->width);
	ASSERT_EQUAL_U((uint32_t)DNG_H, fr.frames[0]->height);
	ASSERT_EQUAL(255, cb_px(&fr, 0, 10, 10)[3]);
	ASSERT_STR("Img2Cam", fr.frames[0]->exif->make);
	img2_frames_free(&fr);
	cb_free(&b);
}
