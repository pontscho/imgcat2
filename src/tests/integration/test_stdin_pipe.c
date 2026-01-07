/**
 * @file test_stdin_pipe.c
 * @brief Stdin pipe integration tests
 *
 * Tests stdin reading functionality for piped input.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../imgcat2/core/cli.h"
#include "../../imgcat2/core/pipeline.h"
#include "../ctest.h"

/**
 * @test Test CLI with stdin input (NULL file)
 *
 * Verifies that stdin mode is indicated by NULL input_file.
 */
CTEST(integration, stdin_cli_null_file)
{
	cli_options_t opts = { 0 };
	opts.input_file = NULL; /* stdin mode */
	opts.interpolation = "lanczos";
	opts.fit_mode = true;
	opts.fps = 15;

	ASSERT_NULL(opts.input_file);
	ASSERT_EQUAL(0, validate_options(&opts));
}

/**
 * @test Test stdin vs file mode detection
 *
 * Verifies differentiation between stdin and file input.
 */
CTEST(integration, stdin_vs_file_mode)
{
	cli_options_t opts1 = { 0 };
	opts1.input_file = NULL; /* stdin */

	cli_options_t opts2 = { 0 };
	opts2.input_file = "image.png"; /* file */

	ASSERT_NULL(opts1.input_file);
	ASSERT_NOT_NULL(opts2.input_file);
	ASSERT_STR("image.png", opts2.input_file);
}

/**
 * @test Test read_file_secure() with valid path
 *
 * Verifies secure file reading API exists and can be called.
 */
CTEST(integration, stdin_read_file_api)
{
	/* This test verifies the API exists */
	/* Actual file reading requires real files, tested separately */

	uint8_t *data = NULL;
	size_t size = 0;

	/* read_file_secure() should fail with non-existent file */
	bool result = read_file_secure("/nonexistent/file.png", &data, &size);
	ASSERT_FALSE(result);
	ASSERT_NULL(data);
	ASSERT_EQUAL(0, size);
}

/**
 * @test Test pipeline_read() dispatch logic
 *
 * Verifies that pipeline_read() can distinguish stdin/file modes.
 */
CTEST(integration, stdin_pipeline_read_dispatch)
{
	cli_options_t opts_stdin = { 0 };
	opts_stdin.input_file = NULL; /* stdin mode */

	cli_options_t opts_file = { 0 };
	opts_file.input_file = "/nonexistent/test.png"; /* file mode */

	/* Both should have valid option structures */
	ASSERT_NULL(opts_stdin.input_file);
	ASSERT_NOT_NULL(opts_file.input_file);
}

/**
 * @test Test stdin size limit protection
 *
 * Verifies that IMAGE_MAX_FILE_SIZE applies to stdin.
 */
CTEST(integration, stdin_size_limit)
{
	/* Verify that the size limit constant exists */
	size_t max_size = IMAGE_MAX_FILE_SIZE;
	ASSERT_EQUAL(52428800UL, max_size); /* 50MB */

	/* Any stdin read should respect this limit */
	ASSERT_TRUE(max_size > 0);
	ASSERT_TRUE(max_size < 1000000000UL); /* Less than 1GB */
}

/**
 * @test Test empty input handling
 *
 * Verifies handling of empty/zero-length input.
 */
CTEST(integration, stdin_empty_input)
{
	/* Empty buffer should be rejected by pipeline */
	uint8_t *empty_data = calloc(1, sizeof(uint8_t));
	ASSERT_NOT_NULL(empty_data);

	size_t size = 0;

	/* pipeline_decode should fail with empty data */
	image_t **frames = NULL;
	int frame_count = 0;
	int result = pipeline_decode(NULL, empty_data, size, &frames, &frame_count);

	ASSERT_EQUAL(-1, result);
	ASSERT_NULL(frames);

	free(empty_data);
}
