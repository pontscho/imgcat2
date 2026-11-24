/**
 * @file test_decoder_jpeg.c
 * @brief Unit tests for JPEG decoder
 *
 * Tests JPEG decoding functionality using ctest.h framework.
 * Tests baseline JPEG, progressive JPEG, and grayscale JPEG formats.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <img2/img2.h>

#include "../../libimg2/core/image.h"
#include "../../libimg2/decoders/decoder.h"
#include "../ctest.h"
#include "../decoder_internal.h"

/**
 * @test Test img2_decode_jpeg() with NULL parameters
 *
 * Verifies that img2_decode_jpeg() rejects NULL input gracefully.
 */
CTEST(decoder_jpeg, null_parameters)
{
	int frame_count;
	uint8_t dummy_data[10] = { 0 };

	/* NULL data pointer */
	img2_image_t **frames = test_decode(img2_decode_jpeg, NULL, 100, &frame_count);
	ASSERT_NULL(frames);

	/* Zero length */
	frames = test_decode(img2_decode_jpeg, dummy_data, 0, &frame_count);
	ASSERT_NULL(frames);

	/* NULL frame_count */
	frames = test_decode(img2_decode_jpeg, dummy_data, 10, NULL);
	ASSERT_NULL(frames);
}

/**
 * @test Test img2_decode_jpeg() with invalid JPEG data
 *
 * Verifies that img2_decode_jpeg() rejects invalid data.
 */
CTEST(decoder_jpeg, invalid_data)
{
	int frame_count;
	uint8_t invalid_data[] = "This is not a JPEG file";

	img2_image_t **frames = test_decode(img2_decode_jpeg, invalid_data, sizeof(invalid_data), &frame_count);
	ASSERT_NULL(frames);
	ASSERT_EQUAL(0, frame_count);
}

/**
 * @test Test img2_decode_jpeg() with minimal 1x1 JPEG
 *
 * Minimal valid JPEG: 1x1 pixel baseline JPEG
 */
CTEST(decoder_jpeg, decode_minimal_1x1)
{
	/* Minimal 1x1 baseline JPEG (134 bytes) - black pixel */
	static const uint8_t jpeg_1x1[] = {
		0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, /* JPEG SOI + APP0 (JFIF) */
		0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43, /* DQT (quantization table) */
		0x00, 0x08, 0x06, 0x06, 0x07, 0x06, 0x05, 0x08, 0x07, 0x07, 0x07, 0x09, 0x09, 0x08, 0x0A, 0x0C, 0x14, 0x0D, 0x0C, 0x0B, 0x0B, 0x0C, 0x19, 0x12, 0x13, 0x0F, 0x14, 0x1D, 0x1A, 0x1F, 0x1E, 0x1D, 0x1A, 0x1C, 0x1C, 0x20, 0x24, 0x2E, 0x27, 0x20, 0x22,
		0x2C, 0x23, 0x1C, 0x1C, 0x28, 0x37, 0x29, 0x2C, 0x30, 0x31, 0x34, 0x34, 0x34, 0x1F, 0x27, 0x39, 0x3D, 0x38, 0x32, 0x3C, 0x2E, 0x33, 0x34, 0x32, 0xFF, 0xC0, 0x00, 0x0B, 0x08, 0x00, 0x01, /* SOF0 (Start of Frame) */
		0x00, 0x01, 0x01, 0x01, 0x11, 0x00, 0xFF, 0xC4, /* 1x1 dimensions */
		0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, /* DHT (Huffman table) */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xC4, 0x00, 0x14, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, /* SOS (Start of Scan)
		                                                                                                                                                                                                                                                 */
		0x00, 0x00, 0x3F, 0x00, 0xD2, 0xFF, 0xD9 /* Scan data + EOI */
	};

	int frame_count;
	img2_image_t **frames = test_decode(img2_decode_jpeg, jpeg_1x1, sizeof(jpeg_1x1), &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);
	ASSERT_EQUAL(1, frames[0]->width);
	ASSERT_EQUAL(1, frames[0]->height);
	ASSERT_NOT_NULL(frames[0]->pixels);

	/* JPEG has no alpha channel - should be converted to RGBA with alpha=255 */
	uint8_t *pixel = img2_image_get_pixel(frames[0], 0, 0);
	ASSERT_NOT_NULL(pixel);
	ASSERT_EQUAL(255, pixel[3]); /* Alpha should be opaque */

	img2_decoder_free_frames(frames, frame_count);
}

