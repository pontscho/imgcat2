/**
 * @file test_cov_b_png.c
 * @brief PNG / APNG decoder coverage (decoder_png.c): APNG colour types, blend and dispose operations, a
 *        hidden default image, truncated animations, tEXt / iTXt / eXIf metadata
 *
 * Fixtures are written chunk by chunk in-test (stored deflate blocks, filter type 0).
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <img2/metadata.h>

#include "../../libimg2/decoders/decoder.h"
#include "test_cov_b_util.h"

static const uint8_t k_plte[12] = { 10, 10, 10, 255, 0, 0, 0, 255, 0, 0, 0, 255 };

/* 2x2 palette + tRNS APNG, 3 frames: dispose PREVIOUS twice, BACKGROUND once; blend SOURCE, OVER on
 * transparent-over-transparent (out_a 0) and opaque-over-transparent */
static void apng_palette(cb_buf_t *b)
{
	cpng_start(b, 2, 2, 8, 3, 0);
	cb_png_chunk(b, "PLTE", k_plte, sizeof(k_plte));
	const uint8_t trns[1] = { 0 };
	cb_png_chunk(b, "tRNS", trns, 1);
	cpng_actl(b, 3, 0);
	uint8_t raw[16];
	const uint8_t idx0[4] = { 1, 2, 3, 0 };
	cpng_fctl(b, 0, 2, 2, 0, 0, 1, 0, 2, 0);
	cpng_data(b, -1, raw, cpng_rows(raw, idx0, 2, 2));
	const uint8_t idx1[1] = { 0 };
	cpng_fctl(b, 1, 1, 1, 1, 1, 3, 100, 2, 1);
	cpng_data(b, 2, raw, cpng_rows(raw, idx1, 1, 1));
	const uint8_t idx2[1] = { 3 };
	cpng_fctl(b, 3, 1, 1, 0, 0, 0, 0, 1, 1);
	cpng_data(b, 4, raw, cpng_rows(raw, idx2, 1, 1));
	cpng_end(b);
}

CTEST(cov_b_png, apng_palette_dispose_blend)
{
	cb_buf_t b = { 0 };
	apng_palette(&b);
	ASSERT_TRUE(img2_png_is_animated(b.d, b.n));

	/* many runs: llvm-cov derives the counts after setjmp() by subtraction, so every longjmp back (error and
	 * allocation-failure tests) hides one execution of the success path; it must outnumber them */
	for (int run = 0; run < 600; run++) {
		img2_frames_t fr;
		ASSERT_EQUAL(IMG2_OK, cb_decode(b.d, b.n, &fr));
		ASSERT_EQUAL(3, fr.count);
		ASSERT_EQUAL(0, fr.loop_count);
		ASSERT_EQUAL_U(10u, fr.frames[0]->delay_ms);
		ASSERT_EQUAL_U(30u, fr.frames[1]->delay_ms);
		ASSERT_EQUAL_U(0u, fr.frames[2]->delay_ms);
		CB_ASSERT_PX(&fr, 0, 0, 0, 255, 0, 0, 255);
		CB_ASSERT_PX(&fr, 0, 1, 1, 10, 10, 10, 0);
		/* frame 0 disposed to previous (empty canvas); the transparent pixel blended over nothing stays empty */
		CB_ASSERT_PX(&fr, 1, 0, 0, 0, 0, 0, 0);
		CB_ASSERT_PX(&fr, 1, 1, 1, 0, 0, 0, 0);
		CB_ASSERT_PX(&fr, 2, 0, 0, 0, 0, 255, 255);
		CB_ASSERT_PX(&fr, 2, 1, 0, 0, 0, 0, 0);
		img2_frames_free(&fr);
	}
	cb_free(&b);
}

