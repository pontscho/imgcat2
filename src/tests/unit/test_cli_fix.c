/**
 * @file test_cli_fix.c
 * @brief Fixes of the imgcat2 command line (argument parsing, messages, --info output); stdout / stderr captured
 *
 * -F / -b: the short forms of --fps / --version did not match their long options (-F read a missing argument:
 *          `imgcat2 -F` crashed; -b took an argument --version does not have).
 * JSON:    --info --json escaped every byte >= 0x80 as \u00XX (signed char): UTF-8 text came out as mojibake.
 * GPS:     --info printed the signed value with the letter ("-121.314136W") and a NUL byte for no reference.
 * -w / -H: checked against the terminal size even when converting to a file (--jpeg/--png/--heif/--webp/--jxl).
 * Errors:  "-w or -h" (-h is --help) and "requires --jpeg or --png" (HEIF / WebP / JXL missing).
 * Frames:  converting an animated image printed "Animated image with N frames, converting frame K" without -v.
 * Exit:    --help / --version exited with status 1; with no terminal "Failed to get terminal size" was printed
 *          three times, without -v and even for --help / --version.
 * --help:  called -f/--fit the default; the default is the original size, only shrunk to the terminal.
 */

#include <sys/types.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <img2/img2.h>

#include "../../imgcat2/cli.h"
#include "../../imgcat2/metadata.h"
#include "../../imgcat2/terminal/file.h"
#include "../ctest.h"

#ifdef IMG2_HAVE_EXIF_READER
#include "../../libimg2/metadata/exif_reader.h"
#endif

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
	snprintf(c->path, sizeof(c->path), "%s/cli_fix_%s_%ld.txt", dir, name, (long)getpid());
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

/* the defaults of main.c that matter to parse_arguments() / validate_options() */
static void opts_defaults(cli_options_t *opts)
{
	memset(opts, 0, sizeof(*opts));
	opts->interpolation = "lanczos";
	opts->silent = true;
	opts->fps = 15;
	opts->target_width = -1;
	opts->target_height = -1;
	opts->jpeg_quality = 90;
	opts->png_compression = 6;
	opts->heif_quality = 80;
	opts->webp_quality = 80;
	opts->jxl_quality = 80;
	opts->avif_quality = 80;
	opts->terminal.rows = 24;
	opts->terminal.cols = 80;
}

/* parse_arguments() on argv with stdout and stderr captured; returns its result, the output in *out / *err (free()) */
static int parse_captured(int argc, char **argv, cli_options_t *opts, char **out, char **err)
{
	cap_t co;
	cap_t ce;
	cap_begin(&co, STDOUT_FILENO, "out");
	cap_begin(&ce, STDERR_FILENO, "err");
	optind = 0; /* glibc: full re-initialisation of getopt */
	int rc = parse_arguments(argc, argv, opts);
	size_t n = 0;
	*err = cap_end(&ce, &n);
	*out = cap_end(&co, &n);
	ASSERT_NOT_NULL(*out);
	ASSERT_NOT_NULL(*err);
	return rc;
}

/* ---- -F / -b: short forms consistent with --fps / --version ---- */

CTEST(cli_fix, short_fps_takes_argument)
{
	cli_options_t opts;
	char *out = NULL;
	char *err = NULL;

	/* -F N: the FPS, like --fps N */
	opts_defaults(&opts);
	char *argv1[] = { (char *)"imgcat2", (char *)"-F", (char *)"10", (char *)"x.png", NULL };
	ASSERT_EQUAL(0, parse_captured(4, argv1, &opts, &out, &err));
	ASSERT_EQUAL(10, opts.fps);
	ASSERT_STR("x.png", opts.input_file);
	ASSERT_EQUAL(0, validate_options(&opts));
	free(out);
	free(err);

	/* -F without its argument: a usage error, no crash */
	opts_defaults(&opts);
	char *argv2[] = { (char *)"imgcat2", (char *)"-F", NULL };
	ASSERT_EQUAL(-1, parse_captured(2, argv2, &opts, &out, &err));
	ASSERT_NOT_NULL(strstr(err, "Try 'imgcat2 --help'"));
	free(out);
	free(err);

	/* -F outside 1..15: rejected by validate_options() like --fps */
	static const char *const bad[] = { "0", "16", "abc" };
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		opts_defaults(&opts);
		char *argv3[] = { (char *)"imgcat2", (char *)"-F", (char *)bad[i], NULL };
		ASSERT_EQUAL(0, parse_captured(3, argv3, &opts, &out, &err));
		free(out);
		free(err);
		cap_t c;
		cap_begin(&c, STDERR_FILENO, "fps");
		int rc = validate_options(&opts);
		size_t n = 0;
		err = cap_end(&c, &n);
		ASSERT_EQUAL(-1, rc);
		ASSERT_NOT_NULL(strstr(err, "Error: FPS must be between 1 and 15"));
		free(err);
	}
}