/**
 * @test Test img2_decode_jpeg() with 2x2 JPEG
 *
 * Tests basic JPEG decoding with a small 2x2 image.
 */
CTEST(decoder_jpeg, decode_2x2)
{
	/* Minimal 2x2 baseline JPEG */
	static const uint8_t jpeg_2x2[] = { 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43, 0x00, 0x08, 0x06, 0x06, 0x07, 0x06, 0x05, 0x08,
		                                0x07, 0x07, 0x07, 0x09, 0x09, 0x08, 0x0A, 0x0C, 0x14, 0x0D, 0x0C, 0x0B, 0x0B, 0x0C, 0x19, 0x12, 0x13, 0x0F, 0x14, 0x1D, 0x1A, 0x1F, 0x1E, 0x1D, 0x1A, 0x1C, 0x1C, 0x20, 0x24, 0x2E, 0x27, 0x20,
		                                0x22, 0x2C, 0x23, 0x1C, 0x1C, 0x28, 0x37, 0x29, 0x2C, 0x30, 0x31, 0x34, 0x34, 0x34, 0x1F, 0x27, 0x39, 0x3D, 0x38, 0x32, 0x3C, 0x2E, 0x33, 0x34, 0x32, 0xFF, 0xC0, 0x00, 0x0B, 0x08, 0x00, 0x02,
		                                0x00, 0x02, 0x01, 0x01, 0x11, 0x00, 0xFF, 0xC4, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xC4, 0x00, 0x14,
		                                0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x3F, 0x00, 0xD2, 0xFF, 0xD9 };

	int frame_count;
	img2_image_t **frames = test_decode(img2_decode_jpeg, jpeg_2x2, sizeof(jpeg_2x2), &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);
	ASSERT_EQUAL(2, frames[0]->width);
	ASSERT_EQUAL(2, frames[0]->height);
	ASSERT_NOT_NULL(frames[0]->pixels);

	/* Verify all 4 pixels exist and have opaque alpha */
	for (uint32_t y = 0; y < 2; y++) {
		for (uint32_t x = 0; x < 2; x++) {
			uint8_t *pixel = img2_image_get_pixel(frames[0], x, y);
			ASSERT_NOT_NULL(pixel);
			ASSERT_EQUAL(255, pixel[3]); /* Alpha=255 */
		}
	}

	img2_decoder_free_frames(frames, frame_count);
}

/**
 * @test Test img2_decoder_free_frames() with NULL
 *
 * Verifies that img2_decoder_free_frames() handles NULL gracefully.
 */
CTEST(decoder_jpeg, free_frames_null)
{
	/* Should not crash */
	img2_decoder_free_frames(NULL, 0);
	img2_decoder_free_frames(NULL, 10);
}

/**
 * @test Test JPEG decoder through decoder registry
 *
 * Verifies that JPEG can be decoded through the img2_decoder_dispatch() API.
 */
