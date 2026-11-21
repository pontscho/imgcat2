/**
 * @file consumer.c
 * @brief libimg2 consumer smoke test: only <img2/img2.h>, linked through img2::img2
 *
 * Usage: img2_consumer <file.png>. Prints "img2 consumer OK" and returns 0 on success; any failure
 * prints the failing step with img2_strerror() and returns 1.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <img2/img2.h>

static int s_log_calls;

static void count_log(img2_log_level_t level, const char *msg, void *user)
{
	(void)level;
	(void)msg;
	(void)user;
	s_log_calls++;
}

static int fail(const char *what, img2_status_t st)
{
	fprintf(stderr, "img2 consumer FAILED: %s: %s\n", what, img2_strerror(st));
	return 1;
}

static uint8_t *read_file(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		return NULL;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return NULL;
	}
	long n = ftell(f);
	if (n <= 0 || fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return NULL;
	}
	uint8_t *buf = malloc((size_t)n);
	if (buf == NULL || fread(buf, 1, (size_t)n, f) != (size_t)n) {
		free(buf);
		fclose(f);
		return NULL;
	}
	fclose(f);
	*len = (size_t)n;
	return buf;
}

static int run(const uint8_t *data, size_t len)
{
	img2_status_t st;

	if (img2_detect_mime(data, len) != IMG2_MIME_PNG) {
		return fail("img2_detect_mime != IMG2_MIME_PNG", IMG2_E_FORMAT);
	}

	img2_frames_t fr;
	img2_decode_opts_t opts = IMG2_DECODE_OPTS_INIT;
	st = img2_decode(data, len, &opts, &fr);
	if (st != IMG2_OK) {
		return fail("img2_decode", st);
	}
	if (fr.count < 1 || fr.frames == NULL || fr.frames[0] == NULL || fr.frames[0]->stride != fr.frames[0]->width * 4) {
		img2_frames_free(&fr);
		return fail("decoded frame set", IMG2_E_INTERNAL);
	}

	img2_image_t *small = NULL;
	st = img2_scale(fr.frames[0], 16, 16, IMG2_FILTER_DEFAULT, &small);
	img2_frames_free(&fr);
	if (st != IMG2_OK) {
		return fail("img2_scale", st);
	}
	if (small->width != 16 || small->height != 16) {
		img2_image_destroy(small);
		return fail("scaled dimensions", IMG2_E_INTERNAL);
	}

	img2_frames_t in = { &small, 1, 0 };
	img2_encode_opts_t eo = IMG2_ENCODE_OPTS_INIT;
	uint8_t *buf = NULL;
	size_t buf_len = 0;
	if (img2_can_encode(IMG2_FORMAT_JPEG)) {
		st = img2_encode(&in, IMG2_FORMAT_JPEG, &eo, &buf, &buf_len); /* quality -1 = format default */
		if (st != IMG2_OK || buf == NULL || buf_len == 0) {
			img2_image_destroy(small);
			return fail("img2_encode(JPEG)", st);
		}
		img2_free(buf);
		buf = NULL;
	}

	st = img2_encode(&in, IMG2_FORMAT_PNG, &eo, &buf, &buf_len);
	img2_image_destroy(small);
	if (st != IMG2_OK) {
		return fail("img2_encode(PNG)", st);
	}
	st = img2_decode(buf, buf_len, NULL, &fr);
	img2_free(buf);
	if (st != IMG2_OK) {
		return fail("img2_decode(re-encoded PNG)", st);
	}
	if (fr.count != 1 || fr.frames[0]->width != 16 || fr.frames[0]->height != 16) {
		img2_frames_free(&fr);
		return fail("re-decoded dimensions", IMG2_E_INTERNAL);
	}
	img2_frames_free(&fr);

	/* [Q6] same fixed 64-byte pattern as test_public_api: 0xA5, byte 2 = 0x00, byte 16 = 0x07 */
	uint8_t garbage[64];
	memset(garbage, 0xA5, sizeof(garbage));
	garbage[2] = 0x00;
	garbage[16] = 0x07;
	if (img2_detect_mime(garbage, sizeof(garbage)) != IMG2_MIME_UNKNOWN) {
		return fail("img2_detect_mime(garbage) != IMG2_MIME_UNKNOWN", IMG2_E_INTERNAL);
	}
	s_log_calls = 0;
	img2_set_log_callback(count_log, NULL);
	st = img2_decode(garbage, sizeof(garbage), NULL, &fr);
	img2_set_log_callback(NULL, NULL);
	if (st != IMG2_E_FORMAT) {
		if (st == IMG2_OK) {
			img2_frames_free(&fr);
		}
		return fail("garbage decode did not return IMG2_E_FORMAT", st);
	}
	if (s_log_calls < 1) {
		return fail("log callback not called for the garbage decode", st);
	}
	if (img2_strerror(st) == NULL) {
		return fail("img2_strerror returned NULL", st);
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <file.png>\n", argv[0]);
		return 1;
	}
	size_t len = 0;
	uint8_t *data = read_file(argv[1], &len);
	if (data == NULL) {
		fprintf(stderr, "img2 consumer FAILED: cannot read %s\n", argv[1]);
		return 1;
	}
	img2_status_t st = img2_init();
	if (st != IMG2_OK) {
		free(data);
		return fail("img2_init", st);
	}
	int rc = run(data, len);
	free(data);
	img2_shutdown();
	if (rc != 0) {
		return rc;
	}
	printf("img2 consumer OK (img2 %s)\n", img2_version_string());
	return 0;
}
