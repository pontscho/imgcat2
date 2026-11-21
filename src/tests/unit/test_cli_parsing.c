/**
 * @file test_cli_parsing.c
 * @brief Unit tests for CLI argument parsing and validation
 *
 * Tests command-line argument parsing, option validation, help/version
 * display, FPS range validation, and interpolation method validation.
 */

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../imgcat2/cli.h"
#include "../ctest.h"

/**
 * @test Test parsing of basic file input
 *
 * Verifies that parse_arguments() correctly parses a simple filename.
 */
CTEST(cli, parse_basic_file)
{
	cli_options_t opts = {
		.input_file = NULL,
		.interpolation = "lanczos",
		.fit_mode = true,
		.silent = false,
		.fps = 15,
		.animate = true,
	};

	/* Reset getopt state */
	optind = 1;

	char *argv[] = { "imgcat2", "test.png" };
	int argc = 2;

	int result = parse_arguments(argc, argv, &opts);

	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(opts.input_file);
	ASSERT_STR("test.png", opts.input_file);
}

/**
 * @test Test parsing of stdin input (no file argument)
 *
 * Verifies that parse_arguments() sets input_file to NULL for stdin.
 */
CTEST(cli, parse_stdin)
{
	cli_options_t opts = {
		.input_file = NULL,
		.interpolation = "lanczos",
		.fit_mode = true,
		.silent = false,
		.fps = 15,
		.animate = true,
	};

	/* Reset getopt state */
	optind = 1;

	char *argv[] = { "imgcat2" };
	int argc = 1;

	int result = parse_arguments(argc, argv, &opts);

	ASSERT_EQUAL(0, result);
	ASSERT_NULL(opts.input_file);
}

/**
 * @test Test parsing of all command-line options
 *
 * Verifies that parse_arguments() correctly parses all supported options.
 */
CTEST(cli, parse_options)
{
	cli_options_t opts = {
		.input_file = NULL,
		.interpolation = "lanczos",
		.fit_mode = true,
		.silent = true,
		.fps = 15,
		.animate = true,
	};

	/* Reset getopt state */
	optind = 1;

	char *argv[] = { "imgcat2", "-o", "output.jpg", "-i", "bilinear", "--fps", "20", "-v", "test.png" };
	int argc = 9;

	int result = parse_arguments(argc, argv, &opts);

	ASSERT_EQUAL(0, result);
	ASSERT_STR("bilinear", opts.interpolation);
	ASSERT_EQUAL(20, opts.fps);
	ASSERT_FALSE(opts.silent); /* -v sets silent to false */
	ASSERT_NOT_NULL(opts.input_file);
	ASSERT_STR("test.png", opts.input_file);
}

/**
 * @test Test parsing of long-form options
 *
 * Verifies that parse_arguments() correctly parses long-form options (--option).
 */
CTEST(cli, parse_long_options)
{
	cli_options_t opts = {
		.input_file = NULL,
		.interpolation = "lanczos",
		.fit_mode = true,
		.silent = true,
		.fps = 15,
		.animate = true,
	};

	/* Reset getopt state */
	optind = 1;

	char *argv[] = { "imgcat2", "--interpolation", "cubic", "--fps", "10", "--verbose", "animation.gif" };
	int argc = 7;

	int result = parse_arguments(argc, argv, &opts);

	ASSERT_EQUAL(0, result);
	ASSERT_STR("cubic", opts.interpolation);
	ASSERT_EQUAL(10, opts.fps);
	ASSERT_FALSE(opts.silent); /* --verbose sets silent to false */
	ASSERT_NOT_NULL(opts.input_file);
	ASSERT_STR("animation.gif", opts.input_file);
}

/**
 * @test Test parsing of invalid arguments
 *
 * Verifies that parse_arguments() rejects invalid command-line arguments.
 * Note: getopt global state (optind) persists between calls,
 * so this test may be affected by previous tests.
 */
CTEST(cli, parse_invalid)
{
	cli_options_t opts = {
		.input_file = NULL,
		.interpolation = "lanczos",
		.fit_mode = true,
		.silent = false,
		.fps = 15,
		.animate = true,
	};

	/* Reset getopt global state */
	optind = 1;
	opterr = 0; /* Disable error messages during test */

	/* Invalid option */
	char *argv1[] = { "imgcat2", "--invalid-option", "test.png" };
	int argc1 = 3;

	int result1 = parse_arguments(argc1, argv1, &opts);
	ASSERT_NOT_EQUAL(0, result1);

	/* Reset getopt for next test */
	optind = 1;

	/* Missing value for option that requires argument */
	char *argv2[] = { "imgcat2", "-o" }; /* -o requires an argument */
	int argc2 = 2;

	int result2 = parse_arguments(argc2, argv2, &opts);
	ASSERT_NOT_EQUAL(0, result2);

	/* Re-enable error messages */
	opterr = 1;
}

/**
 * @test Test FPS range validation
 *
 * Verifies that validate_options() correctly validates FPS range (1-15).
 */
