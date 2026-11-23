/**
 * @file test_cli_fix.c
 * @brief Fixes of the imgcat2 command line (argument parsing, messages, --info output); stdout / stderr captured
 *
 * -F / -b: the short forms of --fps / --version did not match their long options (-F read a missing argument:
 *          `imgcat2 -F` crashed; -b took an argument --version does not have).
 */

#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <img2/img2.h>

#include "../../imgcat2/cli.h"
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
