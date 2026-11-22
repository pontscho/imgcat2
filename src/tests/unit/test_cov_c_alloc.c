/**
 * @file test_cov_c_alloc.c
 * @brief Coverage lane C: allocation failures in the HEIF/AVIF/JXL decoders and the HEIF/JXL/PNG encoders
 *
 * Linked with -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=strdup (alloc_fail.h, Linux only). Every
 * operation must survive each injected failure: an error status and no frames, or a complete result.
 *
 * - HEIF/AVIF decoding uses libheif's uncompressed codec: libheif itself allocates with operator new (not
 *   wrapped), so every failing allocation is one of libimg2's own.
 * - HEIF encoding goes through kvazaar, which crashes on several of its own allocation failures (vendor bug,
 *   e.g. kvz_config_init(NULL) after a failed kvz_config_alloc(), docs/coverage.md). Only the tail of the
 *   operation is failed: the output buffer allocations of libimg2, and the kvazaar failures that come back as an
 *   encoder error, probed in a child process (fork) so that a crashing candidate cannot take the test down.
 */

#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <img2/img2.h>

#include "../../libimg2/core/image.h"
#include "../../libimg2/decoders/decoder.h"
#include "../../libimg2/encoders/encoder.h"
#include "../ctest.h"
#include "alloc_fail.h"
#include "cov_c_heif_util.h"
#include "cov_c_jxl_util.h"
#include "cov_c_util.h"

#define AF_MAX_RUNS 20000

/* result of the last operation; checked after the failure loops (ASSERT must not fire while armed) */
static int s_bad;

/* ===== decoders ===== */

typedef struct {
	img2_status_t (*dec)(const uint8_t *, size_t, const img2_decode_opts_t *, img2_frames_t *);
	const uint8_t *data;
	size_t len;
	int failures; /* runs that returned an error */
} dec_ctx_t;

static void op_dec(void *ctx)
{
	dec_ctx_t *c = ctx;
	img2_frames_t fr = { 0 };
	img2_status_t st = c->dec(c->data, c->len, NULL, &fr);
	if (st == IMG2_OK) {
		if (fr.count < 1 || fr.frames == NULL) {
			s_bad++;
		}
	} else {
		c->failures++;
		if (fr.frames != NULL || fr.count != 0 || (st != IMG2_E_NOMEM && st != IMG2_E_CODEC)) {
			s_bad++;
		}
	}
	img2_frames_free(&fr);
}

static void fail_each(img2_status_t (*dec)(const uint8_t *, size_t, const img2_decode_opts_t *, img2_frames_t *), const uint8_t *data, size_t len)
{
	dec_ctx_t c = { dec, data, len, 0 };
	long runs = af_for_each_failure(op_dec, &c, AF_MAX_RUNS);
	ASSERT_TRUE(runs >= 2);
	ASSERT_TRUE(runs < AF_MAX_RUNS);
	ASSERT_TRUE(c.failures >= 1);
	ASSERT_EQUAL(0, s_bad);
}

static const char k_xmp[] = "<x:xmpmeta><rdf:Description><dc:creator>Ann</dc:creator></rdf:Description></x:xmpmeta>";

CTEST(cov_c_alloc, heif_avif_decoders)
{
	heif_init(NULL);
	uint8_t tiff[600];
	uint8_t exif[620] = { 0, 0, 0, 6, 'E', 'x', 'i', 'f', 0, 0 };
	size_t tlen = cov_tiff_build(tiff, COV_T_MAKE, 0);
	memcpy(exif + 10, tiff, tlen);
	hb_meta_t metas[2] = { { "Exif", NULL, exif, (int)(10 + tlen) }, { "mime", "application/rdf+xml", k_xmp, (int)strlen(k_xmp) } };

	size_t len[3] = { 0, 0, 0 };
	uint8_t *files[3];
	files[0] = hb_list(heif_compression_uncompressed, 1, 8, 8, metas, 2, &len[0]); /* still + EXIF + XMP */
	files[1] = hb_list(heif_compression_uncompressed, 3, 8, 8, NULL, 0, &len[1]);  /* image list */
	hb_seq_t sp = { heif_compression_uncompressed, 3, 1000, 1, 100, 0 };
	files[2] = hb_seq(&sp, &len[2]);                                                 /* sequence track */
	/* malloc'ed memory filled with a non-zero pattern: a struct used without its init function shows up
	 * (the still decoders cleared the never-initialised XMP struct when only the EXIF allocation failed) */
	mallopt(M_PERTURB, 0x87);
	for (int f = 0; f < 3; f++) {
		ASSERT_NOT_NULL(files[f]);
		fail_each(img2_decode_heif, files[f], len[f]);
		fail_each(img2_decode_avif, files[f], len[f]);
		free(files[f]);
	}
	mallopt(M_PERTURB, 0);
	heif_deinit();
}

