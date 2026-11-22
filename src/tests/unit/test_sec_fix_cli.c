/**
 * @file test_sec_fix_cli.c
 * @brief Security fixes of the imgcat2 CLI (P-numbers of the audit); stderr / stdout captured into a file
 *
 * P11: pipeline_decode() printed "Applying EXIF orientation: N (<description>)" (unless silent) with
 *      descriptions[N - 1] for any EXIF orientation > 1: 9 .. 65535 read past the 8-entry array.
 * P20: iterm2_render()'s size line computed (original - encoded) in size_t: an encoded image larger than the
 *      input wrapped to a "1.8e19% reduction".
 * P38: the text --info output printed EXIF / XMP strings raw (terminal escape injection, CWE-150).
 * P39: error / verbose messages printed file paths raw.
 */

#include <sys/stat.h>
#include <sys/types.h>

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <img2/img2.h>

#include "../../imgcat2/cli.h"
#include "../../imgcat2/metadata.h"
#include "../../imgcat2/pipeline.h"
#include "../../imgcat2/safe_text.h"
#include "../../imgcat2/terminal/file.h"
#include "../../imgcat2/terminal/iterm2.h"
#include "../../libimg2/metadata/exif_reader.h"
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

/* ---- P38 / P39: terminal control characters of EXIF / XMP strings and file paths ---- */

/* no C0 control but '\n' / '\t', no DEL, no raw C1 byte (0x80-0x9F outside UTF-8), no UTF-8 encoded C1 */
static int count_controls(const char *s, size_t n)
{
	int bad = 0;
	for (size_t i = 0; i < n; i++) {
		const unsigned char c = (unsigned char)s[i];
		if ((c < 0x20 && c != '\n' && c != '\t') || c == 0x7F || c == 0x9B || (c == 0xC2 && i + 1 < n && (unsigned char)s[i + 1] >= 0x80 && (unsigned char)s[i + 1] <= 0x9F)) {
			bad++;
		}
	}
	return bad;
}

static int count_str(const char *s, const char *needle)
{
	int n = 0;
	for (const char *p = strstr(s, needle); p != NULL; p = strstr(p + 1, needle)) {
		n++;
	}
	return n;
}

/* 4 x 4 JPEG whose EXIF IFD0 has only Make = make; free() */
static uint8_t *jpeg_with_make(const char *make, size_t *len)
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
	const size_t mlen = strlen(make) + 1;
	const size_t seg = 2 + 6 + 26 + mlen; /* length field, "Exif\0\0", TIFF header + IFD0, the string */
	uint8_t *out = malloc(jlen + seg + 2);
	if (out == NULL) {
		img2_free(jpg);
		return NULL;
	}
	const uint8_t head[] = {
		0xFF, 0xE1, (uint8_t)(seg >> 8), (uint8_t)seg, 'E', 'x', 'i', 'f', 0, 0,
		'I', 'I', 42, 0, 8, 0, 0, 0,                                   /* TIFF header, IFD0 at 8 */
		1, 0,                                                          /* one entry */
		0x0F, 0x01, 2, 0, (uint8_t)mlen, 0, 0, 0, 26, 0, 0, 0,        /* Make, ASCII, at 26 */
		0, 0, 0, 0,                                                    /* no next IFD */
	};
	size_t n = 0;
	memcpy(out, jpg, 2);
	n = 2;
	memcpy(out + n, head, sizeof(head));
	n += sizeof(head);
	memcpy(out + n, make, mlen);
	n += mlen;
	memcpy(out + n, jpg + 2, jlen - 2);
	n += jlen - 2;
	img2_free(jpg);
	*len = n;
	return out;
}

CTEST(sec_fix_cli, info_text_exif_make_with_escape)
{
	/* OSC 0 (window title) and CSI 2J (clear screen) in Make: decoded, then printed as by --info */
	size_t len = 0;
	uint8_t *data = jpeg_with_make("Evil\x1b]0;pwned\x07\x1b[2J", &len);
	ASSERT_NOT_NULL(data);
	img2_frames_t fr = { 0 };
	ASSERT_EQUAL(IMG2_OK, img2_decode(data, len, NULL, &fr));
	ASSERT_NOT_NULL(fr.frames[0]->exif);
	ASSERT_STR("Evil\x1b]0;pwned\x07\x1b[2J", fr.frames[0]->exif->make);
	cap_t c;
	cap_begin(&c, STDOUT_FILENO, "make");
	output_exif_text(fr.frames[0]->exif);
	size_t n = 0;
	char *out = cap_end(&c, &n);
	ASSERT_NOT_NULL(out);
	if (count_controls(out, n) != 0) {
		fprintf(stderr, "  stdout: %s\n", out);
	}
	ASSERT_EQUAL(0, count_controls(out, n));
	ASSERT_NOT_NULL(strstr(out, "  Make: Evil\\x1b]0;pwned\\x07\\x1b[2J\n"));
	free(out);
	img2_frames_free(&fr);
	free(data);
}

