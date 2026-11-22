/**
 * @file test_cov_b_gif.c
 * @brief GIF decoder coverage (decoder_gif.c): composition, disposal modes, interlacing, colour-map
 *        edge cases, NETSCAPE2.0 loop parsing, record-walk and LZW errors
 *
 * Fixtures are assembled byte by byte in-test, with a minimal LZW encoder that only emits literal codes
 * (a CLEAR code before the code table would grow, so the code width stays min_code_size + 1).
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../libimg2/decoders/decoder.h"
#include "test_cov_b_util.h"

/* ---- GIF writer ---- */

static const uint8_t k_pal4[12] = { 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255 }; /* black, red, green, blue */

static img2_status_t gif_decode(const cb_buf_t *b, img2_frames_t *out)
{
	return img2_decode_gif_animated(b->d, b->n, NULL, out);
}

/* ---- tests ---- */

CTEST(cov_b_gif, single_frame_is_not_animated)
{
	cb_buf_t b = { 0 };
	gif_header(&b, 2, 2, 1, k_pal4);
	cb_put(&b, "\x21\xF9\x00", 3); /* graphics control extension without any data sub-block */
	gif_fill_image(&b, 0, 0, 2, 2, 2);
	cb_u8(&b, 0x3B);

	ASSERT_FALSE(img2_gif_is_animated(b.d, b.n));
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, gif_decode(&b, &fr));
	ASSERT_EQUAL(1, fr.count);
	ASSERT_EQUAL(0, fr.loop_count);
	ASSERT_EQUAL_U(0u, fr.frames[0]->delay_ms);
	CB_ASSERT_PX(&fr, 0, 1, 1, 0, 255, 0, 255);
	img2_frames_free(&fr);
	cb_free(&b);
}

CTEST(cov_b_gif, disposal_modes_and_clipping)
{
	static const uint8_t lpal2[6] = { 255, 255, 0, 0, 255, 255 }; /* yellow, cyan */
	cb_buf_t b = { 0 };
	gif_header(&b, 4, 4, 1, k_pal4);
	/* 1: all red, keep (delay 0) */
	gif_gce(&b, 1, 0, -1);
	gif_fill_image(&b, 0, 0, 4, 4, 1);
	/* 2: green 3x3 at (2,2), sticking out of the canvas on both axes, first pixel transparent; restore to background */
	gif_gce(&b, 2, 7, 0);
	uint8_t g[9];
	memset(g, 2, sizeof(g));
	g[0] = 0;
	gif_image(&b, 2, 2, 3, 3, 0, -1, NULL, 2, g, sizeof(g));
	/* 3: blue 1x1 at (0,0), restore to previous */
	gif_gce(&b, 3, 1, -1);
	gif_fill_image(&b, 0, 0, 1, 1, 3);
	/* 4: unknown disposal 4; 2-entry local table with code size 2: index 3 is outside the table (not drawn) */
	gif_gce(&b, 4, 2, -1);
	const uint8_t q[4] = { 0, 1, 3, 3 };
	gif_image(&b, 0, 0, 2, 2, 0, 0, lpal2, 2, q, sizeof(q));
	/* 5: restore to previous again (the backup canvas is reused) */
	gif_gce(&b, 3, 1, -1);
	gif_fill_image(&b, 3, 0, 1, 1, 2);
	cb_u8(&b, 0x3B);

	ASSERT_TRUE(img2_gif_is_animated(b.d, b.n));
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, gif_decode(&b, &fr));
	ASSERT_EQUAL(5, fr.count);
	CB_ASSERT_PX(&fr, 4, 3, 0, 0, 255, 0, 255);
	CB_ASSERT_PX(&fr, 4, 0, 0, 255, 255, 0, 255);
	ASSERT_EQUAL(1, fr.loop_count); /* no NETSCAPE2.0 block */
	ASSERT_EQUAL_U(0u, fr.frames[0]->delay_ms);
	ASSERT_EQUAL_U(70u, fr.frames[1]->delay_ms);

	CB_ASSERT_PX(&fr, 0, 3, 3, 255, 0, 0, 255);
	CB_ASSERT_PX(&fr, 1, 2, 2, 255, 0, 0, 255); /* transparent pixel keeps the red below */
	CB_ASSERT_PX(&fr, 1, 3, 2, 0, 255, 0, 255);
	CB_ASSERT_PX(&fr, 1, 3, 3, 0, 255, 0, 255);
	CB_ASSERT_PX(&fr, 1, 1, 1, 255, 0, 0, 255);
	/* frame 3 is drawn on the background-cleared area of frame 2 */
	CB_ASSERT_PX(&fr, 2, 0, 0, 0, 0, 255, 255);
	CB_ASSERT_PX(&fr, 2, 3, 3, 0, 0, 0, 0);
	CB_ASSERT_PX(&fr, 2, 1, 0, 255, 0, 0, 255);
	/* frame 3 restored to previous: (0,0) red again under the frame 4 pixels */
	CB_ASSERT_PX(&fr, 3, 0, 0, 255, 255, 0, 255);
	CB_ASSERT_PX(&fr, 3, 1, 0, 0, 255, 255, 255);
	CB_ASSERT_PX(&fr, 3, 0, 1, 255, 0, 0, 255);
	CB_ASSERT_PX(&fr, 3, 1, 1, 255, 0, 0, 255);
	CB_ASSERT_PX(&fr, 3, 2, 2, 0, 0, 0, 0);
	img2_frames_free(&fr);
	cb_free(&b);
}

