/**
 * @file test_sec_fix_cli.c
 * @brief Security fixes of the imgcat2 CLI (P-numbers of the audit); stderr / stdout captured into a file
 *
 * P11: pipeline_decode() printed "Applying EXIF orientation: N (<description>)" (unless silent) with
 *      descriptions[N - 1] for any EXIF orientation > 1: 9 .. 65535 read past the 8-entry array.
 */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <img2/img2.h>

#include "../../imgcat2/cli.h"
#include "../../imgcat2/pipeline.h"
#include "../ctest.h"

/* ---- capture of a standard stream (fd 1 or 2) into a file of the test temp dir ---- */

typedef struct {
	int fd;    /* captured descriptor */
	int saved; /* its duplicate, restored by cap_end() */
	char path[512];
} cap_t;

static void cap_begin(cap_t *c, int fd, const char *name)
{
	const char *dir = getenv("TMPDIR");
	if (dir == NULL || dir[0] == '\0') {
		dir = IMGCAT2_TEST_TMPDIR;
	}
	snprintf(c->path, sizeof(c->path), "%s/sec_fix_cli_%s_%ld.txt", dir, name, (long)getpid());
	fflush(stdout);
	fflush(stderr);
	c->fd = fd;
	c->saved = dup(fd);
	int f = open(c->path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
	ASSERT_TRUE(c->saved >= 0 && f >= 0);
	ASSERT_TRUE(dup2(f, fd) >= 0);
	close(f);
}

/* restore the stream; returns the captured bytes (NUL-terminated, free()), length in *len */
static char *cap_end(cap_t *c, size_t *len)
{
	fflush(stdout);
	fflush(stderr);
	dup2(c->saved, c->fd);
	close(c->saved);
	FILE *f = fopen(c->path, "rb");
	char *buf = calloc(1, 65536);
	size_t n = 0;
	if (f != NULL && buf != NULL) {
		n = fread(buf, 1, 65535, f);
	}
	if (f != NULL) {
		fclose(f);
	}
	unlink(c->path);
	*len = n;
	return buf;
}

/* ---- P11: EXIF orientation outside 1..8 ---- */

/* 4 x 4 JPEG with an EXIF APP1 (IFD0: Orientation = orientation) right after SOI; free() */
static uint8_t *jpeg_with_orientation(uint16_t orientation, size_t *len)
{
	img2_image_t *img = NULL;
	if (img2_image_create(4, 4, &img) != IMG2_OK) {
		return NULL;
	}
	img2_frames_t one = { .frames = &img, .count = 1 };
	img2_encode_opts_t eo = { .quality = 90 };
	uint8_t *jpg = NULL;
	size_t jlen = 0;
	img2_status_t st = img2_encode(&one, IMG2_FORMAT_JPEG, &eo, &jpg, &jlen);
	img2_image_destroy(img);
	if (st != IMG2_OK) {
		return NULL;
	}
	static const uint8_t app1_head[] = {
		0xFF, 0xE1, 0x00, 34, 'E', 'x', 'i', 'f', 0, 0,
		'I', 'I', 42, 0, 8, 0, 0, 0, /* TIFF header, IFD0 at 8 */
		1, 0,                         /* one entry */
		0x12, 0x01, 3, 0, 1, 0, 0, 0, /* Orientation, SHORT, count 1 */
	};
	uint8_t *out = malloc(jlen + sizeof(app1_head) + 8);
	if (out == NULL) {
		img2_free(jpg);
		return NULL;
	}
	size_t n = 0;
	memcpy(out, jpg, 2); /* SOI */
	n = 2;
	memcpy(out + n, app1_head, sizeof(app1_head));
	n += sizeof(app1_head);
	out[n++] = (uint8_t)orientation;
	out[n++] = (uint8_t)(orientation >> 8);
	out[n++] = 0;
	out[n++] = 0;
	memset(out + n, 0, 4); /* no next IFD */
	n += 4;
	memcpy(out + n, jpg + 2, jlen - 2);
	n += jlen - 2;
	img2_free(jpg);
	*len = n;
	return out;
}

CTEST(sec_fix_cli, exif_orientation_out_of_range)
{
	static const uint16_t values[] = { 9, 255, 65535 };
	for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
		size_t len = 0;
		uint8_t *data = jpeg_with_orientation(values[i], &len);
		ASSERT_NOT_NULL(data);

		cli_options_t opts;
		memset(&opts, 0, sizeof(opts));
		opts.silent = false;
		img2_image_t **frames = NULL;
		int count = 0;
		cap_t c;
		cap_begin(&c, STDERR_FILENO, "orient");
		int rc = pipeline_decode(&opts, data, len, &frames, &count);
		size_t n = 0;
		char *err = cap_end(&c, &n);
		ASSERT_NOT_NULL(err);
		ASSERT_EQUAL(0, rc);
		ASSERT_EQUAL(1, count);
		ASSERT_EQUAL(values[i], frames[0]->exif->orientation);

		char expect[96];
		snprintf(expect, sizeof(expect), "Applying EXIF orientation: %u (invalid)\n", (unsigned)values[i]);
		if (strstr(err, expect) == NULL) {
			fprintf(stderr, "  stderr: %s\n", err);
		}
		ASSERT_NOT_NULL(strstr(err, expect));

		img2_frames_t fr = { .frames = frames, .count = count };
		img2_frames_free(&fr);
		free(err);
		free(data);
	}
}
