/**
 * @file test_jpeg_log_raw.c
 * @brief LibRaw data errors routed through img2_log (decoders/decoder_raw.c), never written to stderr by LibRaw
 *
 * LibRaw's default data error callback prints "<file>: Unexpected end of file" or "<file>: data corrupted at N"
 * to stderr. Two DNGs written in-test trigger it: an uncompressed raw strip that starts 64 bytes before the end of
 * the file (end of data while reading it), and a lossless-JPEG raw (compression 7, encoded in-test with
 * libjpeg-turbo) with a marker-like FF D0 pair inside its entropy-coded data (corrupt data before the end).
 * fd 2 is captured through a pipe: nothing may reach it, the log callback gets the text at IMG2_LOG_WARN.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <jpeglib.h>

#include "../../libimg2/core/log.h"
#include "../../libimg2/decoders/decoder.h"
#include "test_cov_b_util.h"
#include "test_cov_b_dng.h"

#define MAX_MSGS 8

typedef struct {
	int calls;
	img2_log_level_t level[MAX_MSGS];
	char msg[MAX_MSGS][256];
} rlog_capture_t;

static void rlog_cb(img2_log_level_t level, const char *msg, void *user)
{
	rlog_capture_t *cap = (rlog_capture_t *)user;
	if (cap->calls < MAX_MSGS) {
		cap->level[cap->calls] = level;
		snprintf(cap->msg[cap->calls], sizeof(cap->msg[0]), "%s", msg);
	}
	cap->calls++;
}

/**
 * Decode b as RAW with the capture callback at IMG2_LOG_WARN, fd 2 redirected into a pipe (never a file under
 * /tmp); returns the number of bytes written to fd 2 meanwhile
 */
static size_t raw_decode_captured(const cb_buf_t *b, rlog_capture_t *cap, img2_status_t *st)
{
	img2_log_level_t saved = img2_get_log_level();
	img2_set_log_level(IMG2_LOG_WARN);
	img2_set_log_callback(rlog_cb, cap);

	int fds[2];
	if (pipe(fds) != 0) {
		abort();
	}
	fflush(stderr);
	int saved_fd = dup(STDERR_FILENO);
	if (saved_fd < 0 || dup2(fds[1], STDERR_FILENO) < 0) {
		abort();
	}
	close(fds[1]);

	img2_frames_t fr = { 0 };
	*st = img2_decode_raw(b->d, b->n, NULL, &fr);
	img2_frames_free(&fr);

	fflush(stderr);
	dup2(saved_fd, STDERR_FILENO); /* closes the last write end: read() below ends at EOF */
	close(saved_fd);
	size_t total = 0;
	char buf[1024];
	ssize_t r;
	while ((r = read(fds[0], buf, sizeof(buf))) > 0) {
		fwrite(buf, 1, (size_t)r, stderr); /* shown in the test log on failure */
		total += (size_t)r;
	}
	close(fds[0]);

	img2_set_log_callback(NULL, NULL);
	img2_set_log_level(saved);
	return total;
}

/** Set the value of LONG tag `tag` of IFD0 (little-endian TIFF); 0 if the tag is not there */
static int dng_patch_long(cb_buf_t *b, uint16_t tag, uint32_t v)
{
	const uint32_t ifd = (uint32_t)b->d[4] | ((uint32_t)b->d[5] << 8) | ((uint32_t)b->d[6] << 16) | ((uint32_t)b->d[7] << 24);
	const unsigned n = (unsigned)b->d[ifd] | ((unsigned)b->d[ifd + 1] << 8);
	for (unsigned i = 0; i < n; i++) {
		uint8_t *e = b->d + ifd + 2 + 12 * i;
		if (((unsigned)e[0] | ((unsigned)e[1] << 8)) == tag) {
			e[8] = (uint8_t)v;
			e[9] = (uint8_t)(v >> 8);
			e[10] = (uint8_t)(v >> 16);
			e[11] = (uint8_t)(v >> 24);
			return 1;
		}
	}
	return 0;
}

/** DNG_W x DNG_H 16-bit lossless JPEG (SOF3, one component, predictor 1); free() the result */
static uint8_t *ljpeg_encode(size_t *len)
{
	struct jpeg_compress_struct c;
	struct jpeg_error_mgr e;
	c.err = jpeg_std_error(&e);
	jpeg_create_compress(&c);
	unsigned char *out = NULL;
	unsigned long out_len = 0;
	jpeg_mem_dest(&c, &out, &out_len);
	c.image_width = DNG_W;
	c.image_height = DNG_H;
	c.input_components = 1;
	c.in_color_space = JCS_GRAYSCALE;
	jpeg_set_defaults(&c);
	c.data_precision = 16;
	jpeg_enable_lossless(&c, 1, 0);
	c.optimize_coding = TRUE;
	jpeg_start_compress(&c, TRUE);
	J16SAMPLE row[DNG_W];
	J16SAMPROW rp[1] = { row };
	while (c.next_scanline < c.image_height) {
		for (int x = 0; x < DNG_W; x++) {
			row[x] = (J16SAMPLE)(2000 + x * 300 + (int)c.next_scanline * 200);
		}
		jpeg16_write_scanlines(&c, rp, 1);
	}
	jpeg_finish_compress(&c);
	jpeg_destroy_compress(&c);
	*len = out_len;
	return out;
}