CTEST(cov_c_alloc, jxl_decoder)
{
	/* still in a container with Exif and XMP boxes, and a 3-frame animation */
	jx_params_t sp = { 3, 2, 1, 0, 0, 0, 0, 0 };
	size_t cs_len = 0;
	uint8_t *cs = jx_encode(&sp, &cs_len);
	ASSERT_NOT_NULL(cs);
	jx_buf_t b = { 0 };
	jx_container_start(&b);
	uint8_t tiff[600];
	jx_box(&b, "Exif", tiff, cov_tiff_build(tiff, COV_T_MAKE, 0));
	jx_box(&b, "xml ", k_xmp, strlen(k_xmp));
	jx_box(&b, "jxlc", cs, cs_len);
	free(cs);
	fail_each(img2_decode_jxl, b.d, b.len);
	free(b.d);

	jx_params_t ap = { 3, 2, 3, 1000, 1, 0, 20, 0 };
	size_t len = 0;
	uint8_t *anim = jx_encode(&ap, &len);
	ASSERT_NOT_NULL(anim);
	fail_each(img2_decode_jxl, anim, len);
	free(anim);
}

/* ===== encoders ===== */

typedef struct {
	const img2_image_t *img;
	img2_status_t (*enc)(const img2_image_t *, int, uint8_t **, size_t *);
	int quality;
	img2_status_t st;
} enc_ctx_t;

static void op_enc(void *ctx)
{
	enc_ctx_t *c = ctx;
	uint8_t *out = NULL;
	size_t size = 0;
	c->st = c->enc(c->img, c->quality, &out, &size);
	if ((c->st == IMG2_OK) != (out != NULL && size > 0)) {
		s_bad++;
	}
	free(out);
}

/*
 * Fail allocation n of op_enc for every n in [from, to), each in its own child process (run concurrently).
 * Returns how many children reported `expect`; crashed children (vendor bug) count as nothing.
 */
static int enc_fail_in_children(enc_ctx_t *c, long from, long to, img2_status_t expect)
{
	enum { MAX_CHILDREN = 64 };
	pid_t pids[MAX_CHILDREN];
	int n_children = 0;
	fflush(NULL);
	for (long n = from < 0 ? 0 : from; n < to && n_children < MAX_CHILDREN; n++) {
		pid_t pid = fork();
		if (pid == 0) {
			struct rlimit no_core = { 0, 0 };
			setrlimit(RLIMIT_CORE, &no_core);
			af_arm(n);
			op_enc(c);
			af_disarm();
			exit(c->st == IMG2_OK ? 0 : -c->st); /* exit(), not _exit(): the coverage profile is written at exit */
		}
		if (pid > 0) {
			pids[n_children++] = pid;
		}
	}
	int hits = 0;
	for (int i = 0; i < n_children; i++) {
		int ws = 0;
		if (waitpid(pids[i], &ws, 0) == pids[i] && WIFEXITED(ws) && -WEXITSTATUS(ws) == expect) {
			hits++;
		}
	}
	return hits;
}

CTEST(cov_c_alloc, heif_encoder_tail)
{
	img2_image_t *img = img2_image_alloc(16, 16);
	ASSERT_NOT_NULL(img);
	cov_fill_noise(img->pixels, 16 * 16 * 4, 7u);
	enc_ctx_t c = { img, img2_encode_heif, 50, IMG2_OK };
	op_enc(&c); /* warm-up: one-time initialisation inside the codecs allocates only on the first run */
	long total = af_count_allocs(op_enc, &c);
	ASSERT_EQUAL(IMG2_OK, c.st);
	ASSERT_TRUE(total > 3);
	ASSERT_EQUAL(total, af_count_allocs(op_enc, &c));

	/* the last allocation is the output copy, the one before it the write buffer (one write call) */
	af_arm(total - 1);
	op_enc(&c);
	af_disarm();
	ASSERT_EQUAL(IMG2_E_NOMEM, c.st);
	af_arm(total - 2);
	op_enc(&c);
	af_disarm();
	ASSERT_EQUAL(IMG2_E_NOMEM, c.st);
	ASSERT_EQUAL(0, s_bad);

	/* heif_context_encode_image failing: kvazaar allocations near the end whose failure kvazaar reports as an
	 * encoder error (others crash kvazaar, hence the child processes) */
	ASSERT_TRUE(enc_fail_in_children(&c, total - 40, total - 2, IMG2_E_CODEC) >= 1);
	img2_image_destroy(img);
}

