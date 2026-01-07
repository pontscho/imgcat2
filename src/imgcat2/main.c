/**
 * @file main.c
 * @brief Main program entry point and pipeline orchestration
 *
 * Coordinates the entire image processing pipeline: CLI parsing,
 * file I/O, decoding, scaling, and terminal rendering.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decoders/decoder.h"
#include "core/cli.h"
#include "core/image.h"
#include "core/pipeline.h"

/**
 * @brief Main program entry point
 */
int main(int argc, char **argv)
{
	int exit_code = EXIT_FAILURE;

	/* Initialize CLI options with defaults */
	cli_options_t opts = { .input_file = NULL, .top_offset = 8, .interpolation = "lanczos", .fit_mode = true, .silent = true, .fps = 15, .animate = false };

	/* Parse command-line arguments */
	if (parse_arguments(argc, argv, &opts) != 0) {
		return EXIT_FAILURE;
	}

	/* Validate options */
	if (validate_options(&opts) < 0) {
		return EXIT_FAILURE;
	}

	/* Initialize decoder registry */
	decoder_registry_init(&opts);

	/* Pipeline variables */
	uint8_t *buffer = NULL;
	size_t buffer_size = 0;
	image_t **frames = NULL;
	int frame_count = 0;
	image_t **scaled_frames = NULL;

	/* STEP 1: Read input (file or stdin) */
	if (pipeline_read(&opts, &buffer, &buffer_size) < 0) {
		fprintf(stderr, "Error: Failed to read input\n");
		goto cleanup;
	}

	if (!opts.silent) {
		fprintf(stderr, "Read %zu bytes from %s\n", buffer_size, opts.input_file ? opts.input_file : "stdin");
	}

	/* STEP 2: Decode image with MIME detection */
	if (pipeline_decode(&opts, buffer, buffer_size, &frames, &frame_count) < 0) {
		fprintf(stderr, "Error: Failed to decode image\n");
		goto cleanup;
	}

	if (!opts.silent) {
		fprintf(stderr, "Decoded %d frame(s)\n", frame_count);
	}

	/* STEP 3: Scale images to terminal dimensions */
	if (pipeline_scale(frames, frame_count, &opts, &scaled_frames) < 0) {
		fprintf(stderr, "Error: Failed to scale images\n");
		goto cleanup;
	}

	if (!opts.silent) {
		fprintf(stderr, "Scaled to %ux%u pixels\n", scaled_frames[0]->width, scaled_frames[0]->height);
	}

	/* STEP 4: Render to terminal */
	if (pipeline_render(scaled_frames, frame_count, &opts) < 0) {
		fprintf(stderr, "Error: Failed to render output\n");
		goto cleanup;
	}

	/* Success */
	exit_code = EXIT_SUCCESS;

cleanup:
	/* Free buffer */
	if (buffer != NULL) {
		free(buffer);
	}

	/* Free decoded frames */
	if (frames != NULL) {
		for (int i = 0; i < frame_count; i++) {
			image_destroy(frames[i]);
		}
		free(frames);
	}

	/* Free scaled frames */
	if (scaled_frames != NULL) {
		for (int i = 0; i < frame_count; i++) {
			image_destroy(scaled_frames[i]);
		}
		free(scaled_frames);
	}

	return exit_code;
}