CTEST(cli, validate_fps)
{
	cli_options_t opts = {
		.input_file = NULL,
		.interpolation = "lanczos",
		.fit_mode = true,
		.silent = false,
		.fps = 15,
		.animate = true,
	};

	/* Valid FPS: 1 */
	opts.fps = 1;
	ASSERT_EQUAL(0, validate_options(&opts));

	/* Valid FPS: 15 */
	opts.fps = 15;
	ASSERT_EQUAL(0, validate_options(&opts));

	/* Valid FPS: 10 (middle) */
	opts.fps = 10;
	ASSERT_EQUAL(0, validate_options(&opts));

	/* Invalid FPS: 0 (too low) */
	opts.fps = 0;
	ASSERT_NOT_EQUAL(0, validate_options(&opts));

	/* Invalid FPS: 16 (too high) */
	opts.fps = 16;
	ASSERT_NOT_EQUAL(0, validate_options(&opts));

	/* Invalid FPS: -1 (negative) */
	opts.fps = -1;
	ASSERT_NOT_EQUAL(0, validate_options(&opts));

	/* Invalid FPS: 100 (way too high) */
	opts.fps = 100;
	ASSERT_NOT_EQUAL(0, validate_options(&opts));
}

/**
 * @test Test interpolation method validation
 *
 * Verifies that validate_options() correctly validates interpolation method names.
 */
CTEST(cli, validate_interpolation)
{
	cli_options_t opts = {
		.input_file = NULL,
		.interpolation = "lanczos",
		.fit_mode = true,
		.silent = false,
		.fps = 10,
		.animate = true,
	};

	/* Valid: lanczos */
	opts.interpolation = "lanczos";
	ASSERT_EQUAL(0, validate_options(&opts));

	/* Valid: bilinear */
	opts.interpolation = "bilinear";
	ASSERT_EQUAL(0, validate_options(&opts));

	/* Valid: nearest */
	opts.interpolation = "nearest";
	ASSERT_EQUAL(0, validate_options(&opts));

	/* Valid: cubic */
	opts.interpolation = "cubic";
	ASSERT_EQUAL(0, validate_options(&opts));

	/* Invalid: unknown method */
	opts.interpolation = "unknown";
	ASSERT_NOT_EQUAL(0, validate_options(&opts));

	/* Valid: NULL is allowed (validation skipped) */
	opts.interpolation = NULL;
	ASSERT_EQUAL(0, validate_options(&opts));

	/* Invalid: empty string */
	opts.interpolation = "";
	ASSERT_NOT_EQUAL(0, validate_options(&opts)); /* Empty string is not NULL, so it should fail validation */

	/* Invalid: wrong case */
	opts.interpolation = "LANCZOS";
	ASSERT_NOT_EQUAL(0, validate_options(&opts));
}

/**
 * @test Test validate_options() with NULL pointer
 *
 * Verifies that validate_options() handles NULL opts safely.
 */
CTEST(cli, validate_null)
{
	int result = validate_options(NULL);
	ASSERT_NOT_EQUAL(0, result);
}

/**
 * @test Test print_usage() function
 *
 * Verifies that print_usage() can be called without crashing.
 */
CTEST(cli, print_usage_function)
{
	/* This function outputs to stdout */
	/* We just verify it doesn't crash */
	print_usage("imgcat2");

	/* Test passes if no crash occurred */
	ASSERT_TRUE(true);
}

/**
 * @test Test print_version() function
 *
 * Verifies that print_version() can be called without crashing.
 */
CTEST(cli, print_version_function)
{
	/* This function outputs to stdout */
	/* We just verify it doesn't crash */
	print_version();

	/* Test passes if no crash occurred */
	ASSERT_TRUE(true);
}

/**
 * @test Test parsing with animation enabled
 *
 * Verifies that parse_arguments() correctly handles -a/--animate flag.
 */
CTEST(cli, parse_no_animate)
{
	cli_options_t opts = {
		.input_file = NULL,
		.interpolation = "lanczos",
		.fit_mode = true,
		.silent = false,
		.fps = 15,
		.animate = false, /* Start with animate false */
	};

	/* Reset getopt state */
	optind = 1;

	char *argv[] = { "imgcat2", "--animate", "animation.gif" };
	int argc = 3;

	int result = parse_arguments(argc, argv, &opts);

	ASSERT_EQUAL(0, result);
	ASSERT_TRUE(opts.animate); /* Should be true after --animate */
	ASSERT_NOT_NULL(opts.input_file);
	ASSERT_STR("animation.gif", opts.input_file);
}

/**
 * @test Test parsing with resize mode
 *
 * Verifies that parse_arguments() correctly handles -r/--resize flag.
 */
CTEST(cli, parse_resize_mode)
{
	cli_options_t opts = {
		.input_file = NULL,
		.interpolation = "lanczos",
		.fit_mode = true,
		.silent = false,
		.fps = 15,
		.animate = true,
	};

	/* Reset getopt state */
	optind = 1;

	char *argv[] = { "imgcat2", "-r", "test.png" };
	int argc = 3;

	int result = parse_arguments(argc, argv, &opts);

	ASSERT_EQUAL(0, result);
	ASSERT_FALSE(opts.fit_mode); /* -r should set fit_mode to false */
	ASSERT_NOT_NULL(opts.input_file);
	ASSERT_STR("test.png", opts.input_file);
}

