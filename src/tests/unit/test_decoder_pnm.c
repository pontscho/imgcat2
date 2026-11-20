/**
 * @file test_decoder_pnm.c
 * @brief Unit tests for the native Netpbm (PBM/PGM/PPM/PAM) decoder
 *
 * Tests decode_netpbm() using ctest.h framework with inline byte arrays
 * covering P1-P7, 8/16-bit samples, comments, padding and malformed input.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../imgcat2/core/image.h"
#include "../../imgcat2/decoders/decoder.h"
#include "../ctest.h"
#include "../decoder_internal.h"

/** Decode a string literal (excluding the implicit trailing NUL) */
#define DECODE_LIT(lit, fc) decode_netpbm((const uint8_t *)(lit), sizeof(lit) - 1, (fc))

/** Assert RGBA value of pixel (x, y) */
#define ASSERT_PX(img, x, y, r, g, b, a)                     \
	do {                                                     \
		const uint8_t *px_ = image_get_pixel((img), (x), (y)); \
		ASSERT_NOT_NULL(px_);                                \
		ASSERT_EQUAL((r), px_[0]);                           \
		ASSERT_EQUAL((g), px_[1]);                           \
		ASSERT_EQUAL((b), px_[2]);                           \
		ASSERT_EQUAL((a), px_[3]);                           \
	} while (0)

/**
 * @test decode_netpbm() rejects NULL / empty parameters
 */
CTEST(decoder_pnm, null_parameters)
{
	int frame_count;
	uint8_t dummy[10] = { 'P', '5', '\n' };

	ASSERT_NULL(decode_netpbm(NULL, 100, &frame_count));
	ASSERT_NULL(decode_netpbm(dummy, 0, &frame_count));
	ASSERT_NULL(decode_netpbm(dummy, sizeof(dummy), NULL));
}

/**
 * @test decode_netpbm() rejects non-Netpbm data
 */
CTEST(decoder_pnm, garbage)
{
	int frame_count = -1;
	ASSERT_NULL(DECODE_LIT("This is not a valid image file", &frame_count));
	ASSERT_EQUAL(0, frame_count);

	/* P8 is not a Netpbm format, P6 without whitespace is invalid */
	ASSERT_NULL(DECODE_LIT("P8\n1 1\n255\n\x00", &frame_count));
	ASSERT_NULL(DECODE_LIT("P6x1 1\n255\n\x00\x00\x00", &frame_count));
}

/**
 * @test P1 ASCII bitmap with comments and no whitespace between bits
 */