CTEST(cov_c_alloc, jxl_encoder_output_growth)
{
	/* lossless noise > 64 KiB: the output buffer is grown with realloc */
	img2_image_t *img = img2_image_alloc(136, 136);
	ASSERT_NOT_NULL(img);
	cov_fill_noise(img->pixels, (size_t)136 * 136 * 4, 3u);
	enc_ctx_t c = { img, img2_encode_jxl, 100, IMG2_OK };
	long total = af_count_allocs(op_enc, &c);
	ASSERT_EQUAL(IMG2_OK, c.st);
	/* the last allocations: output copy, then (going back) the realloc of the 64 KiB buffer */
	int nomem = 0;
	for (long n = total - 1; n >= total - 6 && n >= 0 && nomem < 2; n--) {
		af_arm(n);
		op_enc(&c);
		af_disarm();
		nomem += c.st == IMG2_E_NOMEM;
	}
	ASSERT_TRUE(nomem >= 2);
	ASSERT_EQUAL(0, s_bad);
	img2_image_destroy(img);
}

/* Bytes of heap in use (glibc): equal before and after an operation that frees everything it allocates */
static size_t heap_in_use(void)
{
	struct mallinfo2 mi = mallinfo2();
	return mi.uordblks + mi.hblkhd;
}

/*
 * Fail each allocation of op_enc; returns the number of failure points that leave heap memory behind. Each
 * failure is injected twice and only the second run is measured: freed chunks parked in glibc's tcache count as
 * "in use" for mallinfo2(), so a first run along a new allocation pattern can raise the figure without a leak,
 * while a real leak grows it on every run.
 */
static int enc_leaking_failures(enc_ctx_t *c)
{
	op_enc(c); /* warm-up */
	long total = af_count_allocs(op_enc, c);
	int leaks = 0;
	for (long n = 0; n < total; n++) {
		af_arm(n);
		op_enc(c);
		af_disarm();
		size_t before = heap_in_use();
		af_arm(n);
		op_enc(c);
		af_disarm();
		if (heap_in_use() != before) {
			fprintf(stderr, "allocation %ld of %ld failed: %zu bytes not freed (status %d)\n", n, total, heap_in_use() - before, c->st);
			leaks++;
		}
	}
	return leaks;
}

/* Failures after the first output bytes: libpng longjmps back into img2_encode_png, which must free the
 * output buffer and the row pointers (both were lost: write_state is indeterminate after longjmp) */
CTEST(cov_c_alloc, png_encoder_error_frees_everything)
{
	img2_image_t *img = img2_image_alloc(64, 64);
	ASSERT_NOT_NULL(img);
	cov_fill_noise(img->pixels, (size_t)64 * 64 * 4, 5u);
	enc_ctx_t c = { img, img2_encode_png, 6, IMG2_OK };
	ASSERT_EQUAL(0, enc_leaking_failures(&c));
	ASSERT_EQUAL(0, s_bad);
	img2_image_destroy(img);
}

/* The jpeg_mem_dest() buffer was never freed on an error (row buffer allocation failure, libjpeg errors) */
CTEST(cov_c_alloc, jpeg_encoder_error_frees_everything)
{
	img2_image_t *img = img2_image_alloc(64, 64);
	ASSERT_NOT_NULL(img);
	cov_fill_noise(img->pixels, (size_t)64 * 64 * 4, 9u);
	enc_ctx_t c = { img, img2_encode_jpeg, 95, IMG2_OK };
	ASSERT_EQUAL(0, enc_leaking_failures(&c));
	ASSERT_EQUAL(0, s_bad);
	img2_image_destroy(img);
}

CTEST(cov_c_alloc, png_encoder_write_growth)
{
	/* noise: several 4 KiB-doubling reallocs of the write buffer; every allocation failed once */
	img2_image_t *img = img2_image_alloc(64, 64);
	ASSERT_NOT_NULL(img);
	cov_fill_noise(img->pixels, (size_t)64 * 64 * 4, 5u);
	enc_ctx_t c = { img, img2_encode_png, 6, IMG2_OK };
	long runs = af_for_each_failure(op_enc, &c, AF_MAX_RUNS);
	ASSERT_TRUE(runs >= 4);
	ASSERT_EQUAL(0, s_bad);
	img2_image_destroy(img);
}
