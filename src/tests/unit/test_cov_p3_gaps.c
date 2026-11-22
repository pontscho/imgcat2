/**
 * @file test_cov_p3_gaps.c
 * @brief Coverage phase 3: remaining reachable gaps after the lane merge
 *
 * Fixtures are assembled byte by byte in-test (test_cov_b_util.h writers).
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../libimg2/decoders/decoder.h"
#include "test_cov_b_util.h"

static const uint8_t k_pal4[12] = { 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255 };

/*
 * decoders/decoder.h img2_decode_check_total_pixels() with a zero-area canvas (B135 `pixels != 0` false): a GIF
 * whose logical screen is 0x0 with two frames. The total check passes (0 pixels per frame); the 0x0 canvas is
 * then rejected when it is allocated.
 */
CTEST(p3_gaps, gif_zero_screen_with_two_frames)
{
	cb_buf_t b = { 0 };
	gif_header(&b, 0, 0, 1, k_pal4);
	gif_fill_image(&b, 0, 0, 1, 1, 1);
	gif_fill_image(&b, 0, 0, 1, 1, 2);
	cb_u8(&b, 0x3B);

	img2_frames_t fr;
	memset(&fr, 0, sizeof(fr));
	img2_status_t st = img2_decode_gif_animated(b.d, b.n, NULL, &fr);
	ASSERT_EQUAL(IMG2_E_LIMIT, st);
	ASSERT_NULL(fr.frames);
	ASSERT_EQUAL(0, fr.count);
	cb_free(&b);
}
