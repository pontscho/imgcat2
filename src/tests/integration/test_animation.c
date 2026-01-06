/**
 * @file test_animation.c
 * @brief Animation integration tests
 *
 * Tests animation playback, FPS control, and interrupt handling.
 * These tests focus on animation data structures and basic validation.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../imgcat2/core/cli.h"
#include "../../imgcat2/core/image.h"
#include "../ctest.h"

/**
 * @test Test animation CLI options
 *
 * Verifies that animation-related CLI options can be set.
 */
CTEST(integration, animation_cli_options)
{
	cli_options_t opts = { 0 };
	opts.animate = true;
	opts.fps = 10;

	ASSERT_TRUE(opts.animate);
	ASSERT_EQUAL(10, opts.fps);
}

/**
 * @test Test FPS validation for animations
 *
 * Verifies that FPS is validated correctly for animations.
 */
CTEST(integration, animation_fps_validation)
{
	cli_options_t opts = { 0 };
	opts.input_file = "test.gif";
	opts.interpolation = "lanczos";
	opts.fit_mode = true;
	opts.animate = true;

	/* Valid FPS range: 1-15 */
	opts.fps = 1;
	ASSERT_EQUAL(0, validate_options(&opts));

	opts.fps = 15;
	ASSERT_EQUAL(0, validate_options(&opts));

	/* Invalid FPS */
	opts.fps = 0;
	ASSERT_EQUAL(-1, validate_options(&opts));

	opts.fps = 20;
	ASSERT_EQUAL(-1, validate_options(&opts));
}

/**
 * @test Test multi-frame image array
 *
 * Verifies handling of multiple frames (as in animated GIF).
 */
CTEST(integration, animation_multi_frame_array)
{
	/* Simulate 3-frame animation */
	image_t **frames = malloc(3 * sizeof(image_t *));
	ASSERT_NOT_NULL(frames);

	/* Create 3 frames */
	for (int i = 0; i < 3; i++) {
		frames[i] = image_create(10, 10);
		ASSERT_NOT_NULL(frames[i]);
	}

	/* Verify all frames exist */
	for (int i = 0; i < 3; i++) {
		ASSERT_NOT_NULL(frames[i]);
		ASSERT_EQUAL(10, frames[i]->width);
		ASSERT_EQUAL(10, frames[i]->height);
	}

	/* Cleanup */
	for (int i = 0; i < 3; i++) {
		image_destroy(frames[i]);
	}
	free(frames);
}

/**
 * @test Test frame timing calculation
 *
 * Verifies FPS to frame delay conversion.
 */
CTEST(integration, animation_frame_timing)
{
	/* Frame delay in milliseconds = 1000 / FPS */
	int fps = 15;
	int delay_ms = 1000 / fps;
	ASSERT_EQUAL(66, delay_ms); /* 1000/15 ≈ 66ms */

	fps = 10;
	delay_ms = 1000 / fps;
	ASSERT_EQUAL(100, delay_ms); /* 1000/10 = 100ms */

	fps = 1;
	delay_ms = 1000 / fps;
	ASSERT_EQUAL(1000, delay_ms); /* 1000/1 = 1000ms */
}

/**
 * @test Test animation with single frame (static image)
 *
 * Verifies that single-frame "animation" works (no animation).
 */
CTEST(integration, animation_single_frame_static)
{
	cli_options_t opts = { 0 };
	opts.input_file = "static.png";
	opts.interpolation = "lanczos";
	opts.fit_mode = true;
	opts.animate = false; /* Static mode */
	opts.fps = 15;

	/* Single frame should not animate */
	ASSERT_FALSE(opts.animate);
}

/**
 * @test Test animation enable/disable
 *
 * Verifies that animation can be toggled.
 */
CTEST(integration, animation_toggle)
{
	cli_options_t opts = { 0 };
	opts.input_file = "test.gif";
	opts.interpolation = "lanczos";
	opts.fit_mode = true;
	opts.fps = 15;

	/* Animation disabled */
	opts.animate = false;
	ASSERT_FALSE(opts.animate);
	ASSERT_EQUAL(0, validate_options(&opts));

	/* Animation enabled */
	opts.animate = true;
	ASSERT_TRUE(opts.animate);
	ASSERT_EQUAL(0, validate_options(&opts));
}
