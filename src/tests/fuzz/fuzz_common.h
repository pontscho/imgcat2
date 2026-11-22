/**
 * @file fuzz_common.h
 * @brief Shared helpers of the libFuzzer targets (src/tests/fuzz, IMG2_FUZZ builds only)
 *
 * Every target routes libimg2's log through a callback at IMG2_LOG_DEBUG, so the message formatting and
 * sanitising code runs on fuzzed strings too; the messages are dropped unless IMG2_FUZZ_LOG is set in the
 * environment (then they go to stderr, useful when reproducing a finding).
 */

#ifndef IMG2_FUZZ_COMMON_H
#define IMG2_FUZZ_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <img2/img2.h>

int LLVMFuzzerInitialize(int *argc, char ***argv);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static int g_fuzz_log_stderr;

static void fuzz_log_cb(img2_log_level_t level, const char *msg, void *user)
{
	(void)user;
	if (g_fuzz_log_stderr) {
		fprintf(stderr, "[img2 %d] %s\n", (int)level, msg);
	}
}

static void fuzz_setup_logging(void)
{
	g_fuzz_log_stderr = getenv("IMG2_FUZZ_LOG") != NULL;
	img2_set_log_callback(fuzz_log_cb, NULL);
	img2_set_log_level(IMG2_LOG_DEBUG);
}

/* Abort (a libFuzzer crash) when a fixed-size string field of a result struct lost its terminator */
#define FUZZ_CHECK_CSTR(field)                                       \
	do {                                                             \
		if (memchr((field), '\0', sizeof(field)) == NULL) {          \
			fprintf(stderr, "unterminated string field: %s\n", #field); \
			abort();                                                 \
		}                                                            \
	} while (0)

/* Validate the frame set's invariants and read a sample of every frame's pixel buffer, its last byte included
 * (ASan then checks that the buffer really spans stride * height bytes) */
static void fuzz_touch_frames(const img2_frames_t *f)
{
	volatile uint8_t sink = 0;
	uint8_t acc = 0;

	if (f->count < 1 || f->frames == NULL) {
		abort();
	}
	for (int i = 0; i < f->count; i++) {
		const img2_image_t *img = f->frames[i];
		if (img == NULL || img->pixels == NULL || img->width == 0 || img->height == 0 ||
			img->width > IMG2_MAX_DIMENSION || img->height > IMG2_MAX_DIMENSION || img->stride != img->width * 4u) {
			abort();
		}
		size_t n = (size_t)img->stride * img->height;
		for (size_t k = 0; k < n; k += 61) {
			acc ^= img->pixels[k];
		}
		acc ^= img->pixels[n - 1];
	}
	sink = acc;
	(void)sink;
}

/* The CLI's post-decode steps on frame 0: EXIF orientation, then a small aspect-preserving scale */
static void fuzz_post_decode(img2_frames_t *f)
{
	img2_image_t *scaled = NULL;

	if (img2_apply_orientation(&f->frames[0]) != IMG2_OK) {
		return;
	}
	if (img2_scale_fit(f->frames[0], 7, 5, IMG2_FILTER_TRIANGLE, &scaled) == IMG2_OK) {
		img2_image_destroy(scaled);
	}
}

#endif /* IMG2_FUZZ_COMMON_H */
