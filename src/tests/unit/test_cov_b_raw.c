/**
 * @file test_cov_b_raw.c
 * @brief RAW decoder coverage (decoder_raw.c): minimal DNG files written in-test (no embedded preview, an
 *        RGB or grey bitmap preview, an undecodable JPEG preview), the full LibRaw processing path, the
 *        LibRaw metadata (orientation, lens, GPS, text, XMP), errors, metadata leaks
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <img2/metadata.h>
#include <jpeglib.h>

#include "../../libimg2/decoders/decoder.h"
#include "test_cov_b_util.h"

#ifndef IMG2_TEST_SRC_DIR
#error "IMG2_TEST_SRC_DIR must be defined by the build system"
#endif

#include "test_cov_b_dng.h"

static img2_status_t raw_decode(const cb_buf_t *b, img2_frames_t *out)
{
	return img2_decode_raw(b->d, b->n, NULL, out);
}

/** Width and height after applying the EXIF orientation of frame 0 (what a viewer shows) */
static void raw_display_size(const img2_frames_t *fr, uint32_t *w, uint32_t *h)
{
	const img2_image_t *img = fr->frames[0];
	unsigned o = img->exif != NULL ? img->exif->orientation : 1;
	int swap = o >= 5 && o <= 8;
	*w = swap ? img->height : img->width;
	*h = swap ? img->width : img->height;
}

static const char k_xmp[] = "<r><d><dc:creator>Xavier</dc:creator></d></r>";

/* ---- tests ---- */

CTEST(cov_b_raw, full_processing_and_metadata)
{
	/* no preview: unpack + dcraw_process + make_mem_image */
	cb_buf_t b = { 0 };
	dng_opts_t o = { .preview = PREV_NONE, .full_meta = 1, .orientation = 6, .xmp = k_xmp, .desc = "A description" };
	dng_build(&b, &o);
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, raw_decode(&b, &fr));
	ASSERT_EQUAL(1, fr.count);
	const img2_exif_t *e = fr.frames[0]->exif;
	ASSERT_NOT_NULL(e);
	ASSERT_STR("Img2Cam", e->make);
	ASSERT_STR("Model 1", e->model);
	ASSERT_STR("img2-test", e->software);
	ASSERT_TRUE(e->has_datetime);
	ASSERT_STR("2021:06:15 12:34:56", e->date_time);
	ASSERT_TRUE(e->has_iso);
	ASSERT_EQUAL(400, e->iso_speed);
	ASSERT_TRUE(e->has_exposure_time);
	ASSERT_DBL_NEAR(1.0 / 250.0, e->exposure_time);
	ASSERT_TRUE(e->has_f_number);
	ASSERT_DBL_NEAR(2.8, e->f_number);
	ASSERT_TRUE(e->has_focal_length);
	ASSERT_DBL_NEAR(50.0, e->focal_length);
	ASSERT_DBL_NEAR(75.0, e->focal_length_35mm);
	ASSERT_STR("LensCo", e->lens_make);
	ASSERT_STR("Zoom 24-70", e->lens_model);
	ASSERT_DBL_NEAR(24.0, e->min_focal_length);
	ASSERT_DBL_NEAR(70.0, e->max_focal_length);
	ASSERT_DBL_NEAR(2.8, e->max_aperture);
	ASSERT_STR("A description", e->description);
	ASSERT_STR("Ann Artist", e->artist);
	ASSERT_TRUE(e->has_gps);
	ASSERT_DBL_NEAR(-33.5, e->gps_latitude);
	ASSERT_DBL_NEAR(-70.26, e->gps_longitude);
	ASSERT_EQUAL('S', e->gps_latitude_ref);
	ASSERT_EQUAL('W', e->gps_longitude_ref);
	ASSERT_NOT_NULL(fr.frames[0]->xmp);
	ASSERT_STR("Xavier", fr.frames[0]->xmp->creator);
	/* the sensor is 64x48, orientation 6 (rotate 90 CW): a viewer applying the EXIF orientation must show
	 * 48x64, i.e. the pixels are not rotated a second time by LibRaw */
	ASSERT_EQUAL(6, e->orientation);
	uint32_t w, h;
	raw_display_size(&fr, &w, &h);
	ASSERT_EQUAL_U(48u, w);
	ASSERT_EQUAL_U(64u, h);
	img2_frames_free(&fr);
	cb_free(&b);
}