#define ESC_FIELD(dst, text) snprintf((dst), sizeof(dst), "%s\x1b[31m", (text))

CTEST(sec_fix_cli, info_text_every_field_escaped)
{
	img2_exif_t e;
	img2_exif_init(&e);
	ESC_FIELD(e.make, "Make");
	ESC_FIELD(e.model, "Model");
	ESC_FIELD(e.software, "Soft");
	ESC_FIELD(e.lens_model, "Lens");
	e.has_datetime = true;
	ESC_FIELD(e.date_time, "2020");
	e.has_gps = true;
	e.gps_latitude_ref = '\x1b';
	e.gps_longitude_ref = '\x9b';
	ESC_FIELD(e.gps_datestamp, "2021");
	ESC_FIELD(e.gps_timestamp, "12");
	ESC_FIELD(e.artist, "Artist");
	ESC_FIELD(e.copyright, "Copy");
	ESC_FIELD(e.description, "Desc");
	ESC_FIELD(e.user_comment, "Comment");

	img2_xmp_t x;
	img2_xmp_init(&x);
	ESC_FIELD(x.creator, "Creator");
	ESC_FIELD(x.title, "Title");
	ESC_FIELD(x.description, "Desc");
	x.has_keywords = true;
	x.keywords = strdup("a,\x1b[31mb");
	ESC_FIELD(x.copyright, "Copy");
	ESC_FIELD(x.label, "Label");
	ESC_FIELD(x.create_date, "C");
	ESC_FIELD(x.modify_date, "M");
	ESC_FIELD(x.city, "City");
	ESC_FIELD(x.country, "Country");
	ESC_FIELD(x.credit, "Credit");
	ESC_FIELD(x.source, "Source");

	cap_t c;
	cap_begin(&c, STDOUT_FILENO, "fields");
	output_exif_text(&e);
	output_xmp_text(&x);
	/* the other branches of the Make/Model and Location lines */
	e.make[0] = '\0';
	output_exif_text(&e);
	x.city[0] = '\0';
	output_xmp_text(&x);
	size_t n = 0;
	char *out = cap_end(&c, &n);
	ASSERT_NOT_NULL(out);
	if (count_controls(out, n) != 0) {
		fprintf(stderr, "  stdout: %s\n", out);
	}
	ASSERT_EQUAL(0, count_controls(out, n));
	ASSERT_NOT_NULL(strstr(out, "  Make/Model: Make\\x1b[31m Model\\x1b[31m\n"));
	ASSERT_NOT_NULL(strstr(out, "  Model: Model\\x1b[31m\n"));
	ASSERT_NOT_NULL(strstr(out, "  Latitude: 0.000000\\x1b\n"));
	ASSERT_NOT_NULL(strstr(out, "  Longitude: 0.000000\\x9b\n"));
	ASSERT_NOT_NULL(strstr(out, "  Date: 2021\\x1b[31m 12\\x1b[31m\n"));
	ASSERT_NOT_NULL(strstr(out, "  Location: City\\x1b[31m, Country\\x1b[31m\n"));
	ASSERT_NOT_NULL(strstr(out, "  Location: Country\\x1b[31m\n"));
	ASSERT_NOT_NULL(strstr(out, "  Keywords: a,\\x1b[31mb\n"));
	/* first pass: 11 EXIF strings + the latitude ref, 12 XMP strings; second pass: no Make, no city */
	ASSERT_EQUAL((11 + 1) + 12 + (10 + 1) + 11, count_str(out, "\\x1b"));
	free(out);
	img2_xmp_clear(&x);
	img2_exif_clear(&e);
}

