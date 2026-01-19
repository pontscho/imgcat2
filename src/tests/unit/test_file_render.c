/**
 * @file test_file_render.c
 * @brief Unit tests for file_render function
 *
 * Tests file rendering functionality using ctest.h framework.
 * Tests NULL handling, output formats, frame selection, and file/stdout output.
 */

#include <sys/stat.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../imgcat2/core/cli.h"
#include "../../imgcat2/core/image.h"
#include "../../imgcat2/encoders/encoder.h"
#include "../../imgcat2/terminal/file.h"
#include "../ctest.h"

/* Helper function to create test image */
static image_t *create_test_image(int width, int height)
{
	image_t *img = image_create(width, height);
	if (img == NULL) {
		return NULL;
	}

	/* Fill with gradient pattern */
	for (uint32_t y = 0; y < (uint32_t)height; y++) {
		for (uint32_t x = 0; x < (uint32_t)width; x++) {
			uint8_t *pixel = image_get_pixel(img, x, y);
			pixel[0] = (x * 255 / width);
			pixel[1] = (y * 255 / height);
			pixel[2] = 128;
			pixel[3] = 255;
		}
	}

	return img;
}

/* Helper function to check if file exists and has content */
static bool file_exists_with_content(const char *path, size_t *size_out)
{
	struct stat st;
	if (stat(path, &st) != 0) {
		return false;
	}
	if (size_out != NULL) {
		*size_out = st.st_size;
	}
	return st.st_size > 0;
}

/**
 * @test Test file_render() with NULL parameters
 *
 * Verifies that file_render() rejects NULL inputs gracefully.
 */
CTEST(file_render, null_inputs)
{
	/* Initialize encoder registry */
	encoder_registry_init(NULL);

	image_t *img = create_test_image(10, 10);
	ASSERT_NOT_NULL(img);

	image_t *frames[] = { img };
	cli_options_t opts = { 0 };
	opts.output_format = FORMAT_JPEG;
	opts.jpeg_quality = 80;
	opts.output_file = NULL;
	opts.silent = true;

	/* NULL frames pointer */
	int result = file_render(NULL, 1, &opts);
	ASSERT_EQUAL(-1, result);

	/* Zero frame count */
	result = file_render(frames, 0, &opts);
	ASSERT_EQUAL(-1, result);

	/* Negative frame count */
	result = file_render(frames, -1, &opts);
	ASSERT_EQUAL(-1, result);

	/* NULL opts pointer */
	result = file_render(frames, 1, NULL);
	ASSERT_EQUAL(-1, result);

	image_destroy(img);
}

/**
 * @test Test file_render() with FORMAT_NONE
 *
 * Verifies that file_render() rejects FORMAT_NONE.
 */
CTEST(file_render, invalid_format)
{
	encoder_registry_init(NULL);

	image_t *img = create_test_image(10, 10);
	ASSERT_NOT_NULL(img);

	image_t *frames[] = { img };
	cli_options_t opts = { 0 };
	opts.output_format = FORMAT_NONE; /* Invalid */
	opts.output_file = NULL;
	opts.silent = true;

	int result = file_render(frames, 1, &opts);
	ASSERT_EQUAL(-1, result);

	image_destroy(img);
}

/**
 * @test Test file_render() with file output
 *
 * Verifies that file_render() can write to a file.
 */
CTEST(file_render, file_output)
{
	encoder_registry_init(NULL);

	image_t *img = create_test_image(20, 20);
	ASSERT_NOT_NULL(img);

	image_t *frames[] = { img };
	cli_options_t opts = { 0 };
	opts.output_format = FORMAT_JPEG;
	opts.jpeg_quality = 80;
	opts.output_file = "/tmp/imgcat2_test_render.jpg";
	opts.silent = true;

	/* Remove file if exists */
	unlink(opts.output_file);

	int result = file_render(frames, 1, &opts);
	ASSERT_EQUAL(0, result);

	/* Verify file was created */
	size_t file_size = 0;
	ASSERT_TRUE(file_exists_with_content(opts.output_file, &file_size));
	ASSERT_TRUE(file_size > 0);

	/* Verify file is valid JPEG (starts with 0xFF 0xD8) */
	FILE *fp = fopen(opts.output_file, "rb");
	ASSERT_NOT_NULL(fp);
	if (fp == NULL) {
		image_destroy(img);
		return;
	}

	uint8_t magic[2];
	size_t read_bytes = fread(magic, 1, 2, fp);
	fclose(fp);

	ASSERT_EQUAL(2, read_bytes);
	ASSERT_EQUAL(0xFF, magic[0]);
	ASSERT_EQUAL(0xD8, magic[1]);

	/* Cleanup */
	unlink(opts.output_file);
	image_destroy(img);
}

/**
 * @test Test file_render() with PNG file output
 *
 * Verifies that file_render() can write PNG to a file.
 */
