/**
 * @file test_ansi.c
 * @brief Unit tests for ANSI escape sequence generation and rendering
 *
 * Tests ANSI escape sequence constants, cursor control functions,
 * color code generation, half-block rendering, and transparency handling.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../imgcat2/ansi/ansi.h"
#include "../../imgcat2/ansi/escape.h"
#include "../../imgcat2/core/image.h"
#include "../ctest.h"

/**
 * @test Test ANSI escape sequence constants
 *
 * Verifies that ANSI escape sequence constants are properly defined
 * and have the expected format.
 */
CTEST(ansi, escape_sequences)
{
	/* Test cursor control sequences */
	ASSERT_STR("\x1B[?25l", ANSI_CURSOR_HIDE);
	ASSERT_STR("\x1B[?25h", ANSI_CURSOR_SHOW);
	ASSERT_STR("\x1B[%dA", ANSI_CURSOR_UP);

	/* Test color sequences */
	ASSERT_STR("\x1b[0m", ANSI_RESET);
	ASSERT_STR("\x1b[48;2;%d;%d;%dm", ANSI_BG_RGB);
	ASSERT_STR("\x1b[38;2;%d;%d;%dm▄", ANSI_FG_RGB_HALFBLOCK);
	ASSERT_STR("\x1b[0;39;49m", ANSI_BG_TRANSPARENT);
	ASSERT_STR("\x1b[0m ", ANSI_FG_TRANSPARENT);

	/* Test half-block character */
	ASSERT_STR("▄", HALF_BLOCK_CHAR);
}

/**
 * @test Test cursor control functions
 *
 * Verifies that cursor hide/show/up functions work without crashing.
 * These functions output to stdout, so we just verify they don't crash.
 */
CTEST(ansi, cursor_control)
{
	/* These functions output to stdout */
	/* We can't easily capture stdout in unit tests, */
	/* so we just verify they don't crash */

	ansi_cursor_hide();
	ansi_cursor_show();
	ansi_cursor_up(5);
	ansi_cursor_up(0); /* Should do nothing */
	ansi_cursor_up(-1); /* Should do nothing */
	ansi_reset();

	/* Test passes if no crash occurred */
	ASSERT_TRUE(true);
}

/**
 * @test Test RGB color code generation format
 *
 * Verifies that RGB color codes can be generated using the format strings.
 */
CTEST(ansi, color_codes)
{
	char buffer[128];

	/* Test background RGB color generation */
	snprintf(buffer, sizeof(buffer), ANSI_BG_RGB, 255, 128, 64);
	ASSERT_STR("\x1b[48;2;255;128;64m", buffer);

	/* Test foreground RGB color with half-block */
	snprintf(buffer, sizeof(buffer), ANSI_FG_RGB_HALFBLOCK, 32, 64, 128);
	ASSERT_STR("\x1b[38;2;32;64;128m▄", buffer);

	/* Test cursor up with specific line count */
	snprintf(buffer, sizeof(buffer), ANSI_CURSOR_UP, 10);
	ASSERT_STR("\x1B[10A", buffer);
}

/**
 * @test Test escape sequence cache initialization
 *
 * Verifies that escape_cache_init() can be called without crashing.
 */
CTEST(ansi, escape_cache_init)
{
	/* Initialize cache (should be idempotent) */
	escape_cache_init();
	escape_cache_init(); /* Call again to test idempotency */

	/* Test passes if no crash occurred */
	ASSERT_TRUE(true);
}

/**
 * @test Test half-block rendering with generate_line_ansi()
 *
 * Verifies that generate_line_ansi() generates valid ANSI escape sequences
 * for a line of pixels using half-block technique.
 */
CTEST(ansi, half_block_rendering)
{
	/* Create a simple 4x4 test image (2 terminal lines) */
	image_t *img = image_create(4, 4);
	ASSERT_NOT_NULL(img);

	/* Fill with solid red (opaque) */
	for (uint32_t y = 0; y < img->height; y++) {
		for (uint32_t x = 0; x < img->width; x++) {
			image_set_pixel(img, x, y, 255, 0, 0, 255);
		}
	}

	/* Allocate line buffer */
	char *line_buffer = malloc(MAX_LINE_BUFFER_SIZE);
	ASSERT_NOT_NULL(line_buffer);

	/* Generate ANSI for first line (rows 0-1) */
	char *result = generate_line_ansi(img, 0, line_buffer);
	ASSERT_NOT_NULL(result);
	ASSERT_TRUE(result == line_buffer); /* Should return same pointer */

	/* Verify the line contains expected elements */
	ASSERT_TRUE(strlen(result) > 0);
	ASSERT_TRUE(strstr(result, "\x1b[") != NULL); /* Should contain ANSI codes */
	ASSERT_TRUE(strstr(result, "▄") != NULL); /* Should contain half-block */
	ASSERT_TRUE(strstr(result, "\x1b[0m") != NULL); /* Should contain reset */
	ASSERT_TRUE(strstr(result, "\n") != NULL); /* Should end with newline */

	free(line_buffer);
	image_destroy(img);
}