CTEST(cov_b_gif, interlaced_rows)
{
	/* 2x9 interlaced: stream order is pass 1 (rows 0, 8), pass 2 (4), pass 3 (2, 6), pass 4 (1, 3, 5, 7) */
	static const unsigned order[9] = { 0, 8, 4, 2, 6, 1, 3, 5, 7 };
	uint8_t px[18];
	for (unsigned i = 0; i < 9; i++) {
		px[i * 2] = px[i * 2 + 1] = (uint8_t)(order[i] % 4);
	}
	cb_buf_t b = { 0 };
	gif_header(&b, 2, 9, 1, k_pal4);
	gif_image(&b, 0, 0, 2, 9, 1, -1, NULL, 2, px, sizeof(px));
	cb_u8(&b, 0x3B);

	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, gif_decode(&b, &fr));
	ASSERT_EQUAL(1, fr.count);
	for (unsigned y = 0; y < 9; y++) {
		const uint8_t *c = &k_pal4[(y % 4) * 3];
		CB_ASSERT_PX(&fr, 0, 1, y, c[0], c[1], c[2], 255);
	}
	img2_frames_free(&fr);

	/* the same image with its LZW data ending two pixels early: DGifGetLine fails in pass 4 */
	cb_buf_t t = { 0 };
	gif_header(&t, 2, 9, 1, k_pal4);
	cb_u8(&t, 0x2C);
	cb_le16(&t, 0);
	cb_le16(&t, 0);
	cb_le16(&t, 2);
	cb_le16(&t, 9);
	cb_u8(&t, 0x40);
	gif_lzw(&t, 2, px, sizeof(px) - 2, 1);
	cb_u8(&t, 0x3B);
	ASSERT_EQUAL(IMG2_E_CODEC, gif_decode(&t, &fr));
	ASSERT_NULL(fr.frames);
	cb_free(&t);
	cb_free(&b);
}

CTEST(cov_b_gif, netscape_loop_rules)
{
	cb_buf_t b = { 0 };
	gif_header(&b, 1, 1, 1, k_pal4);
	const uint8_t sub_loop5[3] = { 1, 5, 0 };
	gif_app(&b, "ANIMEXTS1.0", 11, sub_loop5, 3);             /* 11-byte id, not NETSCAPE2.0 */
	gif_app(&b, "NETSCAPE", 8, sub_loop5, 3);                 /* id shorter than 11 bytes */
	const uint8_t sub_short[2] = { 1, 7 };
	gif_app(&b, "NETSCAPE2.0", 11, sub_short, 2);             /* loop sub-block shorter than 3 bytes */
	const uint8_t sub_id2[3] = { 2, 8, 0 };
	gif_app(&b, "NETSCAPE2.0", 11, sub_id2, 3);               /* sub-block id 2 (buffering), not 1 */
	const uint8_t netscape_only[] = { 0x21, 0xFF, 11, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0', 0 };
	cb_put(&b, netscape_only, sizeof(netscape_only));         /* NETSCAPE2.0 block ends its extension ... */
	const uint8_t comment[] = { 0x21, 0xFE, 3, 1, 9, 0, 0 };  /* ... the next extension is not its continuation */
	cb_put(&b, comment, sizeof(comment));
	const uint8_t sub_loop3[3] = { 1, 3, 0 };
	gif_app(&b, "NETSCAPE2.0", 11, sub_loop3, 3);             /* first match: 3 */
	const uint8_t sub_loop4[3] = { 1, 4, 0 };
	gif_app(&b, "NETSCAPE2.0", 11, sub_loop4, 3);             /* later ones are ignored */
	gif_fill_image(&b, 0, 0, 1, 1, 1);
	gif_fill_image(&b, 0, 0, 1, 1, 2);
	cb_u8(&b, 0x3B);

	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, gif_decode(&b, &fr));
	ASSERT_EQUAL(2, fr.count);
	ASSERT_EQUAL(3, fr.loop_count);
	img2_frames_free(&fr);
	cb_free(&b);
}

CTEST(cov_b_gif, missing_color_map)
{
	cb_buf_t b = { 0 };
	gif_header(&b, 1, 1, -1, NULL);
	gif_fill_image(&b, 0, 0, 1, 1, 1);
	cb_u8(&b, 0x3B);
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_E_CODEC, gif_decode(&b, &fr));
	ASSERT_NULL(fr.frames);
	cb_free(&b);
}