CTEST(file_render, file_output_png)
{
	encoder_registry_init(NULL);

	image_t *img = create_test_image(20, 20);
	ASSERT_NOT_NULL(img);

	image_t *frames[] = { img };
	cli_options_t opts = { 0 };
	opts.output_format = FORMAT_PNG;
	opts.png_compression = 6;
	opts.output_file = "/tmp/imgcat2_test_render.png";
	opts.silent = true;

	/* Remove file if exists */
	unlink(opts.output_file);

	int result = file_render(frames, 1, &opts);
	ASSERT_EQUAL(0, result);

	/* Verify file was created */
	size_t file_size = 0;
	ASSERT_TRUE(file_exists_with_content(opts.output_file, &file_size));
	ASSERT_TRUE(file_size > 0);

	/* Verify file is valid PNG (starts with PNG signature) */
	FILE *fp = fopen(opts.output_file, "rb");
	ASSERT_NOT_NULL(fp);
	if (fp == NULL) {
		image_destroy(img);
		return;
	}

	uint8_t magic[8];
	size_t read_bytes = fread(magic, 1, 8, fp);
	fclose(fp);

	ASSERT_EQUAL(8, read_bytes);
	ASSERT_EQUAL(0x89, magic[0]);
	ASSERT_EQUAL(0x50, magic[1]);
	ASSERT_EQUAL(0x4E, magic[2]);
	ASSERT_EQUAL(0x47, magic[3]);

	/* Cleanup */
	unlink(opts.output_file);
	image_destroy(img);
}

/**
 * @test Test file_render() with animated image warning
 *
 * Verifies that file_render() handles animated images correctly.
 */
CTEST(file_render, animated_warning)
{
	encoder_registry_init(NULL);

	/* Create 3 test frames */
	image_t *frame1 = create_test_image(10, 10);
	image_t *frame2 = create_test_image(10, 10);
	image_t *frame3 = create_test_image(10, 10);
	ASSERT_NOT_NULL(frame1);
	ASSERT_NOT_NULL(frame2);
	ASSERT_NOT_NULL(frame3);

	image_t *frames[] = { frame1, frame2, frame3 };
	cli_options_t opts = { 0 };
	opts.output_format = FORMAT_JPEG;
	opts.jpeg_quality = 80;
	opts.output_file = "/tmp/imgcat2_test_animated.jpg";
	opts.frame_index = 0; /* Convert first frame */
	opts.silent = true;

	/* Remove file if exists */
	unlink(opts.output_file);

	int result = file_render(frames, 3, &opts);
	ASSERT_EQUAL(0, result);

	/* Verify file was created */
	ASSERT_TRUE(file_exists_with_content(opts.output_file, NULL));

	/* Cleanup */
	unlink(opts.output_file);
	image_destroy(frame1);
	image_destroy(frame2);
	image_destroy(frame3);
}

/**
 * @test Test file_render() with specific frame selection
 *
 * Verifies that file_render() can select a specific frame from animated images.
 */
CTEST(file_render, frame_index)
{
	encoder_registry_init(NULL);

	/* Create 3 test frames with different colors */
	image_t *frame1 = create_test_image(10, 10);
	image_t *frame2 = create_test_image(10, 10);
	image_t *frame3 = create_test_image(10, 10);
	ASSERT_NOT_NULL(frame1);
	ASSERT_NOT_NULL(frame2);
	ASSERT_NOT_NULL(frame3);

	/* Mark frames with different colors */
	uint8_t *p1 = image_get_pixel(frame1, 0, 0);
	p1[0] = 255;
	p1[1] = 0;
	p1[2] = 0; /* Red */

	uint8_t *p2 = image_get_pixel(frame2, 0, 0);
	p2[0] = 0;
	p2[1] = 255;
	p2[2] = 0; /* Green */

	uint8_t *p3 = image_get_pixel(frame3, 0, 0);
	p3[0] = 0;
	p3[1] = 0;
	p3[2] = 255; /* Blue */

	image_t *frames[] = { frame1, frame2, frame3 };
	cli_options_t opts = { 0 };
	opts.output_format = FORMAT_PNG;
	opts.png_compression = 6;
	opts.output_file = "/tmp/imgcat2_test_frame.png";
	opts.frame_index = 1; /* Convert second frame (green) */
	opts.silent = true;

	/* Remove file if exists */
	unlink(opts.output_file);

	int result = file_render(frames, 3, &opts);
	ASSERT_EQUAL(0, result);

	/* Verify file was created */
	ASSERT_TRUE(file_exists_with_content(opts.output_file, NULL));

	/* Cleanup */
	unlink(opts.output_file);
	image_destroy(frame1);
	image_destroy(frame2);
	image_destroy(frame3);
}

/**
 * @test Test file_render() with invalid frame index
 *
 * Verifies that file_render() rejects invalid frame indices.
 */
CTEST(file_render, invalid_frame_index)
{
	encoder_registry_init(NULL);

	image_t *frame1 = create_test_image(10, 10);
	image_t *frame2 = create_test_image(10, 10);
	ASSERT_NOT_NULL(frame1);
	ASSERT_NOT_NULL(frame2);

	image_t *frames[] = { frame1, frame2 };
	cli_options_t opts = { 0 };
	opts.output_format = FORMAT_JPEG;
	opts.jpeg_quality = 80;
	opts.output_file = "/tmp/imgcat2_test_invalid.jpg";
	opts.frame_index = 5; /* Out of range (only 2 frames) */
	opts.silent = true;

	/* Remove file if exists */
	unlink(opts.output_file);

	int result = file_render(frames, 2, &opts);
	ASSERT_EQUAL(-1, result);

	/* Verify file was NOT created */
	ASSERT_FALSE(file_exists_with_content(opts.output_file, NULL));

	/* Cleanup */
	image_destroy(frame1);
	image_destroy(frame2);
}