CTEST(cli_fix, short_version_takes_no_argument)
{
	cli_options_t opts;
	char *out = NULL;
	char *err = NULL;

	/* -b alone: the version, like --version (it required an argument) */
	opts_defaults(&opts);
	char *argv1[] = { (char *)"imgcat2", (char *)"-b", NULL };
	ASSERT_EQUAL(1, parse_captured(2, argv1, &opts, &out, &err));
	ASSERT_NOT_NULL(strstr(out, "imgcat2 version "));
	ASSERT_STR("", err);
	free(out);
	free(err);

	/* --version x: x is the input file operand, not an argument of the option (same for -b) */
	opts_defaults(&opts);
	char *argv2[] = { (char *)"imgcat2", (char *)"--version", (char *)"x.png", NULL };
	ASSERT_EQUAL(1, parse_captured(3, argv2, &opts, &out, &err));
	ASSERT_NOT_NULL(strstr(out, "imgcat2 version "));
	free(out);
	free(err);
}

/* ---- -w / -H in conversion mode: not limited by the terminal size ---- */

/* validate_options() with stderr captured; returns its result, stderr in *err (free()) */
static int validate_captured(cli_options_t *opts, char **err)
{
	cap_t c;
	cap_begin(&c, STDERR_FILENO, "validate");
	int rc = validate_options(opts);
	size_t n = 0;
	*err = cap_end(&c, &n);
	ASSERT_NOT_NULL(*err);
	return rc;
}

CTEST(cli_fix, convert_dimensions_not_terminal_bound)
{
	static const img2_format_t formats[] = { IMG2_FORMAT_JPEG, IMG2_FORMAT_PNG, IMG2_FORMAT_HEIF, IMG2_FORMAT_WEBP, IMG2_FORMAT_JXL, IMG2_FORMAT_AVIF };
	cli_options_t opts;
	char *err = NULL;
	for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
		/* 80 x 24 terminal: -w 1000 -H 600 is far beyond it, fine for a conversion */
		opts_defaults(&opts);
		opts.convert_mode = true;
		opts.output_format = formats[i];
		opts.has_custom_dimensions = true;
		opts.target_width = 1000;
		opts.target_height = 600;
		ASSERT_EQUAL(0, validate_captured(&opts, &err));
		ASSERT_STR("", err);
		free(err);

		/* at least one dimension must still be positive */
		opts.target_width = 0;
		opts.target_height = 0;
		ASSERT_EQUAL(-1, validate_captured(&opts, &err));
		free(err);
	}

	/* terminal rendering: still limited by the terminal */
	opts_defaults(&opts);
	opts.has_custom_dimensions = true;
	opts.target_width = 1000;
	ASSERT_EQUAL(-1, validate_captured(&opts, &err));
	ASSERT_NOT_NULL(strstr(err, "Error: Width must be between 1 and 80 pixels (got 1000)"));
	free(err);
	opts.target_width = -1;
	opts.target_height = 600;
	ASSERT_EQUAL(-1, validate_captured(&opts, &err));
	ASSERT_NOT_NULL(strstr(err, "Error: Height must be between 1 and 48 pixels (got 600)"));
	free(err);
}

/* ---- error messages: -H (not -h, which is --help), every conversion format ---- */