CTEST(sec_fix_cli, info_text_gps_absent_reference_unchanged)
{
	/* an absent GPS reference ('\0') is still the NUL byte "%c" wrote (existing output unchanged) */
	img2_exif_t e;
	img2_exif_init(&e);
	e.has_gps = true;
	e.gps_latitude = 1.5;
	cap_t c;
	cap_begin(&c, STDOUT_FILENO, "gps");
	output_exif_text(&e);
	size_t n = 0;
	char *out = cap_end(&c, &n);
	ASSERT_NOT_NULL(out);
	static const char expect[] = "\n\nGPS:\n  Latitude: 1.500000\0\n  Longitude: 0.000000\0\n";
	ASSERT_EQUAL((int)sizeof(expect) - 1, (int)n);
	ASSERT_TRUE(memcmp(out, expect, n) == 0);
	free(out);
	img2_exif_clear(&e);
}

/* ---- the helper itself ---- */

static char *safe_fputs_text(const char *s)
{
	char path[512];
	const char *dir = getenv("TMPDIR");
	snprintf(path, sizeof(path), "%s/sec_fix_cli_fputs_%ld.txt", dir != NULL && dir[0] != '\0' ? dir : IMGCAT2_TEST_TMPDIR, (long)getpid());
	FILE *f = fopen(path, "w+b");
	ASSERT_NOT_NULL(f);
	cli_fputs_safe(s, f);
	char *buf = calloc(1, 4096);
	ASSERT_NOT_NULL(buf);
	rewind(f);
	size_t n = fread(buf, 1, 4095, f);
	buf[n] = '\0';
	fclose(f);
	unlink(path);
	return buf;
}

CTEST(sec_fix_cli, safe_text_escapes)
{
	static const struct {
		const char *in;
		const char *out;
	} cases[] = {
		{ "plain ASCII ~", "plain ASCII ~" },
		{ "\x01\t\n\x1f\x7f", "\\x01\t\n\\x1f\\x7f" },
		{ "line 1\n\tline 2\x1b[2J\r\n", "line 1\n\tline 2\\x1b[2J\\x0d\n" }, /* \n, \t raw; ESC, CR escaped */
		{ "\x80\x9b\x9f", "\\x80\\x9b\\x9f" },                        /* raw C1 bytes */
		{ "\xc2\x80\xc2\x9b\xc2\x9f", "\\xc2\\x80\\xc2\\x9b\\xc2\\x9f" }, /* UTF-8 C1 */
		{ "\xc2\xa0\xc3\xa9\xdf\xbf", "\xc2\xa0\xc3\xa9\xdf\xbf" },     /* 2-byte UTF-8 */
		{ "\xe0\xa0\x80\xe2\x82\xac\xed\x9f\xbf\xef\xbf\xbd", "\xe0\xa0\x80\xe2\x82\xac\xed\x9f\xbf\xef\xbf\xbd" },
		{ "\xf0\x90\x80\x80\xf3\xbf\xbf\xbf\xf4\x8f\xbf\xbf", "\xf0\x90\x80\x80\xf3\xbf\xbf\xbf\xf4\x8f\xbf\xbf" },
		/* invalid: overlong E0 80, surrogate ED A0, F0 80, F4 90, bad continuation, truncated at the end,
		 * C0 / C1 / F5 lead bytes: the bytes >= 0xA0 stay, 0x80-0x9F are escaped */
		{ "\xe0\x80\x80", "\xe0\\x80\\x80" },
		{ "\xed\xa0\x80", "\xed\xa0\\x80" },
		{ "\xf0\x80\x80\x80", "\xf0\\x80\\x80\\x80" },
		{ "\xf4\x90\x80\x80", "\xf4\\x90\\x80\\x80" },
		{ "\xe2\x82\x41", "\xe2\\x82\x41" },
		{ "\xe2\x82", "\xe2\\x82" },
		{ "\xc0\xaf\xc1\xbf\xf5\x80", "\xc0\xaf\xc1\xbf\xf5\\x80" },
		{ "\xc3", "\xc3" },
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		char *got = safe_fputs_text(cases[i].in);
		if (strcmp(got, cases[i].out) != 0) {
			fprintf(stderr, "  case %zu: got \"%s\"\n", i, got);
		}
		ASSERT_STR(cases[i].out, got);
		free(got);

		char buf[256];
		ASSERT_STR(cases[i].out, cli_safe_str(cases[i].in, buf, sizeof(buf)));
	}

	/* NULL */
	char *got = safe_fputs_text(NULL);
	ASSERT_STR("", got);
	free(got);
	char buf[8];
	ASSERT_STR("", cli_safe_str(NULL, buf, sizeof(buf)));

	/* too small: cut before the first piece that does not fit, never inside an escape or a sequence */
	ASSERT_STR("abc", cli_safe_str("abc\x1b" "def", buf, 6));
	ASSERT_STR("", cli_safe_str("abcdefgh", buf, 8));
	ASSERT_STR("a", cli_safe_str("a\xe2\x82\xac", buf, 4));
	ASSERT_STR("a\xe2\x82\xac", cli_safe_str("a\xe2\x82\xac", buf, 5));
	ASSERT_STR("a\\x1b", cli_safe_str("a\x1b\x1b", buf, 6));
}