CTEST(decoder_jpeg, decode_through_registry)
{
	/* Initialize decoder registry */
	img2_decoder_registry_init();

	/* Minimal 1x1 JPEG */
	static const uint8_t jpeg_1x1[] = { 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43, 0x00, 0x08, 0x06, 0x06, 0x07, 0x06, 0x05, 0x08,
		                                0x07, 0x07, 0x07, 0x09, 0x09, 0x08, 0x0A, 0x0C, 0x14, 0x0D, 0x0C, 0x0B, 0x0B, 0x0C, 0x19, 0x12, 0x13, 0x0F, 0x14, 0x1D, 0x1A, 0x1F, 0x1E, 0x1D, 0x1A, 0x1C, 0x1C, 0x20, 0x24, 0x2E, 0x27, 0x20,
		                                0x22, 0x2C, 0x23, 0x1C, 0x1C, 0x28, 0x37, 0x29, 0x2C, 0x30, 0x31, 0x34, 0x34, 0x34, 0x1F, 0x27, 0x39, 0x3D, 0x38, 0x32, 0x3C, 0x2E, 0x33, 0x34, 0x32, 0xFF, 0xC0, 0x00, 0x0B, 0x08, 0x00, 0x01,
		                                0x00, 0x01, 0x01, 0x01, 0x11, 0x00, 0xFF, 0xC4, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xC4, 0x00, 0x14,
		                                0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x3F, 0x00, 0xD2, 0xFF, 0xD9 };

	int frame_count;
	img2_image_t **frames = test_dispatch(NULL, jpeg_1x1, sizeof(jpeg_1x1), IMG2_MIME_JPEG, &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);
	ASSERT_EQUAL(1, frames[0]->width);
	ASSERT_EQUAL(1, frames[0]->height);

	img2_decoder_free_frames(frames, frame_count);
}

/**
 * @test Test img2_decoder_find() for JPEG
 *
 * Verifies that JPEG decoder can be found in registry.
 */
CTEST(decoder_jpeg, find_by_mime)
{
	img2_decoder_registry_init();

	const img2_decoder_entry_t *decoder = img2_decoder_find(IMG2_MIME_JPEG);
	ASSERT_NOT_NULL(decoder);
	ASSERT_EQUAL(IMG2_MIME_JPEG, decoder->info.mime);
	ASSERT_NOT_NULL(decoder->info.name);
	ASSERT_NOT_NULL(decoder->decode);
}

/**
 * @test Test that JPEG frames always return 1 frame
 *
 * JPEG does not support animation, should always return 1 frame.
 */
CTEST(decoder_jpeg, single_frame_only)
{
	static const uint8_t jpeg_1x1[] = { 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43, 0x00, 0x08, 0x06, 0x06, 0x07, 0x06, 0x05, 0x08,
		                                0x07, 0x07, 0x07, 0x09, 0x09, 0x08, 0x0A, 0x0C, 0x14, 0x0D, 0x0C, 0x0B, 0x0B, 0x0C, 0x19, 0x12, 0x13, 0x0F, 0x14, 0x1D, 0x1A, 0x1F, 0x1E, 0x1D, 0x1A, 0x1C, 0x1C, 0x20, 0x24, 0x2E, 0x27, 0x20,
		                                0x22, 0x2C, 0x23, 0x1C, 0x1C, 0x28, 0x37, 0x29, 0x2C, 0x30, 0x31, 0x34, 0x34, 0x34, 0x1F, 0x27, 0x39, 0x3D, 0x38, 0x32, 0x3C, 0x2E, 0x33, 0x34, 0x32, 0xFF, 0xC0, 0x00, 0x0B, 0x08, 0x00, 0x01,
		                                0x00, 0x01, 0x01, 0x01, 0x11, 0x00, 0xFF, 0xC4, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xC4, 0x00, 0x14,
		                                0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x3F, 0x00, 0xD2, 0xFF, 0xD9 };

	int frame_count = -1;
	img2_image_t **frames = test_decode(img2_decode_jpeg, jpeg_1x1, sizeof(jpeg_1x1), &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count); /* JPEG is always single-frame */

	img2_decoder_free_frames(frames, frame_count);
}

/**
 * @test S6 status codes: truncated JPEG -> IMG2_E_CODEC, NULL buffer -> IMG2_E_ARG,
 *       IMG2_MIME_UNKNOWN through the dispatcher -> IMG2_E_FORMAT; *out zeroed on error
 */