CTEST(cov_b_raw, orientation_mapping)
{
	/* TIFF orientation -> LibRaw flip -> EXIF orientation: 1 -> 0 -> 1, 3 -> 3 -> 3, 8 -> 5 -> 8,
	 * 6 -> 6 -> 6, 2 (mirrored) -> 1 -> 1 (unsupported, reported as normal) */
	const unsigned in[5] = { 1, 3, 8, 6, 2 };
	const unsigned out[5] = { 1, 3, 8, 6, 1 };
	for (int i = 0; i < 5; i++) {
		cb_buf_t b = { 0 };
		dng_opts_t o = { .preview = PREV_RGB, .orientation = in[i] };
		dng_build(&b, &o);
		img2_frames_t fr;
		ASSERT_EQUAL(IMG2_OK, raw_decode(&b, &fr));
		ASSERT_NOT_NULL(fr.frames[0]->exif);
		ASSERT_EQUAL(out[i], fr.frames[0]->exif->orientation);
		img2_frames_free(&fr);
		cb_free(&b);
	}
}

CTEST(cov_b_raw, bitmap_previews)
{
	img2_frames_t fr;
	cb_buf_t b = { 0 };
	/* 8-bit RGB preview, sparse metadata: whitespace-only description, no XMP */
	dng_opts_t o = { .preview = PREV_RGB, .desc = " \t\n\r" };
	dng_build(&b, &o);
	ASSERT_EQUAL(IMG2_OK, raw_decode(&b, &fr));
	ASSERT_EQUAL_U((uint32_t)PREV_W, fr.frames[0]->width);
	CB_ASSERT_PX(&fr, 0, 0, 0, 0, 1, 2, 255);
	CB_ASSERT_PX(&fr, 0, 1, 0, 3, 4, 5, 255);
	ASSERT_STR("", fr.frames[0]->exif->description);
	ASSERT_NULL(fr.frames[0]->xmp);
	img2_frames_free(&fr);
	cb_free(&b);

	/* 8-bit grey preview: one byte per pixel */
	dng_opts_t g = { .preview = PREV_GRAY };
	dng_build(&b, &g);
	ASSERT_EQUAL(IMG2_OK, raw_decode(&b, &fr));
	ASSERT_EQUAL_U((uint32_t)PREV_W, fr.frames[0]->width);
	CB_ASSERT_PX(&fr, 0, 0, 0, 0, 0, 0, 255);
	CB_ASSERT_PX(&fr, 0, 1, 0, 1, 1, 1, 255);
	CB_ASSERT_PX(&fr, 0, PREV_W - 1, PREV_H - 1, PREV_W * PREV_H - 1, PREV_W * PREV_H - 1, PREV_W * PREV_H - 1, 255);
	img2_frames_free(&fr);
	cb_free(&b);
}

CTEST(cov_b_raw, unusable_previews_fall_back)
{
	/* previews the decoder cannot use: a JPEG libjpeg cannot decode (12-bit SOF), a grey + alpha bitmap
	 * (LibRaw ignores it), a bitmap whose data is shorter than width x height x 3. The raw data is processed
	 * instead. The raw is a monochrome LinearRaw: LibRaw returns one colour per pixel. XMP that does not
	 * parse is dropped. */
	const int previews[3] = { PREV_BAD_JPEG, PREV_GRAY_ALPHA, PREV_SHORT };
	for (int i = 0; i < 3; i++) {
		cb_buf_t b = { 0 };
		dng_opts_t o = { .preview = previews[i], .xmp = "<x><unterminated", .mono = 1 };
		dng_build(&b, &o);
		img2_frames_t fr;
		ASSERT_EQUAL(IMG2_OK, raw_decode(&b, &fr));
		ASSERT_EQUAL_U((uint32_t)DNG_W, fr.frames[0]->width);
		ASSERT_EQUAL_U((uint32_t)DNG_H, fr.frames[0]->height);
		const uint8_t *p = cb_px(&fr, 0, 10, 10);
		ASSERT_EQUAL(p[0], p[1]);
		ASSERT_EQUAL(p[0], p[2]);
		ASSERT_EQUAL(255, p[3]);
		ASSERT_NULL(fr.frames[0]->xmp);
		img2_frames_free(&fr);
		cb_free(&b);
	}
}