/* stderr of op(ctx) must hold the path escaped, no control character */
static void expect_path_escaped(const char *what, void (*op)(void *ctx), void *ctx, const char *expect)
{
	cap_t c;
	cap_begin(&c, STDERR_FILENO, "path");
	op(ctx);
	size_t n = 0;
	char *err = cap_end(&c, &n);
	ASSERT_NOT_NULL(err);
	if (count_controls(err, n) != 0 || strstr(err, expect) == NULL) {
		fprintf(stderr, "  %s: stderr: %s\n", what, err);
	}
	ASSERT_EQUAL(0, count_controls(err, n));
	ASSERT_NOT_NULL(strstr(err, expect));
	free(err);
}

static void op_read_file(void *ctx)
{
	uint8_t *data = NULL;
	size_t size = 0;
	ASSERT_FALSE(read_file_secure((const char *)ctx, &data, &size));
}

typedef struct {
	char *output_file;
	bool silent;
	int expect_rc;
} render_ctx_t;

static void op_file_render(void *ctx)
{
	const render_ctx_t *r = ctx;
	img2_image_t *img = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_create(2, 2, &img));
	cli_options_t opts;
	memset(&opts, 0, sizeof(opts));
	opts.output_format = IMG2_FORMAT_PNG;
	opts.png_compression = 1;
	opts.output_file = r->output_file;
	opts.silent = r->silent;
	ASSERT_EQUAL(r->expect_rc, file_render(&img, 1, &opts));
	img2_image_destroy(img);
}

CTEST(sec_fix_cli, paths_escaped_in_messages)
{
	const char *dir = getenv("TMPDIR");
	if (dir == NULL || dir[0] == '\0') {
		dir = IMGCAT2_TEST_TMPDIR;
	}
	char base[600];
	snprintf(base, sizeof(base), "%s/sec_fix_cli_%ld", dir, (long)getpid());
	ASSERT_EQUAL(0, mkdir(base, 0700));
	char p[1024];

	/* realpath fails */
	snprintf(p, sizeof(p), "%s/missing\x1b]0;t\x07.png", base);
	expect_path_escaped("resolve", op_read_file, p, "missing\\x1b]0;t\\x07.png': ");

	/* not a regular file */
	snprintf(p, sizeof(p), "%s/dir\x1b[2J", base);
	ASSERT_EQUAL(0, mkdir(p, 0700));
	expect_path_escaped("not regular", op_read_file, p, "Not a regular file: ");
	expect_path_escaped("not regular", op_read_file, p, "dir\\x1b[2J\n");

	/* file_render: the existing directory as the output file (cannot open) */
	render_ctx_t r = { p, true, -1 };
	expect_path_escaped("open for writing", op_file_render, &r, "dir\\x1b[2J' for writing");

	/* empty file */
	snprintf(p, sizeof(p), "%s/empty\x1b[2J", base);
	FILE *f = fopen(p, "wb");
	ASSERT_NOT_NULL(f);
	fclose(f);
	expect_path_escaped("empty", op_read_file, p, "File is empty: ");
	unlink(p);

	/* larger than IMG2_MAX_FILE_SIZE (sparse) */
	snprintf(p, sizeof(p), "%s/big\x1b[2J", base);
	f = fopen(p, "wb");
	ASSERT_NOT_NULL(f);
	ASSERT_EQUAL(0, ftruncate(fileno(f), (off_t)IMG2_MAX_FILE_SIZE + 1));
	fclose(f);
	expect_path_escaped("too large", op_read_file, p, "big\\x1b[2J\n");
	unlink(p);

	/* unreadable (as non-root) */
	snprintf(p, sizeof(p), "%s/noread\x1b[2J", base);
	f = fopen(p, "wb");
	ASSERT_NOT_NULL(f);
	fputc('x', f);
	fclose(f);
	ASSERT_EQUAL(0, chmod(p, 0));
	if (geteuid() != 0) {
		expect_path_escaped("cannot open", op_read_file, p, "noread\\x1b[2J': ");
	}
	unlink(p);

	/* file_render: parent directory missing (both messages), success message */
	snprintf(p, sizeof(p), "%s/nodir\x1b[2J/out.png", base);
	r.output_file = p;
	expect_path_escaped("parent", op_file_render, &r, "Parent directory does not exist: ");
	expect_path_escaped("parent", op_file_render, &r, "Invalid output file path: ");
	expect_path_escaped("parent", op_file_render, &r, "nodir\\x1b[2J/out.png\n");
	snprintf(p, sizeof(p), "%s/out\x1b[2J.png", base);
	r.silent = false;
	r.expect_rc = 0;
	expect_path_escaped("written", op_file_render, &r, "out\\x1b[2J.png\n");
	unlink(p);

	/* file_render: parent part longer than PATH_MAX */
	char *longp = malloc(PATH_MAX + 64);
	ASSERT_NOT_NULL(longp);
	memset(longp, 'a', PATH_MAX + 32);
	longp[0] = '\x1b';
	longp[PATH_MAX + 8] = '/';
	longp[PATH_MAX + 32] = '\0';
	r.output_file = longp;
	r.silent = true;
	r.expect_rc = -1;
	expect_path_escaped("too long", op_file_render, &r, "Path too long: \\x1baaaa");
	free(longp);

	snprintf(p, sizeof(p), "%s/dir\x1b[2J", base);
	rmdir(p);
	rmdir(base);
}