CTEST(decoder_pnm, p1_ascii_comments)
{
	int frame_count;
	image_t **frames = DECODE_LIT("P1\n# comment line\n2 # width\n2\n1 0\n# mid-raster comment\n01", &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_EQUAL(2, frames[0]->width);
	ASSERT_EQUAL(2, frames[0]->height);

	/* PBM: 1 = black, 0 = white */
	ASSERT_PX(frames[0], 0, 0, 0, 0, 0, 255);
	ASSERT_PX(frames[0], 1, 0, 255, 255, 255, 255);
	ASSERT_PX(frames[0], 0, 1, 255, 255, 255, 255);
	ASSERT_PX(frames[0], 1, 1, 0, 0, 0, 255);

	decoder_free_frames(frames, frame_count);
}

/**
 * @test P2 ASCII graymap with maxval 15 is scaled to 8 bit with rounding
 */
CTEST(decoder_pnm, p2_maxval_scaling)
{
	int frame_count;
	image_t **frames = DECODE_LIT("P2\n4 1\n15\n0 7 15 20\n", &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_EQUAL(4, frames[0]->width);
	ASSERT_EQUAL(1, frames[0]->height);

	ASSERT_PX(frames[0], 0, 0, 0, 0, 0, 255);
	ASSERT_PX(frames[0], 1, 0, 119, 119, 119, 255); /* (7*255+7)/15 = 119 */
	ASSERT_PX(frames[0], 2, 0, 255, 255, 255, 255);
	ASSERT_PX(frames[0], 3, 0, 255, 255, 255, 255); /* 20 > maxval: clamped */

	decoder_free_frames(frames, frame_count);
}

/**
 * @test P3 ASCII pixmap
 */
CTEST(decoder_pnm, p3_ascii_rgb)
{
	int frame_count;
	image_t **frames = DECODE_LIT("P3\n2 1\n255\n255 0 0\n  0 128 255\n", &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_PX(frames[0], 0, 0, 255, 0, 0, 255);
	ASSERT_PX(frames[0], 1, 0, 0, 128, 255, 255);

	decoder_free_frames(frames, frame_count);
}

/**
 * @test P4 packed bitmap, width 10 (rows padded to 2 bytes, padding bits ignored)
 */
CTEST(decoder_pnm, p4_row_padding)
{
	/* Row 0: 1000000001 (+ padding 111111), Row 1: 0101010101 (+ padding 000000) */
	int frame_count;
	image_t **frames = DECODE_LIT("P4\n10 2\n"
	                              "\x80\x7F"
	                              "\x55\x40",
	                              &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_EQUAL(10, frames[0]->width);
	ASSERT_EQUAL(2, frames[0]->height);

	static const int row0[10] = { 1, 0, 0, 0, 0, 0, 0, 0, 0, 1 };
	static const int row1[10] = { 0, 1, 0, 1, 0, 1, 0, 1, 0, 1 };
	for (uint32_t x = 0; x < 10; x++) {
		uint8_t v0 = row0[x] ? 0 : 255;
		uint8_t v1 = row1[x] ? 0 : 255;
		ASSERT_PX(frames[0], x, 0, v0, v0, v0, 255);
		ASSERT_PX(frames[0], x, 1, v1, v1, v1, 255);
	}

	decoder_free_frames(frames, frame_count);
}

/**
 * @test P5 binary 8-bit graymap
 */
CTEST(decoder_pnm, p5_8bit)
{
	int frame_count;
	image_t **frames = DECODE_LIT("P5\n2 2\n255\n"
	                              "\x00\x7F\xAF\xFF",
	                              &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_PX(frames[0], 0, 0, 0x00, 0x00, 0x00, 255);
	ASSERT_PX(frames[0], 1, 0, 0x7F, 0x7F, 0x7F, 255);
	ASSERT_PX(frames[0], 0, 1, 0xAF, 0xAF, 0xAF, 255);
	ASSERT_PX(frames[0], 1, 1, 0xFF, 0xFF, 0xFF, 255);

	decoder_free_frames(frames, frame_count);
}

/**
 * @test P5 binary 16-bit graymap (maxval 65535, big-endian samples)
 */
CTEST(decoder_pnm, p5_16bit_big_endian)
{
	int frame_count;
	image_t **frames = DECODE_LIT("P5\n3 1\n65535\n"
	                              "\x00\x00"
	                              "\x80\x00"
	                              "\xFF\xFF",
	                              &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_PX(frames[0], 0, 0, 0, 0, 0, 255);
	ASSERT_PX(frames[0], 1, 0, 128, 128, 128, 255); /* 0x8000 -> 128 */
	ASSERT_PX(frames[0], 2, 0, 255, 255, 255, 255);

	decoder_free_frames(frames, frame_count);
}

/**
 * @test P6 binary pixmap
 */
CTEST(decoder_pnm, p6_binary_rgb)
{
	int frame_count;
	image_t **frames = DECODE_LIT("P6\n# comment\n2 1\n255\n"
	                              "\xFF\x00\x00"
	                              "\x00\x80\xFF",
	                              &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_PX(frames[0], 0, 0, 255, 0, 0, 255);
	ASSERT_PX(frames[0], 1, 0, 0, 128, 255, 255);

	decoder_free_frames(frames, frame_count);
}

/**
 * @test Only the first image of a multi-image stream is decoded
 */
CTEST(decoder_pnm, multi_image_first_only)
{
	int frame_count;
	image_t **frames = DECODE_LIT("P5\n1 1\n255\n"
	                              "\x40"
	                              "P5\n2 2\n255\n"
	                              "\x00\x00\x00\x00",
	                              &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_EQUAL(1, frames[0]->width);
	ASSERT_EQUAL(1, frames[0]->height);
	ASSERT_PX(frames[0], 0, 0, 0x40, 0x40, 0x40, 255);

	decoder_free_frames(frames, frame_count);
}

/**
 * @test P7 PAM GRAYSCALE_ALPHA
 */
CTEST(decoder_pnm, p7_grayscale_alpha)
{
	int frame_count;
	image_t **frames = DECODE_LIT("P7\nWIDTH 2\nHEIGHT 1\nDEPTH 2\nMAXVAL 255\nTUPLTYPE GRAYSCALE_ALPHA\nENDHDR\n"
	                              "\x10\x80"
	                              "\xF0\xFF",
	                              &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_EQUAL(2, frames[0]->width);
	ASSERT_EQUAL(1, frames[0]->height);
	ASSERT_PX(frames[0], 0, 0, 0x10, 0x10, 0x10, 0x80);
	ASSERT_PX(frames[0], 1, 0, 0xF0, 0xF0, 0xF0, 0xFF);

	decoder_free_frames(frames, frame_count);
}

/**
 * @test P7 PAM RGB_ALPHA with comments
 */
CTEST(decoder_pnm, p7_rgb_alpha)
{
	int frame_count;
	image_t **frames = DECODE_LIT("P7\n# PAM comment\nWIDTH 1\nHEIGHT 1\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n"
	                              "\x01\x02\x03\x04",
	                              &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_PX(frames[0], 0, 0, 1, 2, 3, 4);

	decoder_free_frames(frames, frame_count);
}

/**
 * @test P7 PAM without TUPLTYPE infers layout from DEPTH
 */
CTEST(decoder_pnm, p7_missing_tupltype)
{
	int frame_count;
	image_t **frames = DECODE_LIT("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 3\nMAXVAL 255\nENDHDR\n"
	                              "\x0A\x14\x1E",
	                              &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_PX(frames[0], 0, 0, 10, 20, 30, 255);

	decoder_free_frames(frames, frame_count);
}

/**
 * @test P7 PAM BLACKANDWHITE: 0 = black, 1 = white (opposite of PBM)
 */
CTEST(decoder_pnm, p7_blackandwhite)
{
	int frame_count;
	image_t **frames = DECODE_LIT("P7\nWIDTH 2\nHEIGHT 1\nDEPTH 1\nMAXVAL 1\nTUPLTYPE BLACKANDWHITE\nENDHDR\n"
	                              "\x00\x01",
	                              &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_PX(frames[0], 0, 0, 0, 0, 0, 255);
	ASSERT_PX(frames[0], 1, 0, 255, 255, 255, 255);

	decoder_free_frames(frames, frame_count);
}

/**
 * @test Malformed PAM headers are rejected
 */
CTEST(decoder_pnm, p7_invalid_headers)
{
	int frame_count;

	/* TUPLTYPE / DEPTH mismatch */
	ASSERT_NULL(DECODE_LIT("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 3\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n\x01\x02\x03\x04", &frame_count));
	/* Unsupported depth */
	ASSERT_NULL(DECODE_LIT("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 5\nMAXVAL 255\nENDHDR\n\x01\x02\x03\x04\x05", &frame_count));
	/* Missing ENDHDR */
	ASSERT_NULL(DECODE_LIT("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\n", &frame_count));
	/* Missing MAXVAL */
	ASSERT_NULL(DECODE_LIT("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 1\nENDHDR\n\x01", &frame_count));
	/* Unknown header keyword */
	ASSERT_NULL(DECODE_LIT("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\nFOO 1\nENDHDR\n\x01", &frame_count));
	ASSERT_EQUAL(0, frame_count);
}

/**
 * @test Truncated raster data is rejected
 */
CTEST(decoder_pnm, truncated_raster)
{
	int frame_count;

	/* P6 2x2 needs 12 bytes, only 11 present */
	ASSERT_NULL(DECODE_LIT("P6\n2 2\n255\n\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B", &frame_count));
	ASSERT_EQUAL(0, frame_count);
	/* P5 16-bit 2x1 needs 4 bytes */
	ASSERT_NULL(DECODE_LIT("P5\n2 1\n65535\n\x01\x02\x03", &frame_count));
	/* P4 10x2 needs 4 bytes */
	ASSERT_NULL(DECODE_LIT("P4\n10 2\n\x80\x7F\x55", &frame_count));
	/* P2 ASCII missing last sample */
	ASSERT_NULL(DECODE_LIT("P2\n2 2\n255\n1 2 3     ", &frame_count));
	/* P1 ASCII with invalid bit character */
	ASSERT_NULL(DECODE_LIT("P1\n2 1\n1 2", &frame_count));
	/* Header only */
	ASSERT_NULL(DECODE_LIT("P6\n2 2", &frame_count));
}

/**
 * @test Zero width / height is rejected
 */
CTEST(decoder_pnm, zero_dimensions)
{
	int frame_count;
	ASSERT_NULL(DECODE_LIT("P5\n0 1\n255\n\x00", &frame_count));
	ASSERT_NULL(DECODE_LIT("P5\n1 0\n255\n\x00", &frame_count));
	ASSERT_NULL(DECODE_LIT("P7\nWIDTH 0\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\nENDHDR\n\x00", &frame_count));
}

/**
 * @test maxval outside 1..65535 is rejected
 */
CTEST(decoder_pnm, invalid_maxval)
{
	int frame_count;
	ASSERT_NULL(DECODE_LIT("P5\n1 1\n0\n\x00", &frame_count));
	ASSERT_NULL(DECODE_LIT("P5\n1 1\n70000\n\x00\x00", &frame_count));
	ASSERT_NULL(DECODE_LIT("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 1\nMAXVAL 70000\nENDHDR\n\x00\x00", &frame_count));
}

/**
 * @test Huge / overflowing dimensions are rejected without crashing
 */
CTEST(decoder_pnm, huge_dimensions)
{
	int frame_count;
	ASSERT_NULL(DECODE_LIT("P5\n100000 100000\n255\n\x00\x00\x00\x00", &frame_count));
	ASSERT_NULL(DECODE_LIT("P6\n65536 65536\n65535\n\x00\x00\x00\x00", &frame_count));
	ASSERT_NULL(DECODE_LIT("P1\n65536 65536\n0 1 0 1", &frame_count));
	ASSERT_NULL(DECODE_LIT("P5\n99999999999999999999 1\n255\n\x00", &frame_count));
	ASSERT_NULL(DECODE_LIT("P7\nWIDTH 4294967295\nHEIGHT 4294967295\nDEPTH 4\nMAXVAL 65535\nENDHDR\n\x00", &frame_count));
	ASSERT_EQUAL(0, frame_count);
}

/**
 * @test PNM decoding through the decoder registry
 */
CTEST(decoder_pnm, decode_through_registry)
{
	decoder_registry_init(NULL);

	const decoder_t *decoder = decoder_find_by_mime(MIME_PNM);
	ASSERT_NOT_NULL(decoder);
	ASSERT_EQUAL(MIME_PNM, decoder->mime_type);
	ASSERT_STR("PNM/PAM (native)", decoder->name);

	static const char pgm[] = "P2\n2 1\n255\n10 200\n";
	int frame_count;
	image_t **frames = decoder_decode(NULL, (const uint8_t *)pgm, sizeof(pgm) - 1, MIME_PNM, &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_EQUAL(2, frames[0]->width);
	ASSERT_EQUAL(1, frames[0]->height);
	ASSERT_PX(frames[0], 0, 0, 10, 10, 10, 255);
	ASSERT_PX(frames[0], 1, 0, 200, 200, 200, 255);

	decoder_free_frames(frames, frame_count);
}