CTEST(cli_fix, validate_messages)
{
	cli_options_t opts;
	char *err = NULL;

	opts_defaults(&opts);
	opts.has_custom_dimensions = true;
	opts.target_width = 0;
	opts.target_height = 0;
	ASSERT_EQUAL(-1, validate_captured(&opts, &err));
	ASSERT_STR("Error: At least one of -w or -H must be positive\n", err);
	free(err);

	opts_defaults(&opts);
	opts.convert_mode = true;
	opts.output_format = IMG2_FORMAT_NONE;
	ASSERT_EQUAL(-1, validate_captured(&opts, &err));
	ASSERT_STR("Error: Conversion mode requires --jpeg, --png, --heif, --webp, --jxl or --avif\n", err);
	free(err);

	/* file_render() without a format: the same list */
	img2_image_t *img = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_create(2, 2, &img));
	opts_defaults(&opts);
	cap_t c;
	cap_begin(&c, STDERR_FILENO, "render");
	int rc = file_render(&img, 1, &opts);
	size_t n = 0;
	err = cap_end(&c, &n);
	img2_image_destroy(img);
	ASSERT_EQUAL(-1, rc);
	ASSERT_STR("Error: No output format specified (use --jpeg, --png, --heif, --webp, --jxl or --avif)\n", err);
	free(err);
}

/* ---- --avif Q: long-only conversion option, 0..100, out of range is an error ---- */

CTEST(cli_fix, avif_option)
{
	cli_options_t opts;
	char *out = NULL;
	char *err = NULL;

	opts_defaults(&opts);
	char *argv1[] = { (char *)"imgcat2", (char *)"--avif", (char *)"55", (char *)"-o", (char *)"x.avif", (char *)"in.png", NULL };
	ASSERT_EQUAL(0, parse_captured(6, argv1, &opts, &out, &err));
	ASSERT_TRUE(opts.convert_mode);
	ASSERT_EQUAL(IMG2_FORMAT_AVIF, opts.output_format);
	ASSERT_EQUAL(55, opts.avif_quality);
	ASSERT_STR("x.avif", opts.output_file);
	ASSERT_STR("in.png", opts.input_file);
	free(out);
	free(err);
	ASSERT_EQUAL(0, validate_captured(&opts, &err));
	ASSERT_STR("", err);
	free(err);

	/* bounds: 0 and 100 accepted, -1 and 101 rejected like --heif/--webp/--jxl */
	static const struct {
		const char *q;
		int rc;
		const char *msg;
	} cases[] = {
		{ "0",   0,  ""                                           },
		{ "100", 0,  ""                                           },
		{ "-1",  -1, "Error: AVIF quality must be 0-100 (got -1)\n"  },
		{ "101", -1, "Error: AVIF quality must be 0-100 (got 101)\n" },
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		opts_defaults(&opts);
		char *argv2[] = { (char *)"imgcat2", (char *)"--avif", (char *)cases[i].q, (char *)"in.png", NULL };
		ASSERT_EQUAL(0, parse_captured(4, argv2, &opts, &out, &err));
		free(out);
		free(err);
		ASSERT_EQUAL(cases[i].rc, validate_captured(&opts, &err));
		ASSERT_STR(cases[i].msg, err);
		free(err);
	}

	/* --avif without its argument: a usage error */
	opts_defaults(&opts);
	char *argv3[] = { (char *)"imgcat2", (char *)"--avif", NULL };
	ASSERT_EQUAL(-1, parse_captured(2, argv3, &opts, &out, &err));
	ASSERT_NOT_NULL(strstr(err, "Try 'imgcat2 --help'"));
	free(out);
	free(err);

	/* --help lists it with the other conversion formats */
	opts_defaults(&opts);
	char *argv4[] = { (char *)"imgcat2", (char *)"--help", NULL };
	ASSERT_EQUAL(1, parse_captured(2, argv4, &opts, &out, &err));
	ASSERT_NOT_NULL(strstr(out, "      --avif <quality>      Convert to AVIF (quality: 0-100, default: 80)\n"));
	free(out);
	free(err);
}