CTEST(cov_b_raw, unreadable_input)
{
	img2_frames_t fr;
	/* not a raw file at all */
	static const uint8_t junk[64] = { 'n', 'o', 't', ' ', 'r', 'a', 'w' };
	ASSERT_EQUAL(IMG2_E_CODEC, img2_decode_raw(junk, sizeof(junk), NULL, &fr));
	ASSERT_NULL(fr.frames);
	/* no preview and the raw strip outside the file: unpack fails */
	cb_buf_t b = { 0 };
	dng_opts_t o = { .preview = PREV_NONE, .truncate_raw = 1 };
	dng_build(&b, &o);
	ASSERT_EQUAL(IMG2_E_CODEC, raw_decode(&b, &fr));
	ASSERT_NULL(fr.frames);
	cb_free(&b);
}

static uint8_t *raw_read_fixture(const char *rel, size_t *len)
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

CTEST(cov_b_raw, jpeg_preview_metadata_does_not_leak)
{
	/* LibRaw hands out JPEG previews with an EXIF block; the JPEG decoder's metadata must not be dropped
	 * on the floor when the RAW metadata replaces it */
	size_t len = 0;
	uint8_t *dng = raw_read_fixture("raw/canon_eos_350d.dng", &len);
	ASSERT_NOT_NULL(dng);
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, img2_decode_raw(dng, len, NULL, &fr));
	ASSERT_NOT_NULL(fr.frames[0]->exif);
	img2_frames_free(&fr);
	size_t before = cb_heap_in_use();
	for (int i = 0; i < 2; i++) {
		ASSERT_EQUAL(IMG2_OK, img2_decode_raw(dng, len, NULL, &fr));
		img2_frames_free(&fr);
	}
	size_t after = cb_heap_in_use();
	ASSERT_TRUE(after < before + 4096); /* one img2_exif_t per leaked run is > 3 KiB */
	free(dng);
}

CTEST(cov_b_raw, jpeg_preview_with_xmp)
{
	/* LibRaw adds its EXIF block to the JPEG preview, which also carries an XMP packet: both are replaced by
	 * the RAW file's metadata (the raw's own XMLPacket here), without leaking the preview's */
	cb_buf_t b = { 0 };
	dng_opts_t o = { .preview = PREV_JPEG_XMP, .full_meta = 1, .xmp = k_xmp, .gps_ne = 1 };
	dng_build(&b, &o);
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, raw_decode(&b, &fr));
	ASSERT_EQUAL_U((uint32_t)PREV_W, fr.frames[0]->width);
	ASSERT_NOT_NULL(fr.frames[0]->xmp);
	ASSERT_STR("Xavier", fr.frames[0]->xmp->creator);
	ASSERT_STR("Img2Cam", fr.frames[0]->exif->make);
	ASSERT_DBL_NEAR(33.5, fr.frames[0]->exif->gps_latitude);
	ASSERT_DBL_NEAR(70.26, fr.frames[0]->exif->gps_longitude);
	img2_frames_free(&fr);

	for (int i = 0; i < 8; i++) { /* warm-up: glibc's tcache (freed chunks it holds count as in use) */
		ASSERT_EQUAL(IMG2_OK, raw_decode(&b, &fr));
		img2_frames_free(&fr);
	}
	size_t before = cb_heap_in_use();
	for (int i = 0; i < 16; i++) {
		ASSERT_EQUAL(IMG2_OK, raw_decode(&b, &fr));
		img2_frames_free(&fr);
	}
	ASSERT_TRUE(cb_heap_in_use() < before + 4096);
	cb_free(&b);
}

CTEST(cov_b_raw, no_camera_names)
{
	/* no Make / Model / UniqueCameraModel: LibRaw reports make "DNG" and an empty model */
	cb_buf_t b = { 0 };
	dng_opts_t o = { .preview = PREV_RGB, .no_camera = 1 };
	dng_build(&b, &o);
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, raw_decode(&b, &fr));
	ASSERT_STR("", fr.frames[0]->exif->model);
	img2_frames_free(&fr);
	cb_free(&b);

	/* the same as a plain CFA TIFF (no DNGVersion): LibRaw does not open a raw without a camera make */
	dng_opts_t t = { .preview = PREV_NONE, .no_camera = 1, .not_dng = 1 };
	dng_build(&b, &t);
	ASSERT_EQUAL(IMG2_E_CODEC, raw_decode(&b, &fr));
	cb_free(&b);
}

CTEST(cov_b_raw, argument_checks)
{
	const uint8_t one = 'I';
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_raw(&one, 1, NULL, NULL));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_raw(NULL, 1, NULL, &fr));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_raw(&one, 0, NULL, &fr));
}