CTEST(cov_b_png, apng_gray_alpha_16bit)
{
	cb_buf_t b = { 0 };
	cpng_start(&b, 2, 1, 16, 4, 0);
	cpng_actl(&b, 2, 5);
	uint8_t raw[16];
	const uint8_t px0[8] = { 0x12, 0x34, 0xFF, 0xFF, 0xAB, 0xCD, 0x80, 0x00 };
	cpng_fctl(&b, 0, 2, 1, 0, 0, 1, 1000, 0, 0);
	cpng_data(&b, -1, raw, cpng_rows(raw, px0, 8, 1));
	const uint8_t px1[4] = { 0, 0, 0xFF, 0xFF };
	cpng_fctl(&b, 1, 1, 1, 1, 0, 1, 1000, 0, 0);
	cpng_data(&b, 2, raw, cpng_rows(raw, px1, 4, 1));
	cpng_end(&b);

	for (int run = 0; run < 300; run++) {
		img2_frames_t fr;
		ASSERT_EQUAL(IMG2_OK, cb_decode(b.d, b.n, &fr));
		ASSERT_EQUAL(2, fr.count);
		ASSERT_EQUAL(5, fr.loop_count);
		ASSERT_EQUAL_U(1u, fr.frames[0]->delay_ms);
		CB_ASSERT_PX(&fr, 0, 0, 0, 0x12, 0x12, 0x12, 0xFF);
		CB_ASSERT_PX(&fr, 0, 1, 0, 0xAB, 0xAB, 0xAB, 0x80);
		CB_ASSERT_PX(&fr, 1, 1, 0, 0, 0, 0, 0xFF);
		img2_frames_free(&fr);
	}
	cb_free(&b);
}

CTEST(cov_b_png, apng_hidden_default_image)
{
	/* acTL, IDAT without a preceding fcTL (the default image is not part of the animation), then 2 frames */
	cb_buf_t b = { 0 };
	cpng_start(&b, 2, 1, 8, 2, 0);
	cpng_actl(&b, 2, 0);
	uint8_t raw[16];
	const uint8_t px0[6] = { 1, 2, 3, 4, 5, 6 };
	cpng_data(&b, -1, raw, cpng_rows(raw, px0, 6, 1));
	const uint8_t px1[3] = { 9, 9, 9 };
	cpng_fctl(&b, 0, 1, 1, 0, 0, 0, 0, 0, 0);
	cpng_data(&b, 1, raw, cpng_rows(raw, px1, 3, 1));
	cpng_fctl(&b, 2, 1, 1, 1, 0, 0, 0, 0, 0);
	cpng_data(&b, 3, raw, cpng_rows(raw, px1, 3, 1));
	cpng_end(&b);

	for (int run = 0; run < 300; run++) {
		img2_frames_t fr;
		ASSERT_EQUAL(IMG2_OK, cb_decode(b.d, b.n, &fr));
		/* APNG spec: a default image without fcTL is not part of the animation (libpng counts it as a frame) */
		ASSERT_EQUAL(2, fr.count);
		CB_ASSERT_PX(&fr, 0, 0, 0, 9, 9, 9, 255);
		CB_ASSERT_PX(&fr, 0, 1, 0, 0, 0, 0, 0);
		CB_ASSERT_PX(&fr, 1, 0, 0, 9, 9, 9, 255);
		CB_ASSERT_PX(&fr, 1, 1, 0, 9, 9, 9, 255);
		img2_frames_free(&fr);
	}
	cb_free(&b);

	/* hidden default image + a single animation frame: one (not animated) output frame */
	cpng_start(&b, 1, 1, 8, 2, 0);
	cpng_actl(&b, 1, 3);
	cpng_data(&b, -1, raw, cpng_rows(raw, px0, 3, 1));
	cpng_fctl(&b, 0, 1, 1, 0, 0, 5, 0, 0, 0);
	cpng_data(&b, 1, raw, cpng_rows(raw, px1, 3, 1));
	cpng_end(&b);
	ASSERT_TRUE(img2_png_is_animated(b.d, b.n));
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, cb_decode(b.d, b.n, &fr));
	ASSERT_EQUAL(1, fr.count);
	ASSERT_EQUAL(0, fr.loop_count);
	ASSERT_EQUAL_U(0u, fr.frames[0]->delay_ms);
	CB_ASSERT_PX(&fr, 0, 0, 0, 9, 9, 9, 255);
	img2_frames_free(&fr);
	cb_free(&b);
}