#ifdef IMG2_HAVE_HEIF
/* file_render() with --avif: the default quality, written to stdout, is an AVIF of the source size */
CTEST(cli_fix, avif_conversion)
{
	img2_image_t *img = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_create(32, 24, &img));
	for (uint32_t y = 0; y < 24; y++) {
		for (uint32_t x = 0; x < 32; x++) {
			uint8_t *p = img->pixels + (size_t)y * img->stride + x * 4;
			p[0] = (uint8_t)(x * 8);
			p[1] = (uint8_t)(y * 10);
			p[2] = 100;
			p[3] = 255;
		}
	}
	cli_options_t opts;
	opts_defaults(&opts);
	opts.convert_mode = true;
	opts.output_format = IMG2_FORMAT_AVIF;
	cap_t co;
	cap_t ce;
	cap_begin(&co, STDOUT_FILENO, "avif_out");
	cap_begin(&ce, STDERR_FILENO, "avif_err");
	int rc = file_render(&img, 1, &opts);
	size_t n_err = 0;
	size_t n_out = 0;
	char *err = cap_end(&ce, &n_err);
	char *out = cap_end(&co, &n_out);
	img2_image_destroy(img);
	ASSERT_EQUAL(0, rc);
	ASSERT_STR("", err);
	ASSERT_TRUE(n_out > 0 && n_out < 65535);
	ASSERT_EQUAL(IMG2_MIME_AVIF, img2_detect_mime((const uint8_t *)out, n_out));
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, img2_decode((const uint8_t *)out, n_out, NULL, &fr));
	ASSERT_EQUAL(32, fr.frames[0]->width);
	ASSERT_EQUAL(24, fr.frames[0]->height);
	img2_frames_free(&fr);
	free(out);
	free(err);
}
#endif

/* ---- --frame N of an animated image: the "converting frame" note only with -v ---- */

/* file_render() of a 2-frame image to stdout (discarded); returns its stderr (free()) */
static char *render_two_frames(bool silent)
{
	img2_image_t *imgs[2] = { NULL, NULL };
	ASSERT_EQUAL(IMG2_OK, img2_image_create(2, 2, &imgs[0]));
	ASSERT_EQUAL(IMG2_OK, img2_image_create(2, 2, &imgs[1]));
	cli_options_t opts;
	opts_defaults(&opts);
	opts.convert_mode = true;
	opts.output_format = IMG2_FORMAT_PNG;
	opts.png_compression = 1;
	opts.frame_index = 1;
	opts.silent = silent;
	cap_t co;
	cap_t ce;
	cap_begin(&co, STDOUT_FILENO, "frames_out");
	cap_begin(&ce, STDERR_FILENO, "frames_err");
	int rc = file_render(imgs, 2, &opts);
	size_t n = 0;
	char *err = cap_end(&ce, &n);
	free(cap_end(&co, &n));
	img2_image_destroy(imgs[0]);
	img2_image_destroy(imgs[1]);
	ASSERT_EQUAL(0, rc);
	ASSERT_NOT_NULL(err);
	return err;
}

CTEST(cli_fix, animated_frame_note_verbose_only)
{
	char *err = render_two_frames(true);
	ASSERT_NULL(strstr(err, "Animated image"));
	free(err);

	err = render_two_frames(false);
	ASSERT_NOT_NULL(strstr(err, "Animated image with 2 frames, converting frame 1\n"));
	free(err);
}

/* ---- --help: fit to the terminal is -f, not the default ---- */

CTEST(cli_fix, help_fit_not_default)
{
	cap_t c;
	cap_begin(&c, STDOUT_FILENO, "help");
	print_usage("imgcat2");
	size_t n = 0;
	char *out = cap_end(&c, &n);
	ASSERT_NOT_NULL(out);
	/* main.c starts with fit_mode = false: the original size, only shrunk to the terminal */
	ASSERT_NULL(strstr(out, "maintain aspect ratio, default"));
	ASSERT_NULL(strstr(out, "If neither: fit to terminal (default)"));
	ASSERT_NOT_NULL(strstr(out, "  -f, --fit                 Fit image to the terminal (scale up or down, keep aspect ratio)\n"));
	ASSERT_NOT_NULL(strstr(out, "If neither: original size, shrunk to fit the terminal\n"));
	free(out);
}

#ifdef IMGCAT2_BIN
/* ---- the imgcat2 binary: exit status of --help / --version, the terminal size warning ---- */

typedef struct {
	int rc;    /* exit status, -1 if it did not exit normally */
	char *out; /* stdout (free()) */
	char *err; /* stderr (free()) */
} run_t;

/* read and unlink a capture file; NUL-terminated, free() */
static char *slurp(const char *path)
{
	FILE *f = fopen(path, "rb");
	char *buf = calloc(1, 65536);
	ASSERT_NOT_NULL(f);
	ASSERT_NOT_NULL(buf);
	size_t n = fread(buf, 1, 65535, f);
	buf[n] = '\0';
	fclose(f);
	unlink(path);
	return buf;
}

