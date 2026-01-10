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
#include "core/metadata.h"
#include "core/pipeline.h"
#include "decoders/decoder.h"
#include "decoders/magic.h"
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
		.interpolation = "lanczos",
		.fit_mode = false,
		.silent = true,
		.fps = 15,
		.animate = false,
		.target_width = -1,
		.target_height = -1,
		.has_custom_dimensions = false,
		.force_ansi = false,
		.info_mode = false,
		.json_output = false,

		.terminal = {
			.rows = 0,
			.cols = 0,
			.width = 0,
			.height = 0,
			.is_iterm2 = terminal_is_iterm2(),
			.is_ghostty = terminal_is_ghostty(),
			.is_kitty = terminal_is_kitty(),
			.is_tmux = terminal_is_tmux(),

			.has_kitty = terminal_is_ghostty() || terminal_is_kitty(),
		},
	};

	terminal_get_pixels(&opts.terminal.width, &opts.terminal.height);
	if (terminal_get_size(&opts.terminal.rows, &opts.terminal.cols) < 0) {
		fprintf(stderr, "Warning: Failed to get terminal size, using defaults\n");
		opts.terminal.rows = DEFAULT_TERM_ROWS;
		opts.terminal.cols = DEFAULT_TERM_COLS;
	}

	// if (opts.terminal.is_ghostty || opts.terminal.is_iterm2) {
	// 	opts.fit_mode = false;
	// }

	/* Parse command-line arguments */
	if (parse_arguments(argc, argv, &opts) != 0) {
		return EXIT_FAILURE;

		/* Validate options */
	} else if (validate_options(&opts) < 0) {
		return EXIT_FAILURE;
	}

	if (!opts.silent) {
		const char *terminal_type = opts.terminal.is_iterm2 ? "iTerm2" : (opts.terminal.is_ghostty ? "Ghostty" : (opts.terminal.is_kitty ? "Kitty" : "ANSI"));
		fprintf(stderr, "Terminal size: %dx%d (%dx%d) pixels, is %s\n", opts.terminal.width, opts.terminal.height, opts.terminal.cols, opts.terminal.rows, terminal_type);
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

	} else if (!opts.force_ansi && opts.terminal.has_kitty) {
		/* Check if format is supported by Kitty graphics protocol */
		if (kitty_is_format_supported(buffer, buffer_size, &opts)) {
			if (!opts.silent) {
				const char *terminal_name = opts.terminal.is_ghostty ? "Ghostty" : "Kitty";
				fprintf(stderr, "Using %s (Kitty graphics protocol)\n", terminal_name);
			}

		} else {
			opts.terminal.has_kitty = false;
			opts.force_ansi = true;

			if (!opts.silent) {
				const char *terminal_name = opts.terminal.is_ghostty ? "Ghostty" : "Kitty";
				fprintf(stderr, "Format not supported by %s, using ANSI rendering\n", terminal_name);
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

	/* STEP 2.5: Output metadata and exit if --info specified */
	if (opts.info_mode) {
		/* Re-detect MIME type for output */
		mime_type_t mime = detect_mime_type(buffer, buffer_size);

		if (opts.json_output) {
			output_metadata_json(mime, frames[0]->width, frames[0]->height, frame_count);
		} else {
			output_metadata_text(mime, frames[0]->width, frames[0]->height, frame_count);
		}

		/* Success - skip scaling and rendering */
		exit_code = EXIT_SUCCESS;
		goto cleanup;
	}

	/* STEP 3: Scale images to terminal dimensions */
	if (pipeline_scale(frames, frame_count, &opts, &scaled_frames) < 0) {
		fprintf(stderr, "Error: Failed to scale images\n");
		goto cleanup;
	}

	if (!opts.silent) {
		fprintf(stderr, "Scaled to %ux%u pixels\n", scaled_frames[0]->width, scaled_frames[0]->height);
	}

	/* STEP 4.1: Render using Kitty graphics protocol */
	if (opts.terminal.has_kitty && !opts.force_ansi) {
		if (kitty_render(scaled_frames, frame_count, &opts) == 0) {
			exit_code = EXIT_SUCCESS;
			goto cleanup;
		}
	}

	/* STEP 4.2: Render to terminal */
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
