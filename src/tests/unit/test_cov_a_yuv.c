/**
 * @file test_cov_a_yuv.c
 * @brief Raw YUV spec / geometry errors (decoders/decoder_yuv.c) without an error buffer
 *
 * Every error path formats its message only into a caller buffer of non-zero size: each one is driven with
 * a NULL buffer, with a buffer of size 0 (which must stay untouched), and with a real buffer.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <img2/img2.h>

#include "../ctest.h"

/* parse_spec with no buffer, a zero-size buffer and a real buffer: all IMG2_E_ARG, the zero-size one untouched */
static void spec_fails(const char *spec)
{
	img2_yuv_params_t p;
	char buf[8] = "keep";
	char msg[256] = "";
	ASSERT_EQUAL(IMG2_E_ARG, img2_yuv_parse_spec(spec, &p, NULL, 0));
	ASSERT_EQUAL(IMG2_E_ARG, img2_yuv_parse_spec(spec, &p, buf, 0));
	ASSERT_STR("keep", buf);
	ASSERT_EQUAL(IMG2_E_ARG, img2_yuv_parse_spec(spec, &p, msg, sizeof(msg)));
	ASSERT_TRUE(msg[0] != '\0');
}

CTEST(yuv_spec, errors_without_message_buffer)
{
	static char too_long[200];
	memset(too_long, '1', sizeof(too_long) - 1);
	const char *specs[] = {
		"",                                  /* empty */
		too_long,                            /* too long */
		"i420:1:1:1:1:1",                    /* too many fields */
		"i420:4",                            /* missing fields */
		":4:4",                              /* missing format */
		"bogus:4:4",                         /* unknown format */
		"i420:x:4",                          /* invalid number */
		"i420:4:99999999999",                /* out of range */
		"i420:4:4:0",                        /* y_stride 0 */
		"i420:4:4::0",                       /* uv_stride 0 */
		"i420:0:4",                          /* zero width */
		"i420:70000:4",                      /* larger than IMG2_MAX_DIMENSION */
		"i420:65536:65536",                  /* more than IMG2_MAX_PIXELS */
		"i420:4:4:2",                        /* y_stride below the minimum */
		"gray:4:4::2",                       /* uv_stride without chroma planes */
		"i420:4:4::1",                       /* uv_stride below the minimum */
		"i420:4:4:18446744073709551615",     /* frame size overflows */
	};
	for (size_t i = 0; i < sizeof(specs) / sizeof(specs[0]); i++) {
		spec_fails(specs[i]);
	}
	img2_yuv_params_t p;
	char buf[8] = "keep";
	ASSERT_EQUAL(IMG2_E_ARG, img2_yuv_parse_spec(NULL, &p, NULL, 0));
	ASSERT_EQUAL(IMG2_E_ARG, img2_yuv_parse_spec(NULL, &p, buf, 0));
	ASSERT_EQUAL(IMG2_E_ARG, img2_yuv_parse_spec("i420:4:4", NULL, buf, 0));
	ASSERT_STR("keep", buf);
}

CTEST(yuv_spec, frame_size_rejects_bad_geometry)
{
	size_t sz = 0;
	ASSERT_FALSE(img2_yuv_frame_size(NULL, &sz));
	const img2_yuv_params_t bad[] = {
		{ IMG2_YUV_FMT_I420, 0, 4, 0, 0 },
		{ IMG2_YUV_FMT_I420, 70000, 4, 0, 0 },
		{ IMG2_YUV_FMT_I420, 65536, 65536, 0, 0 },
		{ IMG2_YUV_FMT_NONE, 4, 4, 0, 0 },
		{ IMG2_YUV_FMT_I420, 4, 4, 2, 0 },
		{ IMG2_YUV_FMT_GRAY, 4, 4, 0, 2 },
		{ IMG2_YUV_FMT_I420, 4, 4, 0, 1 },
		{ IMG2_YUV_FMT_I420, 4, 4, SIZE_MAX, 0 },
	};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		ASSERT_FALSE(img2_yuv_frame_size(&bad[i], &sz));
	}
	const img2_yuv_params_t ok = { IMG2_YUV_FMT_I420, 4, 4, 0, 0 };
	ASSERT_TRUE(img2_yuv_frame_size(&ok, &sz));
	ASSERT_EQUAL(24, (int)sz);
}