CTEST(cov_b_png, apng_truncated_does_not_leak)
{
	cb_buf_t full = { 0 };
	apng_palette(&full);
	/* cut inside the last fdAT: libpng longjmps out of the frame loop with every frame buffer allocated */
	size_t cut = full.n - 12 - 10;
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_E_CODEC, cb_decode(full.d, cut, &fr));
	ASSERT_NULL(fr.frames);

	img2_set_log_callback(cb_log_sink, NULL);
	for (int i = 0; i < 8; i++) { /* warm-up: glibc's tcache (freed chunks it holds count as in use) */
		ASSERT_EQUAL(IMG2_E_CODEC, cb_decode(full.d, cut, &fr));
	}
	size_t before = cb_heap_in_use();
	for (int i = 0; i < 12; i++) {
		ASSERT_EQUAL(IMG2_E_CODEC, cb_decode(full.d, cut, &fr));
	}
	size_t after = cb_heap_in_use();
	img2_set_log_callback(NULL, NULL);
	/* a leaked run: frames array, 2 frames, canvas and backup (image structs + pixels), row pointers and
	 * frame buffer: > 500 bytes of heap */
	ASSERT_TRUE(after < before + 2048);
	cb_free(&full);
}

CTEST(cov_b_png, text_xmp_and_exif_chunks)
{
	cb_buf_t b = { 0 };
	cpng_start(&b, 1, 1, 8, 2, 0);
	cpng_text(&b, "Author", "Ann");
	cpng_text(&b, "Software", "sw");
	cpng_text(&b, "Other", "ignored");
	/* eXIf with the TIFF header libpng checks, but shorter than a TIFF header + IFD offset: the EXIF parse
	 * fails, the tEXt fields still land */
	const uint8_t exif[6] = { 'I', 'I', 42, 0, 8, 0 };
	cb_png_chunk(&b, "eXIf", exif, sizeof(exif));
	cpng_itxt(&b, "Description", "itxt but not XMP");
	cpng_itxt(&b, "XML:com.adobe.xmp", "<r><d><dc:creator>Bob</dc:creator></d></r>");
	uint8_t raw[8];
	const uint8_t px[3] = { 7, 8, 9 };
	cpng_data(&b, -1, raw, cpng_rows(raw, px, 3, 1));
	cpng_end(&b);

	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, cb_decode(b.d, b.n, &fr));
	ASSERT_EQUAL(1, fr.count);
	CB_ASSERT_PX(&fr, 0, 0, 0, 7, 8, 9, 255);
	ASSERT_NOT_NULL(fr.frames[0]->exif);
	ASSERT_EQUAL(0, fr.frames[0]->exif->orientation); /* nothing from the eXIf chunk (img2_exif_init: 0) */
	ASSERT_STR("Ann", fr.frames[0]->exif->artist);
	ASSERT_STR("sw", fr.frames[0]->exif->software);
	/* XMP in an iTXt chunk (the form the XMP spec prescribes for PNG) */
	ASSERT_NOT_NULL(fr.frames[0]->xmp);
	ASSERT_STR("Bob", fr.frames[0]->xmp->creator);
	img2_frames_free(&fr);
	cb_free(&b);
}

CTEST(cov_b_png, unparsable_xmp_is_dropped)
{
	cb_buf_t b = { 0 };
	cpng_start(&b, 1, 1, 8, 0, 0);
	cpng_itxt(&b, "XML:com.adobe.xmp", ""); /* empty packet: skipped */
	cpng_text(&b, "XML:com.adobe.xmp", "<r><unterminated");
	uint8_t raw[4];
	const uint8_t px[1] = { 200 };
	cpng_data(&b, -1, raw, cpng_rows(raw, px, 1, 1));
	cpng_end(&b);

	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, cb_decode(b.d, b.n, &fr));
	ASSERT_NULL(fr.frames[0]->xmp);
	CB_ASSERT_PX(&fr, 0, 0, 0, 200, 200, 200, 255);
	img2_frames_free(&fr);
	cb_free(&b);
}

CTEST(cov_b_png, truncated_at_a_chunk_boundary)
{
	cb_buf_t b = { 0 };
	cpng_start(&b, 1, 1, 8, 0, 0); /* signature + IHDR, nothing else: the next read finds no data at all */
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_E_CODEC, cb_decode(b.d, b.n, &fr));
	ASSERT_NULL(fr.frames);
	cb_free(&b);
}

CTEST(cov_b_png, argument_checks)
{
	const uint8_t one = 0x89;
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_png(&one, 1, NULL, NULL));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_png(NULL, 1, NULL, &fr));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_png(&one, 0, NULL, &fr));
	ASSERT_FALSE(img2_png_is_animated(NULL, 1));
	ASSERT_FALSE(img2_png_is_animated(&one, 0));
}
