/**
 * @file test_cov_b_jpeg.c
 * @brief JPEG decoder coverage (decoder_jpeg.c): libjpeg errors after the output image is allocated
 *
 * A 12-bit JPEG (written in-test with libjpeg-turbo's 12-bit API) passes jpeg_read_header() and
 * jpeg_start_decompress(); the 8-bit jpeg_read_scanlines() then fails (JERR_BAD_PRECISION) and longjmps
 * back with the image and row buffer allocated. A second scan after a complete baseline scan fails in
 * jpeg_finish_decompress() the same way.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jpeglib.h>

#include "../../libimg2/decoders/decoder.h"
#include "test_cov_b_util.h"

/** 8x8 RGB JPEG with data_precision 12 (or 8); free() the result */
static uint8_t *jpeg_encode(int precision, unsigned long *len)
{
	struct jpeg_compress_struct c;
	struct jpeg_error_mgr e;
	c.err = jpeg_std_error(&e);
	jpeg_create_compress(&c);
	unsigned char *out = NULL;
	unsigned long out_len = 0;
	jpeg_mem_dest(&c, &out, &out_len);
	c.image_width = 8;
	c.image_height = 8;
	c.input_components = 3;
	c.in_color_space = JCS_RGB;
	jpeg_set_defaults(&c);
	c.data_precision = precision;
	jpeg_start_compress(&c, TRUE);
	if (precision == 12) {
		J12SAMPLE row[8 * 3];
		for (int i = 0; i < 8 * 3; i++) {
			row[i] = (J12SAMPLE)(i * 100);
		}
		J12SAMPROW rp[1] = { row };
		while (c.next_scanline < c.image_height) {
			jpeg12_write_scanlines(&c, rp, 1);
		}
	} else {
		JSAMPLE row[8 * 3];
		for (int i = 0; i < 8 * 3; i++) {
			row[i] = (JSAMPLE)(i * 10);
		}
		JSAMPROW rp[1] = { row };
		while (c.next_scanline < c.image_height) {
			jpeg_write_scanlines(&c, rp, 1);
		}
	}
	jpeg_finish_compress(&c);
	jpeg_destroy_compress(&c);
	*len = out_len;
	return out;
}

static img2_status_t jpeg_decode(const uint8_t *data, size_t len, int deblock, img2_frames_t *out)
{
	img2_decode_opts_t opts = IMG2_DECODE_OPTS_INIT;
	opts.jpeg_deblock = deblock;
	return img2_decode_jpeg(data, len, &opts, out);
}

/* Decode data `runs` times expecting `expected`; true if the heap grew by less than `slack` bytes */
static int jpeg_no_leak(const uint8_t *data, size_t len, int deblock, img2_status_t expected, int runs, size_t slack)
{
	img2_frames_t fr;
	img2_set_log_callback(cb_log_sink, NULL);
	/* warm-up: one-time allocations, and glibc's tcache (freed chunks it holds count as in use) */
	for (int i = 0; i < 8; i++) {
		if (jpeg_decode(data, len, deblock, &fr) != expected) {
			img2_set_log_callback(NULL, NULL);
			return 0;
		}
		img2_frames_free(&fr);
	}
	size_t before = cb_heap_in_use();
	int ok = 1;
	for (int i = 0; i < runs; i++) {
		if (jpeg_decode(data, len, deblock, &fr) != expected) {
			ok = 0;
		}
		img2_frames_free(&fr);
	}
	size_t after = cb_heap_in_use();
	img2_set_log_callback(NULL, NULL);
	if (!ok || after >= before + slack) {
		fprintf(stderr, "jpeg_no_leak: status ok %d, heap %zu -> %zu\n", ok, before, after);
	}
	return ok && after < before + slack;
}

CTEST(cov_b_jpeg, twelve_bit_fails_without_leaking)
{
	unsigned long len = 0;
	uint8_t *j12 = jpeg_encode(12, &len);
	ASSERT_NOT_NULL(j12);
	/* each leaked run: 8x8 RGBA image (256 bytes + struct) and a 24-byte row buffer */
	ASSERT_TRUE(jpeg_no_leak(j12, len, 0, IMG2_E_CODEC, 8, 2048));
	ASSERT_TRUE(jpeg_no_leak(j12, len, -1, IMG2_E_CODEC, 8, 2048));
	free(j12);
}

CTEST(cov_b_jpeg, many_clean_decodes)
{
	/* llvm-cov derives the counts after setjmp() by subtraction: every longjmp back (the error and
	 * allocation-failure tests) hides one execution of the paths after it; plain decodes with and without
	 * quantsmooth keep those counts positive */
	unsigned long len = 0;
	uint8_t *j8 = jpeg_encode(8, &len);
	ASSERT_NOT_NULL(j8);
	for (int i = 0; i < 1500; i++) {
		img2_frames_t fr;
		ASSERT_EQUAL(IMG2_OK, jpeg_decode(j8, len, i % 3 == 0 ? -1 : 0, &fr));
		ASSERT_EQUAL(1, fr.count);
		img2_frames_free(&fr);
	}
	free(j8);
}

CTEST(cov_b_jpeg, second_scan_fails_in_finish_without_leaking)
{
	unsigned long len = 0;
	uint8_t *j8 = jpeg_encode(8, &len);
	ASSERT_NOT_NULL(j8);
	/* splice a copy of the SOS segment + entropy data before EOI: a second scan in a sequential file */
	size_t sos = 0;
	for (size_t i = 2; i + 1 < len; i++) {
		if (j8[i] == 0xFF && j8[i + 1] == 0xDA) {
			sos = i;
			break;
		}
	}
	ASSERT_TRUE(sos > 0);
	size_t scan_len = (size_t)len - 2 - sos; /* SOS .. last entropy byte (EOI excluded) */
	cb_buf_t b = { 0 };
	cb_put(&b, j8, (size_t)len - 2);
	cb_put(&b, j8 + sos, scan_len);
	cb_put(&b, "\xFF\xD9", 2);
	ASSERT_TRUE(jpeg_no_leak(b.d, b.n, 0, IMG2_E_CODEC, 8, 2048));
	cb_free(&b);
	free(j8);
}

CTEST(cov_b_jpeg, argument_checks)
{
	const uint8_t one = 0xFF;
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_jpeg(&one, 1, NULL, NULL));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_jpeg(NULL, 1, NULL, &fr));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_jpeg(&one, 0, NULL, &fr));
}