/** Minimal DNG whose raw is the lossless JPEG lj (compression 7, CFA RGGB) */
static void ljpeg_dng_build(cb_buf_t *b, const uint8_t *lj, size_t lj_len)
{
	cb_str(b, "II");
	cb_le16(b, 42);
	cb_le32(b, 0); /* IFD0 offset, patched */
	const uint32_t raw_off = (uint32_t)b->n;
	cb_put(b, lj, lj_len);

	dng_ifd_t ifd;
	ifd.n = 0;
	dng_long(&ifd, 254, 0);
	dng_long(&ifd, 256, DNG_W);
	dng_long(&ifd, 257, DNG_H);
	dng_short(&ifd, 258, 16);
	dng_short(&ifd, 259, 7);
	dng_short(&ifd, 262, 32803);
	dng_long(&ifd, 273, raw_off);
	dng_short(&ifd, 277, 1);
	dng_long(&ifd, 278, DNG_H);
	dng_long(&ifd, 279, (uint32_t)lj_len);
	dng_short(&ifd, 284, 1);
	dng_ent_t *dim = dng_add(&ifd, 33421, T_SHORT, 2); /* CFARepeatPatternDim 2x2 */
	cb_le16(&dim->val, 2);
	cb_le16(&dim->val, 2);
	dng_bytes(&ifd, 33422, T_BYTE, "\x00\x01\x01\x02", 4);                  /* CFAPattern RGGB */
	dng_bytes(&ifd, 50706, T_BYTE, "\x01\x04\x00\x00", 4);                  /* DNGVersion */
	dng_ascii(&ifd, 50708, "Img2 Test Camera");                             /* UniqueCameraModel */
	dng_long(&ifd, 50717, 65535);                                            /* WhiteLevel */
	dng_rationals(&ifd, 50721, T_SRATIONAL, 9, (const int32_t[]){ 10, -3, -1, -4, 12, 1, -1, 2, 6 }, (const int32_t[]){ 10, 10, 10, 10, 10, 10, 10, 10, 10 });
	dng_rationals(&ifd, 50728, T_RATIONAL, 3, (const int32_t[]){ 5, 1, 6 }, (const int32_t[]){ 10, 1, 10 }); /* AsShotNeutral */
	uint32_t ifd0 = dng_write_ifd(b, &ifd, 0);
	b->d[4] = (uint8_t)ifd0;
	b->d[5] = (uint8_t)(ifd0 >> 8);
	b->d[6] = (uint8_t)(ifd0 >> 16);
	b->d[7] = (uint8_t)(ifd0 >> 24);
}

CTEST(jpeg_log_raw, end_of_data_reaches_callback_not_stderr)
{
	cb_buf_t b = { 0 };
	dng_opts_t o = { .preview = PREV_NONE };
	dng_build(&b, &o);
	/* the 64x48x2-byte strip now starts 64 bytes before the end of the file */
	ASSERT_TRUE(dng_patch_long(&b, 273, (uint32_t)b.n - 64));

	rlog_capture_t cap = { 0 };
	img2_status_t st = IMG2_OK;
	size_t n = raw_decode_captured(&b, &cap, &st);

	ASSERT_EQUAL(0, (int)n);
	ASSERT_NOT_EQUAL(IMG2_OK, st);
	int found = 0;
	for (int i = 0; i < cap.calls && i < MAX_MSGS; i++) {
		if (strcmp(cap.msg[i], "unknown file: Unexpected end of file") == 0) {
			ASSERT_EQUAL(IMG2_LOG_WARN, cap.level[i]);
			found++;
		}
	}
	ASSERT_EQUAL(1, found);
	cb_free(&b);
}

CTEST(jpeg_log_raw, corrupt_data_reaches_callback_not_stderr)
{
	size_t lj_len = 0;
	uint8_t *lj = ljpeg_encode(&lj_len);
	ASSERT_NOT_NULL(lj);

	/* entropy-coded data starts after the SOS segment */
	size_t sos = 0;
	for (size_t i = 2; i + 3 < lj_len; i++) {
		if (lj[i] == 0xFF && lj[i + 1] == 0xDA) {
			sos = i;
			break;
		}
	}
	ASSERT_TRUE(sos != 0);
	const size_t data = sos + 2 + (((size_t)lj[sos + 2] << 8) | lj[sos + 3]);
	ASSERT_TRUE(data + 16 < lj_len);
	const size_t at = data + (lj_len - 2 - data) / 2;
	lj[at] = 0xFF;
	lj[at + 1] = 0xD0; /* LibRaw's ljpeg bit reader stops at a non-zero byte after 0xFF */

	cb_buf_t b = { 0 };
	ljpeg_dng_build(&b, lj, lj_len);
	free(lj);

	rlog_capture_t cap = { 0 };
	img2_status_t st = IMG2_OK;
	size_t n = raw_decode_captured(&b, &cap, &st);
	(void)st; /* LibRaw decodes on after a data error */

	ASSERT_EQUAL(0, (int)n);
	static const char prefix[] = "unknown file: data corrupted at ";
	int found = 0;
	for (int i = 0; i < cap.calls && i < MAX_MSGS; i++) {
		if (strncmp(cap.msg[i], prefix, sizeof(prefix) - 1) == 0) {
			ASSERT_EQUAL(IMG2_LOG_WARN, cap.level[i]);
			char *end = NULL;
			long long off = strtoll(cap.msg[i] + sizeof(prefix) - 1, &end, 10);
			ASSERT_TRUE(off > 0 && off < (long long)b.n);
			ASSERT_EQUAL('\0', *end);
			found++;
		}
	}
	ASSERT_EQUAL(1, found);
	cb_free(&b);
}
