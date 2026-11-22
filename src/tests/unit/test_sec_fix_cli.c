/**
 * @file test_sec_fix_cli.c
 * @brief Security fixes of the imgcat2 CLI (P-numbers of the audit); stderr / stdout captured into a file
 *
 * P11: pipeline_decode() printed "Applying EXIF orientation: N (<description>)" (unless silent) with
 *      descriptions[N - 1] for any EXIF orientation > 1: 9 .. 65535 read past the 8-entry array.
 * P20: iterm2_render()'s size line computed (original - encoded) in size_t: an encoded image larger than the
 *      input wrapped to a "1.8e19% reduction".
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
#include "../../imgcat2/terminal/iterm2.h"
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

/* ---- P20: iTerm2 size line when the re-encoded image is larger than the input ---- */

/* iterm2_render() of a w x h noise image (JPEG) with original_size; returns its stderr (free()) */
static char *iterm2_size_line(uint32_t w, uint32_t h, size_t original_size)
{
	img2_image_t *img = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_create(w, h, &img));
	uint32_t seed = 12345;
	for (size_t i = 0; i < (size_t)w * h * 4; i++) {
		seed = seed * 1103515245u + 12345u;
		img->pixels[i] = (uint8_t)(seed >> 16);
	}
	cli_options_t opts;
	memset(&opts, 0, sizeof(opts));
	opts.silent = false;
	opts.iterm2_format = IMG2_FORMAT_JPEG;
	opts.jpeg_quality = 90;
	cap_t out;
	cap_t err;
	cap_begin(&out, STDOUT_FILENO, "iterm2_out");
	cap_begin(&err, STDERR_FILENO, "iterm2_err");
	int rc = iterm2_render(&img, 1, &opts, original_size);
	size_t n = 0;
	char *text = cap_end(&err, &n);
	free(cap_end(&out, &n));
	img2_image_destroy(img);
	ASSERT_EQUAL(0, rc);
	ASSERT_NOT_NULL(text);
	return text;
}

CTEST(sec_fix_cli, iterm2_size_increase)
{
	/* 64 x 64 noise: the JPEG is several KB, the "original" 100 bytes */
	char *err = iterm2_size_line(64, 64, 100);
	if (strstr(err, "% increase)\n") == NULL || strstr(err, "reduction") != NULL) {
		fprintf(stderr, "  stderr: %s\n", err);
	}
	ASSERT_NOT_NULL(strstr(err, "iTerm2: Encoded to JPEG: 0.0 MB → 0.0 MB ("));
	ASSERT_NOT_NULL(strstr(err, "% increase)\n"));
	ASSERT_NULL(strstr(err, "reduction"));
	free(err);

	/* smaller than the input: unchanged "N% reduction" line (100 MB input: 100.0%) */
	err = iterm2_size_line(8, 8, (size_t)100 << 20);
	ASSERT_NOT_NULL(strstr(err, "iTerm2: Encoded to JPEG: 100.0 MB → 0.0 MB (100.0% reduction)\n"));
	free(err);
}
