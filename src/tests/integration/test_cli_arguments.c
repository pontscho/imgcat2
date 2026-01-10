/**
 * @file test_cli_arguments.c
 * @brief CLI argument parsing integration tests
 *
 * Tests command-line argument parsing and validation in integration context.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../imgcat2/core/cli.h"
#include "../ctest.h"

/**
 * @test Test CLI options initialization with defaults
 *
 * Verifies that CLI options structure can be initialized.
 */
CTEST(integration, cli_defaults)
{
	cli_options_t opts = { 0 };

	/* Default values */
	opts.input_file = NULL;
	opts.interpolation = "lanczos";
	opts.fit_mode = true;
	opts.silent = false;
	opts.fps = 15;
	opts.animate = false;

	/* Validate defaults */
	ASSERT_NULL(opts.input_file);
	ASSERT_NOT_NULL(opts.interpolation);
	ASSERT_TRUE(opts.fit_mode);
	ASSERT_FALSE(opts.silent);
	ASSERT_EQUAL(15, opts.fps);
	ASSERT_FALSE(opts.animate);
}

/**
 * @test Test validate_options() with valid options
 *
 * Verifies that valid CLI options pass validation.
 */
CTEST(integration, cli_validate_valid_options)
{
	cli_options_t opts = { 0 };
	opts.input_file = "test.png";
	opts.interpolation = "lanczos";
	opts.fit_mode = true;
	opts.silent = false;
	opts.fps = 15;
	opts.animate = false;

	int result = validate_options(&opts);
	ASSERT_EQUAL(0, result);
}

/**
 * @test Test validate_options() with invalid FPS
 *
 * Verifies that invalid FPS values are rejected.
 */
CTEST(integration, cli_validate_invalid_fps)
{
	cli_options_t opts = { 0 };
	opts.input_file = "test.png";
	opts.interpolation = "lanczos";
	opts.fit_mode = true;
	opts.silent = false;
	opts.animate = true;

	/* FPS too low (< 1) */
	opts.fps = 0;
	int result = validate_options(&opts);
	ASSERT_EQUAL(-1, result);

	/* FPS too high (> 15) */
	opts.fps = 20;
	result = validate_options(&opts);
	ASSERT_EQUAL(-1, result);

	/* FPS in valid range */
	opts.fps = 10;
	result = validate_options(&opts);
	ASSERT_EQUAL(0, result);
}

/**
 * @test Test validate_options() with invalid interpolation
 *
 * Verifies that invalid interpolation methods are rejected.
 */
CTEST(integration, cli_validate_invalid_interpolation)
{
	cli_options_t opts = { 0 };
	opts.input_file = "test.png";
	opts.interpolation = "invalid_method";
	opts.fit_mode = true;
	opts.silent = false;
	opts.fps = 15;
	opts.animate = false;

	int result = validate_options(&opts);
	ASSERT_EQUAL(-1, result);
}

/**
 * @test Test validate_options() with valid interpolation methods
 *
 * Verifies that all valid interpolation methods pass validation.
 */
CTEST(integration, cli_validate_valid_interpolations)
{
	cli_options_t opts = { 0 };
	opts.input_file = "test.png";
	opts.fit_mode = true;
	opts.silent = false;
	opts.fps = 15;
	opts.animate = false;

	/* Test each valid interpolation method */
	const char *valid_methods[] = { "lanczos", "bilinear", "nearest", "cubic" };

	for (int i = 0; i < 4; i++) {
		opts.interpolation = (char *)valid_methods[i];
		int result = validate_options(&opts);
		ASSERT_EQUAL(0, result);
	}
}

/**
 * @test Test CLI option combinations
 *
 * Verifies various valid CLI option combinations.
 */
CTEST(integration, cli_option_combinations)
{
	cli_options_t opts;

	/* Combination 1: Fit mode with default FPS */
	opts = (cli_options_t){ 0 };
	opts.input_file = "image.png";
	opts.interpolation = "lanczos";
	opts.fit_mode = true;
	opts.fps = 15;
	opts.animate = false;
	ASSERT_EQUAL(0, validate_options(&opts));

	/* Combination 2: Animation enabled with custom FPS */
	opts = (cli_options_t){ 0 };
	opts.input_file = "animation.gif";
	opts.interpolation = "bilinear";
	opts.fit_mode = true;
	opts.fps = 10;
	opts.animate = true;
	ASSERT_EQUAL(0, validate_options(&opts));

	/* Combination 3: Silent mode */
	opts = (cli_options_t){ 0 };
	opts.input_file = "test.jpg";
	opts.interpolation = "nearest";
	opts.fit_mode = true;
	opts.silent = true;
	opts.fps = 15;
	ASSERT_EQUAL(0, validate_options(&opts));
}

/**
 * @test Test FPS boundary values
 *
 * Verifies FPS boundary validation (1 and 15).
 */
CTEST(integration, cli_fps_boundaries)
{
	cli_options_t opts = { 0 };
	opts.input_file = "test.gif";
	opts.interpolation = "lanczos";
	opts.fit_mode = true;
	opts.animate = true;

	/* Lower boundary (1 fps) */
	opts.fps = 1;
	ASSERT_EQUAL(0, validate_options(&opts));

	/* Upper boundary (15 fps) */
	opts.fps = 15;
	ASSERT_EQUAL(0, validate_options(&opts));

	/* Below lower boundary (0 fps) */
	opts.fps = 0;
	ASSERT_EQUAL(-1, validate_options(&opts));

	/* Above upper boundary (16 fps) */
	opts.fps = 16;
	ASSERT_EQUAL(-1, validate_options(&opts));
}

/**
 * @test Test stdin input (NULL input_file)
 *
 * Verifies that stdin input (NULL file) is valid.
 */
CTEST(integration, cli_stdin_input)
{
	cli_options_t opts = { 0 };
	opts.input_file = NULL; /* stdin */
	opts.interpolation = "lanczos";
	opts.fit_mode = true;
	opts.fps = 15;

	int result = validate_options(&opts);
	ASSERT_EQUAL(0, result);
}