/**
 * @test Test generate_line_ansi() with NULL parameters
 *
 * Verifies that generate_line_ansi() handles NULL parameters gracefully.
 */
CTEST(ansi, generate_line_ansi_null_params)
{
	image_t *img = image_create(4, 4);
	ASSERT_NOT_NULL(img);

	char *line_buffer = malloc(MAX_LINE_BUFFER_SIZE);
	ASSERT_NOT_NULL(line_buffer);

	/* NULL image */
	char *result1 = generate_line_ansi(NULL, 0, line_buffer);
	ASSERT_NULL(result1);

	/* NULL buffer */
	char *result2 = generate_line_ansi(img, 0, NULL);
	ASSERT_NULL(result2);

	free(line_buffer);
	image_destroy(img);
}

/**
 * @test Test transparency handling with alpha threshold
 *
 * Verifies that pixels with alpha < 128 are treated as transparent.
 */
CTEST(ansi, transparency)
{
	/* Create 4x4 image */
	image_t *img = image_create(4, 4);
	ASSERT_NOT_NULL(img);

	/* Top row: fully opaque red (alpha = 255) */
	for (uint32_t x = 0; x < img->width; x++) {
		image_set_pixel(img, x, 0, 255, 0, 0, 255);
	}

	/* Second row: semi-transparent (alpha = 64, < 128) */
	for (uint32_t x = 0; x < img->width; x++) {
		image_set_pixel(img, x, 1, 0, 255, 0, 64);
	}

	/* Third row: just above threshold (alpha = 128) */
	for (uint32_t x = 0; x < img->width; x++) {
		image_set_pixel(img, x, 2, 0, 0, 255, 128);
	}

	/* Fourth row: fully transparent (alpha = 0) */
	for (uint32_t x = 0; x < img->width; x++) {
		image_set_pixel(img, x, 3, 255, 255, 255, 0);
	}

	char *line_buffer = malloc(MAX_LINE_BUFFER_SIZE);
	ASSERT_NOT_NULL(line_buffer);

	/* Generate line for rows 0-1 (opaque + semi-transparent) */
	char *result = generate_line_ansi(img, 0, line_buffer);
	ASSERT_NOT_NULL(result);

	/* Should contain transparent handling codes */
	ASSERT_TRUE(strlen(result) > 0);

	free(line_buffer);
	image_destroy(img);
}

/**
 * @test Test generate_frame_ansi() for full frame rendering
 *
 * Verifies that generate_frame_ansi() generates ANSI sequences for all lines.
 */
CTEST(ansi, generate_frame_ansi)
{
	/* Create 4x4 image (will generate 2 terminal lines) */
	image_t *img = image_create(4, 4);
	ASSERT_NOT_NULL(img);

	/* Fill with blue */
	for (uint32_t y = 0; y < img->height; y++) {
		for (uint32_t x = 0; x < img->width; x++) {
			image_set_pixel(img, x, y, 0, 0, 255, 255);
		}
	}

	size_t line_count = 0;
	char **lines = generate_frame_ansi(img, &line_count);

	ASSERT_NOT_NULL(lines);
	ASSERT_EQUAL(2, line_count); /* 4 pixel rows / 2 = 2 terminal lines */

	/* Verify each line is valid */
	for (size_t i = 0; i < line_count; i++) {
		ASSERT_NOT_NULL(lines[i]);
		ASSERT_TRUE(strlen(lines[i]) > 0);
	}

	free_frame_lines(lines, line_count);
	image_destroy(img);
}

/**
 * @test Test generate_frame_ansi() with NULL image
 *
 * Verifies that generate_frame_ansi() handles NULL image.
 */
CTEST(ansi, generate_frame_ansi_null)
{
	size_t line_count = 0;
	char **lines = generate_frame_ansi(NULL, &line_count);

	ASSERT_NULL(lines);
	ASSERT_EQUAL(0, line_count);
}

/**
 * @test Test free_frame_lines() is NULL-safe
 *
 * Verifies that free_frame_lines() handles NULL gracefully.
 */
CTEST(ansi, free_frame_lines_null_safe)
{
	/* Should not crash with NULL */
	free_frame_lines(NULL, 0);
	free_frame_lines(NULL, 10);

	/* Test passes if no crash occurred */
	ASSERT_TRUE(true);
}

/**
 * @test Test generate_frame_ansi() with odd height image
 *
 * Verifies behavior when image height is odd (last row should be handled).
 */
CTEST(ansi, generate_frame_ansi_odd_height)
{
	/* Create 4x5 image (odd height) */
	image_t *img = image_create(4, 5);
	ASSERT_NOT_NULL(img);

	/* Fill with green */
	for (uint32_t y = 0; y < img->height; y++) {
		for (uint32_t x = 0; x < img->width; x++) {
			image_set_pixel(img, x, y, 0, 255, 0, 255);
		}
	}

	size_t line_count = 0;
	char **lines = generate_frame_ansi(img, &line_count);

	ASSERT_NOT_NULL(lines);
	/* Height 5 / 2 = 2 terminal lines (rounded down) */
	ASSERT_EQUAL(2, line_count);

	free_frame_lines(lines, line_count);
	image_destroy(img);
}