/* ---- P39: command-line arguments echoed in argument-error messages ---- */

/* parse_arguments() (and validate_options() if it parsed) on argv; returns stderr (free()) */
static char *cli_errors(int argc, char **argv, int expect_parse)
{
	cli_options_t opts;
	memset(&opts, 0, sizeof(opts));
	opts.fps = 15;
	cap_t c;
	cap_begin(&c, STDERR_FILENO, "args");
	optind = 0; /* glibc: full re-initialisation of getopt */
	int rc = parse_arguments(argc, argv, &opts);
	if (rc == 0) {
		rc = validate_options(&opts);
	}
	size_t n = 0;
	char *err = cap_end(&c, &n);
	ASSERT_EQUAL(expect_parse, rc);
	ASSERT_NOT_NULL(err);
	return err;
}

CTEST(sec_fix_cli, argument_errors_escaped)
{
	static const struct {
		const char *opt;
		const char *arg;
		const char *expect;
	} cases[] = {
		{ "-w", "1\x1b[2J", "(got '1\\x1b[2J')" },
		{ "--iterm2-format", "x\x1b]0;t\x07", "Invalid iterm2-format 'x\\x1b]0;t\\x07'" },
		{ "--yuv", "nv12\x1b[2J:1:1", "Invalid --yuv 'nv12\\x1b[2J:1:1': " },
		{ "-i", "bogus\x1b[2J", "Invalid interpolation method 'bogus\\x1b[2J'" },
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		char *argv[] = { (char *)"imgcat2", (char *)cases[i].opt, (char *)cases[i].arg, NULL };
		char *err = cli_errors(3, argv, -1);
		if (count_controls(err, strlen(err)) != 0 || strstr(err, cases[i].expect) == NULL) {
			fprintf(stderr, "  case %zu: stderr: %s\n", i, err);
		}
		ASSERT_EQUAL(0, count_controls(err, strlen(err)));
		ASSERT_NOT_NULL(strstr(err, cases[i].expect));
		free(err);
	}

	/* unknown option: our "Try" line escapes argv[0] (getopt's own message before it is glibc's) */
	char *argv[] = { (char *)"img\x1b[2Jcat", (char *)"--no-such-option", NULL };
	char *err = cli_errors(2, argv, -1);
	ASSERT_NOT_NULL(strstr(err, "Try 'img\\x1b[2Jcat --help' for more information.\n"));
	free(err);
}