/**
 * @test Test parse_arguments() with NULL opts
 *
 * Verifies that parse_arguments() handles NULL opts parameter.
 */
CTEST(cli, parse_null_opts)
{
	char *argv[] = { "imgcat2", "test.png" };
	int argc = 2;

	int result = parse_arguments(argc, argv, NULL);
	ASSERT_NOT_EQUAL(0, result);
}

/**
 * @test Test parsing of a valid --yuv specification
 *
 * Verifies that --yuv enables raw YUV input and stores resolved geometry.
 */
CTEST(cli, parse_yuv_valid)
{
	cli_options_t opts = {
		.input_file = NULL,
		.interpolation = "lanczos",
		.fps = 15,
	};

	optind = 1;
	char *argv[] = { "imgcat2", "--yuv", "NV12:97:61", "frame.yuv" };
	ASSERT_EQUAL(0, parse_arguments(4, argv, &opts));
	ASSERT_TRUE(opts.yuv_input);
	ASSERT_EQUAL(YUV_FMT_NV12, opts.yuv.format);
	ASSERT_EQUAL(97, opts.yuv.width);
	ASSERT_EQUAL(61, opts.yuv.height);
	ASSERT_EQUAL(97, opts.yuv.y_stride);
	ASSERT_EQUAL(98, opts.yuv.uv_stride);
	ASSERT_STR("frame.yuv", opts.input_file);

	/* Explicit strides, stdin input */
	cli_options_t opts2 = { .interpolation = "lanczos", .fps = 15 };
	optind = 1;
	char *argv2[] = { "imgcat2", "--yuv=yuv420p:97:61:128:64", "-" };
	ASSERT_EQUAL(0, parse_arguments(3, argv2, &opts2));
	ASSERT_TRUE(opts2.yuv_input);
	ASSERT_EQUAL(YUV_FMT_I420, opts2.yuv.format);
	ASSERT_EQUAL(128, opts2.yuv.y_stride);
	ASSERT_EQUAL(64, opts2.yuv.uv_stride);
	ASSERT_NULL(opts2.input_file);

	/* Without --yuv the flag stays off */
	cli_options_t opts3 = { .interpolation = "lanczos", .fps = 15 };
	optind = 1;
	char *argv3[] = { "imgcat2", "test.png" };
	ASSERT_EQUAL(0, parse_arguments(2, argv3, &opts3));
	ASSERT_FALSE(opts3.yuv_input);
}

/**
 * @test Test rejection of invalid --yuv specifications
 *
 * Missing fields, bad format name, zero dimensions, too-small strides and
 * garbage numbers must all make parse_arguments() fail.
 */
CTEST(cli, parse_yuv_invalid)
{
	static char *bad_specs[] = {
		"i420", /* missing fields */
		"i420:64", /* missing height */
		"rgb565:64:64", /* bad format name */
		"i420:0:64", /* zero width */
		"i420:64:0", /* zero height */
		"i420:64:64:32", /* y stride too small */
		"nv12:64:64:64:32", /* uv stride too small (min 64) */
		"yuyv:64:64:64", /* packed stride too small (min 128) */
		"gray:64:64:64:64", /* uv stride not applicable */
		"i420:6x4:64", /* garbage number */
		"i420:64:-1", /* negative */
		"i420:64:64:abc", /* garbage stride */
		"i420:99999999999999999999999:64", /* overflow */
		"i420:64:64:64:32:1", /* too many fields */
	};

	opterr = 0;
	for (size_t i = 0; i < sizeof(bad_specs) / sizeof(bad_specs[0]); i++) {
		cli_options_t opts = { .interpolation = "lanczos", .fps = 15 };
		optind = 1;
		char *argv[] = { "imgcat2", "--yuv", bad_specs[i], "frame.yuv" };
		ASSERT_EQUAL(-1, parse_arguments(4, argv, &opts));
		ASSERT_FALSE(opts.yuv_input);
	}

	/* Missing argument */
	cli_options_t opts = { .interpolation = "lanczos", .fps = 15 };
	optind = 1;
	char *argv[] = { "imgcat2", "--yuv" };
	ASSERT_NOT_EQUAL(0, parse_arguments(2, argv, &opts));
	opterr = 1;
}

/**
 * @test Test combined short options
 *
 * Verifies that parse_arguments() can handle multiple short options.
 */
CTEST(cli, parse_combined_short_options)
{
	cli_options_t opts = {
		.input_file = NULL,
		.interpolation = "lanczos",
		.fit_mode = true,
		.silent = true,
		.fps = 15,
		.animate = true,
	};

	/* Reset getopt state */
	optind = 1;

	char *argv[] = { "imgcat2", "-v", "-o", "output.jpg", "test.png" };
	int argc = 5;

	int result = parse_arguments(argc, argv, &opts);

	ASSERT_EQUAL(0, result);
	ASSERT_FALSE(opts.silent); /* -v sets silent to false */
	ASSERT_NOT_NULL(opts.input_file);
	ASSERT_STR("test.png", opts.input_file);
}
