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

#include "core/cli.h"
#include "core/image.h"
#include "core/pipeline.h"
#include "decoders/decoder.h"
#include "ghostty/ghostty.h"
#include "iterm2/iterm2.h"
#include "terminal/terminal.h"

/**
 * @brief Main program entry point
 */
int main(int argc, char **argv)
{
	int exit_code = EXIT_FAILURE;

	/* Initialize CLI options with defaults */
	cli_options_t opts = {
		.input_file = NULL,
		.top_offset = 8,
		.interpolation = "lanczos",
		.fit_mode = true,
		.silent = true,
		.fps = 15,
		.animate = false,
		.target_width = -1,
		.target_height = -1,
		.has_custom_dimensions = false,
		.force_ansi = false,

		.terminal = {
			.rows = 0,
			.cols = 0,
			.width = 0,
			.height = 0,
			.is_iterm2 = terminal_is_iterm2(),
			.is_ghostty = terminal_is_ghostty(),
		},
	};

	terminal_get_size(&opts.terminal.rows, &opts.terminal.cols);
	terminal_get_pixels(&opts.terminal.width, &opts.terminal.height);

	if (opts.terminal.is_ghostty || opts.terminal.is_iterm2) {
		opts.fit_mode = false;
	}

	/* Parse command-line arguments */
	if (parse_arguments(argc, argv, &opts) != 0) {
		return EXIT_FAILURE;

		/* Validate options */
	} else if (validate_options(&opts) < 0) {
		return EXIT_FAILURE;
	}

	if (!opts.silent) {
		fprintf(stderr, "Terminal size: %dx%d (%dx%d) pixels, is %s\n", opts.terminal.width, opts.terminal.height, opts.terminal.cols, opts.terminal.rows, opts.terminal.is_iterm2 ? "iTerm2" : (opts.terminal.is_ghostty ? "Ghostty" : "ANSI"));
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

	/* DECISION POINT: iTerm2 / Ghostty / ANSI rendering */

	if (!opts.force_ansi && opts.terminal.is_iterm2) {
		/* Check if format is supported by iTerm2 protocol */
		if (iterm2_is_format_supported(buffer, buffer_size)) {
			if (!opts.silent) {
				fprintf(stderr, "Using iTerm2 inline images protocol\n");
			}

			if (pipeline_render_iterm2(buffer, buffer_size, &opts) == 0) {
				/* Success - skip ANSI pipeline */
				exit_code = EXIT_SUCCESS;
				goto cleanup;
			}
		}

		opts.terminal.is_iterm2 = false;
		opts.force_ansi = true;
		if (!opts.silent) {
			fprintf(stderr, "Format not supported by iTerm2 or rendering failed, using ANSI rendering\n");
		}

	} else if (!opts.force_ansi && opts.terminal.is_ghostty) {
		/* Check if format is supported by Kitty graphics protocol */
		if (ghostty_is_format_supported(buffer, buffer_size, &opts)) {
			if (!opts.silent) {
				fprintf(stderr, "Using Ghostty (Kitty graphics protocol)\n");
			}

		} else {
			opts.terminal.is_ghostty = false;
			opts.force_ansi = true;

			if (!opts.silent) {
				fprintf(stderr, "Format not supported by Ghostty, using ANSI rendering\n");
			}
		}
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

	if (opts.terminal.is_ghostty) {
		extern int ghostty_render2(image_t * *frames, int frame_count, const char *filename, const cli_options_t *opts, int target_width, int target_height);
		/* Render using Ghostty protocol */
		printf(" ! opts.target_width=%d opts.target_height=%d \n", opts.target_width, opts.target_height);
		if (ghostty_render2(scaled_frames, frame_count, opts.input_file, &opts, opts.target_width, opts.target_height) == 0) {
			exit_code = EXIT_SUCCESS;
			goto cleanup;
		}
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