CTEST(cov_b_gif, record_walk_errors)
{
	img2_frames_t fr;
	cb_buf_t h = { 0 };
	gif_header(&h, 1, 1, 1, k_pal4);

	/* trailer right after the screen descriptor: no image */
	cb_buf_t b = { 0 };
	cb_put(&b, h.d, h.n);
	cb_u8(&b, 0x3B);
	ASSERT_EQUAL(IMG2_E_CODEC, gif_decode(&b, &fr));
	ASSERT_FALSE(img2_gif_is_animated(b.d, b.n));
	cb_free(&b);

	/* extension introducer, then EOF (DGifGetExtension) */
	cb_put(&b, h.d, h.n);
	cb_u8(&b, 0x21);
	ASSERT_EQUAL(IMG2_E_CODEC, gif_decode(&b, &fr));
	cb_free(&b);

	/* extension sub-block, then EOF instead of the next block length (DGifGetExtensionNext) */
	cb_put(&b, h.d, h.n);
	cb_put(&b, "\x21\xFE\x03" "abc", 6);
	ASSERT_EQUAL(IMG2_E_CODEC, gif_decode(&b, &fr));
	cb_free(&b);

	/* raster cut in its second sub-block (DGifGetCodeNext) */
	cb_buf_t big = { 0 };
	gif_header(&big, 32, 32, 1, k_pal4);
	gif_fill_image(&big, 0, 0, 32, 32, 1);
	ASSERT_TRUE(big.n > h.n + 10 + 256 + 20);
	cb_put(&b, big.d, h.n + 10 + 1 + 256 + 10); /* descriptor, code size, first block (len + 255), part of the second */
	ASSERT_EQUAL(IMG2_E_CODEC, gif_decode(&b, &fr));
	ASSERT_NULL(fr.frames);
	cb_free(&b);
	cb_free(&big);
	cb_free(&h);
}

static char s_last_log[512];

static void gif_log_cb(img2_log_level_t level, const char *msg, void *user)
{
	(void)level;
	(void)user;
	snprintf(s_last_log, sizeof(s_last_log), "%s", msg);
}

CTEST(cov_b_gif, raster_errors_after_a_clean_walk)
{
	img2_frames_t fr;
	img2_log_level_t saved_level = img2_get_log_level();
	img2_set_log_level(IMG2_LOG_ERROR);
	img2_set_log_callback(gif_log_cb, NULL);
	const uint8_t px[4] = { 1, 2, 3, 1 };
	/* LZW data ends before the last pixel (non-interlaced DGifGetLine) */
	cb_buf_t b = { 0 };
	gif_header(&b, 2, 2, 1, k_pal4);
	cb_u8(&b, 0x2C);
	cb_le16(&b, 0);
	cb_le16(&b, 0);
	cb_le16(&b, 2);
	cb_le16(&b, 2);
	cb_u8(&b, 0);
	gif_lzw(&b, 2, px, 3, 1);
	cb_u8(&b, 0x3B);
	ASSERT_EQUAL(IMG2_E_CODEC, gif_decode(&b, &fr));
	cb_free(&b);

	/* image descriptors the walk accepts but the decoder rejects: zero width, zero height (w * h > INT_MAX, as
	 * any descriptor over IMG2_MAX_PIXELS, is IMG2_E_LIMIT from the walk since [F16]: test_f_residual_limits.c) */
	const unsigned dims[2][2] = { { 0, 1 }, { 1, 0 } };
	for (int i = 0; i < 2; i++) {
		gif_header(&b, 1, 1, 1, k_pal4);
		gif_image(&b, 0, 0, dims[i][0], dims[i][1], 0, -1, NULL, 2, px, 1);
		cb_u8(&b, 0x3B);
		s_last_log[0] = '\0';
		ASSERT_EQUAL(IMG2_E_CODEC, gif_decode(&b, &fr));
		ASSERT_NULL(fr.frames);
		/* the error names a giflib error, never a NULL GifErrorString() (printf "%s" of NULL) */
		ASSERT_TRUE(strstr(s_last_log, "Failed to read GIF structure: ") != NULL);
		ASSERT_TRUE(strstr(s_last_log, "(null)") == NULL);
		cb_free(&b);
	}
	img2_set_log_callback(NULL, NULL);
	img2_set_log_level(saved_level);
}

CTEST(cov_b_gif, argument_checks)
{
	img2_frames_t fr;
	const uint8_t one = 'G';
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_gif_animated(&one, 1, NULL, NULL));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_gif_animated(NULL, 1, NULL, &fr));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_gif_animated(&one, 0, NULL, &fr));
	ASSERT_FALSE(img2_gif_is_animated(NULL, 1));
	ASSERT_FALSE(img2_gif_is_animated(&one, 0));
}