/* run IMGCAT2_BIN with argv[1..] in a new session (no controlling terminal), stdin /dev/null, stdout / stderr
 * into files: terminal_get_size() fails, as when the output is piped */
static run_t run_imgcat2(char *const argv[])
{
	const char *dir = getenv("TMPDIR");
	if (dir == NULL || dir[0] == '\0') {
		dir = IMGCAT2_TEST_TMPDIR;
	}
	char out_path[512];
	char err_path[512];
	snprintf(out_path, sizeof(out_path), "%s/cli_fix_run_out_%ld.txt", dir, (long)getpid());
	snprintf(err_path, sizeof(err_path), "%s/cli_fix_run_err_%ld.txt", dir, (long)getpid());
	fflush(stdout);
	fflush(stderr);
	pid_t pid = fork();
	ASSERT_TRUE(pid >= 0);
	if (pid == 0) {
		int in = open("/dev/null", O_RDONLY);
		int out = open(out_path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
		int err = open(err_path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
		if (in < 0 || out < 0 || err < 0 || setsid() < 0 || dup2(in, STDIN_FILENO) < 0 || dup2(out, STDOUT_FILENO) < 0 || dup2(err, STDERR_FILENO) < 0) {
			_exit(126);
		}
		execv(IMGCAT2_BIN, argv);
		_exit(127);
	}
	int status = 0;
	ASSERT_EQUAL(pid, waitpid(pid, &status, 0));
	run_t r = { WIFEXITED(status) ? WEXITSTATUS(status) : -1, slurp(out_path), slurp(err_path) };
	return r;
}

static int count_str(const char *s, const char *needle)
{
	int n = 0;
	for (const char *p = strstr(s, needle); p != NULL; p = strstr(p + 1, needle)) {
		n++;
	}
	return n;
}

CTEST(cli_fix, help_version_exit_success)
{
	char *help[] = { (char *)"imgcat2", (char *)"--help", NULL };
	run_t r = run_imgcat2(help);
	ASSERT_EQUAL(0, r.rc);
	ASSERT_NOT_NULL(strstr(r.out, "Usage: imgcat2 "));
	ASSERT_STR("", r.err); /* no terminal size warning */
	free(r.out);
	free(r.err);

	char *version[] = { (char *)"imgcat2", (char *)"--version", NULL };
	r = run_imgcat2(version);
	ASSERT_EQUAL(0, r.rc);
	ASSERT_NOT_NULL(strstr(r.out, "imgcat2 version "));
	ASSERT_STR("", r.err);
	free(r.out);
	free(r.err);

	/* argument errors stay failures */
	char *bad[] = { (char *)"imgcat2", (char *)"-i", (char *)"bogus", (char *)"x.png", NULL };
	r = run_imgcat2(bad);
	ASSERT_EQUAL(1, r.rc);
	ASSERT_NOT_NULL(strstr(r.err, "Error: Invalid interpolation method 'bogus'"));
	free(r.out);
	free(r.err);

	char *unknown[] = { (char *)"imgcat2", (char *)"--no-such-option", NULL };
	r = run_imgcat2(unknown);
	ASSERT_EQUAL(1, r.rc);
	free(r.out);
	free(r.err);
}

CTEST(cli_fix, terminal_size_warning_verbose_once)
{
	char *quiet[] = { (char *)"imgcat2", (char *)"--info", (char *)IMG2_TEST_SRC_DIR "/data/png/rgb8_srgb.png", NULL };
	run_t r = run_imgcat2(quiet);
	ASSERT_EQUAL(0, r.rc);
	ASSERT_NOT_NULL(strstr(r.out, "PNG image/png "));
	if (r.err[0] != '\0') {
		fprintf(stderr, "  stderr: %s\n", r.err);
	}
	ASSERT_STR("", r.err);
	free(r.out);
	free(r.err);

	char *verbose[] = { (char *)"imgcat2", (char *)"-v", (char *)"--info", (char *)IMG2_TEST_SRC_DIR "/data/png/rgb8_srgb.png", NULL };
	r = run_imgcat2(verbose);
	ASSERT_EQUAL(0, r.rc);
	if (count_str(r.err, "terminal size") != 1) {
		fprintf(stderr, "  stderr: %s\n", r.err);
	}
	ASSERT_EQUAL(1, count_str(r.err, "terminal size"));
	ASSERT_NOT_NULL(strstr(r.err, "Warning: Failed to get terminal size, using defaults\n"));
	free(r.out);
	free(r.err);
}
#endif /* IMGCAT2_BIN */

#ifdef IMG2_HAVE_EXIF_READER
/* ---- --info --json strings: UTF-8 passed through, controls escaped ---- */

CTEST(cli_fix, json_string_utf8)
{
	static const struct {
		const char *in;
		const char *out; /* the JSON string */
	} cases[] = {
		{ "Zolt\xc3\xa1n", "\"Zolt\xc3\xa1n\"" },                                   /* 2-byte UTF-8 */
		{ "\xe2\x82\xac \xf0\x9f\x98\x80", "\"\xe2\x82\xac \xf0\x9f\x98\x80\"" },   /* 3- and 4-byte UTF-8 */
		{ "a\"b\\c/", "\"a\\\"b\\\\c/\"" },
		{ "\b\f\n\r\t", "\"\\b\\f\\n\\r\\t\"" },                                    /* short forms */
		{ "\x01\x1b\x1f\x7f", "\"\\u0001\\u001b\\u001f\\u007f\"" },                 /* other C0, DEL */
		{ "\xc2\x80\xc2\x9b\xc2\x9f\xc2\xa0", "\"\\u0080\\u009b\\u009f\xc2\xa0\"" }, /* UTF-8 C1; U+00A0 stays */
		/* invalid UTF-8: one U+FFFD per byte (raw C1 byte, Latin-1 byte, truncated / overlong / surrogate) */
		{ "x\x9b\xe1y", "\"x\xef\xbf\xbd\xef\xbf\xbdy\"" },
		{ "\xe2\x82", "\"\xef\xbf\xbd\xef\xbf\xbd\"" },
		{ "\xc0\xaf", "\"\xef\xbf\xbd\xef\xbf\xbd\"" },
		{ "\xed\xa0\x80", "\"\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\"" },
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		img2_exif_t e;
		img2_exif_init(&e);
		snprintf(e.artist, sizeof(e.artist), "%s", cases[i].in);
		cap_t c;
		cap_begin(&c, STDOUT_FILENO, "json");
		output_exif_json(&e, true);
		size_t n = 0;
		char *out = cap_end(&c, &n);
		img2_exif_clear(&e);
		char expect[256];
		snprintf(expect, sizeof(expect), "\"exif\":{\"rights\":{\"artist\":%s}}", cases[i].out);
		if (strcmp(out, expect) != 0) {
			fprintf(stderr, "  case %zu: got %s\n", i, out);
		}
		ASSERT_STR(expect, out);
		free(out);
	}
}

/* ---- --info GPS lines: the hemisphere letter with the absolute value ---- */

CTEST(cli_fix, info_text_gps_hemisphere)
{
	img2_exif_t e;
	img2_exif_init(&e);
	e.has_gps = true;
	e.gps_latitude = -33.5;
	e.gps_latitude_ref = 'S';
	e.gps_longitude = -121.314136;
	e.gps_longitude_ref = 'W';
	cap_t c;
	cap_begin(&c, STDOUT_FILENO, "gps");
	output_exif_text(&e);
	e.gps_latitude = 47.5;
	e.gps_latitude_ref = 'N';
	e.gps_longitude = 19.04;
	e.gps_longitude_ref = 'E';
	output_exif_text(&e);
	size_t n = 0;
	char *out = cap_end(&c, &n);
	ASSERT_NOT_NULL(out);
	static const char expect[] = "\n\nGPS:\n  Latitude: 33.500000S\n  Longitude: 121.314136W\n"
	                             "\n\nGPS:\n  Latitude: 47.500000N\n  Longitude: 19.040000E\n";
	if (strcmp(out, expect) != 0) {
		fprintf(stderr, "  stdout: %s\n", out);
	}
	ASSERT_STR(expect, out);
	free(out);

	/* JSON keeps the signed numbers */
	e.gps_latitude = -33.5;
	e.gps_longitude = -121.314136;
	cap_begin(&c, STDOUT_FILENO, "gps_json");
	output_exif_json(&e, true);
	out = cap_end(&c, &n);
	ASSERT_STR("\"exif\":{\"gps\":{\"latitude\":-33.500000,\"longitude\":-121.314136}}", out);
	free(out);
	img2_exif_clear(&e);
}
#endif /* IMG2_HAVE_EXIF_READER */