CTEST(decoder_jpeg, status_codes)
{
	/* SOI + APP0 (JFIF) + the start of a DQT segment, then nothing */
	static const uint8_t truncated[] = {
		0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01,
		0x00, 0x01, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43, 0x00, 0x08, 0x06, 0x06,
	};

	img2_frames_t fr = { .frames = (img2_image_t **)1, .count = 7, .loop_count = 3 };
	ASSERT_EQUAL(IMG2_E_CODEC, img2_decode_jpeg(truncated, sizeof(truncated), NULL, &fr));
	ASSERT_NULL(fr.frames);
	ASSERT_EQUAL(0, fr.count);
	ASSERT_EQUAL(0, fr.loop_count);

	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_jpeg(NULL, 100, NULL, &fr));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decoder_dispatch(NULL, NULL, 100, IMG2_MIME_JPEG, &fr));
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_decoder_dispatch(NULL, truncated, sizeof(truncated), IMG2_MIME_UNKNOWN, &fr));
	ASSERT_NULL(fr.frames);

	/* const table: no init needed */
	ASSERT_TRUE(img2_decoder_count() > 0);
	ASSERT_NOT_NULL(img2_decoder_info(0));
	ASSERT_NULL(img2_decoder_info(img2_decoder_count()));
	ASSERT_TRUE(img2_can_decode(IMG2_MIME_YUV));
	ASSERT_FALSE(img2_can_decode(IMG2_MIME_UNKNOWN));
	ASSERT_FALSE(img2_is_animated(NULL, 10));
	ASSERT_FALSE(img2_is_animated(truncated, sizeof(truncated)));

	/* jpeg_deblock = 0 (plain libjpeg) still decodes */
	img2_decode_opts_t opts = { .force_mime = IMG2_MIME_UNKNOWN, .yuv = NULL, .max_frames = 0, .jpeg_deblock = 0 };
	ASSERT_EQUAL(IMG2_E_CODEC, img2_decode_jpeg(truncated, sizeof(truncated), &opts, &fr));
}

/* width x height image encoded as fmt (progressive if asked); free with img2_free() */
static uint8_t *probe_encode(uint32_t width, uint32_t height, img2_format_t fmt, bool progressive, size_t *len)
{
	img2_image_t *img = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_create(width, height, &img));
	img2_frames_t one = { &img, 1, 0 };
	img2_encode_opts_t eo = IMG2_ENCODE_OPTS_INIT;
	eo.progressive = progressive;
	uint8_t *buf = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_encode(&one, fmt, &eo, &buf, len));
	img2_image_destroy(img);
	return buf;
}

/* *probe filled with garbage, so the tests see that an error zeroes it */
static void probe_poison(img2_probe_info_t *info)
{
	memset(info, 0xA5, sizeof(*info));
}

static void assert_probe_zeroed(const img2_probe_info_t *info)
{
	ASSERT_EQUAL(IMG2_MIME_UNKNOWN, info->mime);
	ASSERT_EQUAL(0, info->width);
	ASSERT_EQUAL(0, info->height);
	ASSERT_EQUAL(0, info->orientation);
}

/**
 * @test img2_probe(): JPEG header size without a pixel decode (baseline, progressive), the img2_decode() input
 *       checks and limits, errors zero *out, a non-JPEG type through the full-decode fallback
 */
