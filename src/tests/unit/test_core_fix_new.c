/**
 * @file test_core_fix_new.c
 * @brief RAW decoder (decoder_raw.c) when LibRaw's processor cannot be created: `new LibRaw` throws
 *        std::bad_alloc, libraw_init() returns NULL
 *
 * Linked with core_fix_new_wrap.cpp and -Wl,--wrap=_Znwm,--wrap=_Znam (Linux only).
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

void core_fix_new_arm(long n);
void core_fix_new_disarm(void);
int core_fix_new_fired(void);

static int s_init_failed;

static void init_log_probe(img2_log_level_t level, const char *msg, void *user)
{
	(void)level;
	(void)user;
	s_init_failed += strstr(msg, "Failed to initialize LibRAW") != NULL;
}

CTEST(core_fix_new, libraw_init_bad_alloc)
{
	cb_buf_t b = { 0 };
	dng_opts_t o = { .preview = PREV_RGB };
	dng_build(&b, &o);
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, img2_decode_raw(b.d, b.n, NULL, &fr));
	img2_frames_free(&fr);

	/* the first operator new of a decode is LibRaw's processor object itself (libraw_init) */
	img2_set_log_callback(init_log_probe, NULL);
	core_fix_new_arm(0);
	img2_status_t st = img2_decode_raw(b.d, b.n, NULL, &fr);
	int fired = core_fix_new_fired();
	core_fix_new_disarm();
	img2_set_log_callback(NULL, NULL);
	ASSERT_TRUE(fired);
	ASSERT_EQUAL(1, s_init_failed);
	ASSERT_EQUAL(IMG2_E_CODEC, st);
	ASSERT_NULL(fr.frames);
	ASSERT_EQUAL(0, fr.count);

	/* the process is unharmed: the next decode works */
	ASSERT_EQUAL(IMG2_OK, img2_decode_raw(b.d, b.n, NULL, &fr));
	img2_frames_free(&fr);
	cb_free(&b);
}
