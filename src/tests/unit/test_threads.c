/**
 * @file test_threads.c
 * @brief Concurrency smoke test for the library ([SM3], S6 item 12)
 *
 * 8 threads x 20 iterations decode (round-robin) a PNG, a JPEG (jpegqs path), an animated GIF
 * (giflib path) and an SVG with text (font cache contention) through img2_decoder_dispatch,
 * scale frame 0 to 64x64 and encode it as PNG. Meanwhile one thread toggles the log callback.
 * Every call must succeed and every PNG must equal a single-threaded reference.
 * A lock-order violation hangs; the ctest TIMEOUT turns that into a failure.
 */

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../libimg2/core/image.h"
#include "../../libimg2/core/log.h"
#include "../../libimg2/decoders/decoder.h"
#include "../../libimg2/encoders/encoder.h"
#include "../ctest.h"

#ifndef IMG2_TEST_SRC_DIR
#define IMG2_TEST_SRC_DIR "../src/tests"
#endif

#define N_THREADS 8
#define N_ITERS 20
#define N_INPUTS 4

typedef struct {
	const char *rel;
	uint8_t *data;
	size_t len;
	img2_mime_t mime;
	bool usable;
	uint8_t *ref;
	size_t ref_len;
} input_t;

static input_t s_inputs[N_INPUTS] = {
	{ .rel = "data/png/goldhill.png" },
	{ .rel = "data/jpg/DSCN0010.jpg" },
	{ .rel = "data/anim/smoke_anim.gif" },
	{ .rel = "fixtures/svg/text_simple.svg" },
};

static atomic_int s_failures;
static atomic_int s_mismatches;
static atomic_int s_count_a;
static atomic_int s_count_b;
static atomic_bool s_stop;

static void count_a(img2_log_level_t level, const char *msg, void *user)
{
	(void)level;
	(void)msg;
	(void)user;
	atomic_fetch_add(&s_count_a, 1);
}

static void count_b(img2_log_level_t level, const char *msg, void *user)
{
	(void)level;
	(void)msg;
	(void)user;
	atomic_fetch_add(&s_count_b, 1);
}

static uint8_t *load_file(const char *rel, size_t *len)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", IMG2_TEST_SRC_DIR, rel);
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *buf = (n > 0) ? malloc((size_t)n) : NULL;
	if (buf == NULL || fread(buf, 1, (size_t)n, f) != (size_t)n) {
		free(buf);
		fclose(f);
		return NULL;
	}
	fclose(f);
	*len = (size_t)n;
	return buf;
}

/* decode -> scale frame 0 to 64x64 -> PNG (level 6) */
static img2_status_t run_once(const input_t *in, uint8_t **png, size_t *png_len)
{
	*png = NULL;
	*png_len = 0;
	img2_frames_t fr = { 0 };
	img2_status_t st = img2_decoder_dispatch(NULL, in->data, in->len, in->mime, &fr);
	if (st != IMG2_OK) {
		return st;
	}
	img2_image_t *scaled = NULL;
	st = img2_image_scale_resize(fr.frames[0], 64, 64, &scaled);
	img2_frames_free(&fr);
	if (st != IMG2_OK) {
		return st;
	}
	st = img2_encoder_dispatch(scaled, IMG2_FORMAT_PNG, 6, png, png_len);
	img2_image_destroy(scaled);
	return st;
}

static void *worker(void *arg)
{
	int tid = (int)(intptr_t)arg;
	for (int i = 0; i < N_ITERS; i++) {
		const input_t *in = &s_inputs[(tid + i) % N_INPUTS];
		if (!in->usable) {
			continue;
		}
		uint8_t *png = NULL;
		size_t png_len = 0;
		if (run_once(in, &png, &png_len) != IMG2_OK) {
			atomic_fetch_add(&s_failures, 1);
			continue;
		}
		if (png_len != in->ref_len || memcmp(png, in->ref, png_len) != 0) {
			atomic_fetch_add(&s_mismatches, 1);
		}
		free(png);
	}
	return NULL;
}

static void *toggler(void *arg)
{
	(void)arg;
	bool a = true;
	while (!atomic_load(&s_stop)) {
		img2_set_log_callback(a ? count_a : count_b, NULL);
		a = !a;
	}
	return NULL;
}

CTEST(threads, decode_scale_encode_concurrently)
{
	ASSERT_EQUAL(IMG2_OK, img2_init());
	ASSERT_TRUE(img2_can_encode(IMG2_FORMAT_PNG));

	int usable = 0;
	for (int k = 0; k < N_INPUTS; k++) {
		input_t *in = &s_inputs[k];
		in->data = load_file(in->rel, &in->len);
		ASSERT_NOT_NULL(in->data);
		in->mime = img2_detect_mime(in->data, in->len);
		in->usable = img2_can_decode(in->mime);
		if (in->usable) {
			/* single-threaded reference */
			ASSERT_EQUAL(IMG2_OK, run_once(in, &in->ref, &in->ref_len));
			usable++;
		}
	}
	ASSERT_TRUE(usable >= 2);

	/* INFO lines from the dispatcher make the callbacks fire while they are swapped */
	img2_log_level_t old_level = img2_get_log_level();
	img2_set_log_level(IMG2_LOG_INFO);

	pthread_t tog;
	pthread_t th[N_THREADS];
	atomic_store(&s_stop, false);
	ASSERT_EQUAL(0, pthread_create(&tog, NULL, toggler, NULL));
	for (int t = 0; t < N_THREADS; t++) {
		ASSERT_EQUAL(0, pthread_create(&th[t], NULL, worker, (void *)(intptr_t)t));
	}
	for (int t = 0; t < N_THREADS; t++) {
		pthread_join(th[t], NULL);
	}
	atomic_store(&s_stop, true);
	pthread_join(tog, NULL);

	img2_set_log_callback(NULL, NULL);
	img2_set_log_level(old_level);

	ASSERT_EQUAL(0, atomic_load(&s_failures));
	ASSERT_EQUAL(0, atomic_load(&s_mismatches));
	ASSERT_TRUE(atomic_load(&s_count_a) + atomic_load(&s_count_b) > 0);

	for (int k = 0; k < N_INPUTS; k++) {
		free(s_inputs[k].data);
		free(s_inputs[k].ref);
	}
	img2_shutdown();
}

CTEST(threads, init_shutdown_refcount)
{
	/* unbalanced shutdown is a no-op; re-init after the final shutdown works (fonts re-scan lazily) */
	img2_shutdown();
	ASSERT_EQUAL(IMG2_OK, img2_init());
	ASSERT_EQUAL(IMG2_OK, img2_init());
	img2_shutdown();
	img2_shutdown();
	img2_shutdown();
	ASSERT_EQUAL(IMG2_OK, img2_init());
	img2_shutdown();
	ASSERT_TRUE(img2_decoder_count() > 0);
}