CTEST(decoder_jpeg, probe)
{
	img2_probe_info_t info;
	size_t len = 0;

	uint8_t *jpg = probe_encode(37, 23, IMG2_FORMAT_JPEG, false, &len);
	probe_poison(&info);
	ASSERT_EQUAL(IMG2_OK, img2_probe(jpg, len, &info));
	ASSERT_EQUAL(IMG2_MIME_JPEG, info.mime);
	ASSERT_EQUAL(37, info.width);
	ASSERT_EQUAL(23, info.height);
	ASSERT_EQUAL(1, info.orientation);

	/* only the header is read: a file cut after the first scan header still probes */
	ASSERT_EQUAL(IMG2_OK, img2_probe(jpg, len - 4, &info));
	ASSERT_EQUAL(37, info.width);
	img2_free(jpg);

	uint8_t *prog = probe_encode(64, 40, IMG2_FORMAT_JPEG, true, &len);
	ASSERT_EQUAL(IMG2_OK, img2_probe(prog, len, &info));
	ASSERT_EQUAL(IMG2_MIME_JPEG, info.mime);
	ASSERT_EQUAL(64, info.width);
	ASSERT_EQUAL(40, info.height);
	img2_free(prog);

	/* truncated before the frame header: libjpeg error -> IMG2_E_CODEC, *out zeroed */
	static const uint8_t truncated[] = {
		0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01,
		0x00, 0x01, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43, 0x00, 0x08, 0x06, 0x06,
	};
	probe_poison(&info);
	ASSERT_EQUAL(IMG2_E_CODEC, img2_probe(truncated, sizeof(truncated), &info));
	assert_probe_zeroed(&info);

	/* garbage: no type -> IMG2_E_FORMAT */
	static const uint8_t garbage[] = "This is not a JPEG file";
	probe_poison(&info);
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_probe(garbage, sizeof(garbage), &info));
	assert_probe_zeroed(&info);

	/* the img2_decode() argument checks */
	probe_poison(&info);
	ASSERT_EQUAL(IMG2_E_ARG, img2_probe(NULL, 10, &info));
	assert_probe_zeroed(&info);
	ASSERT_EQUAL(IMG2_E_ARG, img2_probe(garbage, 0, &info));
	ASSERT_EQUAL(IMG2_E_ARG, img2_probe(garbage, sizeof(garbage), NULL));
	ASSERT_EQUAL(IMG2_E_LIMIT, img2_probe(garbage, (size_t)IMG2_MAX_FILE_SIZE + 1, &info));

	/* a 20000x20000 frame header (4e8 pixels > IMG2_MAX_PIXELS) -> IMG2_E_LIMIT, as img2_decode() */
	uint8_t *big = probe_encode(8, 8, IMG2_FORMAT_JPEG, false, &len);
	bool patched = false;
	for (size_t i = 0; i + 8 < len; i++) {
		if (big[i] == 0xFF && big[i + 1] == 0xC0) { /* SOF0: length(2) precision(1) height(2) width(2) */
			big[i + 5] = 0x4E;
			big[i + 6] = 0x20;
			big[i + 7] = 0x4E;
			big[i + 8] = 0x20;
			patched = true;
			break;
		}
	}
	ASSERT_TRUE(patched);
	probe_poison(&info);
	ASSERT_EQUAL(IMG2_E_LIMIT, img2_probe(big, len, &info));
	assert_probe_zeroed(&info);
	img2_free(big);

	/* non-JPEG: the full decode */
	uint8_t *png = probe_encode(5, 3, IMG2_FORMAT_PNG, false, &len);
	probe_poison(&info);
	ASSERT_EQUAL(IMG2_OK, img2_probe(png, len, &info));
	ASSERT_EQUAL(IMG2_MIME_PNG, info.mime);
	ASSERT_EQUAL(5, info.width);
	ASSERT_EQUAL(3, info.height);
	ASSERT_EQUAL(1, info.orientation);
	img2_free(png);
}

/**
 * @test img2_probe() of a camera JPEG with EXIF orientation 6: the stored size and the orientation still to
 *       apply, both as img2_decode() reports them
 */
CTEST(decoder_jpeg, probe_orientation_matches_decode)
{
	FILE *fp = fopen(IMG2_TEST_SRC_DIR "/data/jpg/canon_hdr_NO.jpg", "rb");
	ASSERT_NOT_NULL(fp);
	uint8_t *data = malloc(1 << 22);
	ASSERT_NOT_NULL(data);
	size_t len = fread(data, 1, 1 << 22, fp);
	fclose(fp);
	ASSERT_TRUE(len > 0);

	img2_probe_info_t info;
	ASSERT_EQUAL(IMG2_OK, img2_probe(data, len, &info));

	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, img2_decode(data, len, NULL, &fr));
	ASSERT_EQUAL(IMG2_MIME_JPEG, info.mime);
	ASSERT_EQUAL(fr.frames[0]->width, info.width);
	ASSERT_EQUAL(fr.frames[0]->height, info.height);
	ASSERT_EQUAL(fr.frames[0]->orientation, info.orientation);
#if IMG2_HAS_EXIF
	ASSERT_EQUAL(6, info.orientation);
#endif
	img2_frames_free(&fr);
	free(data);
}
