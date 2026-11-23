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

#include <img2/img2.h>

#include "cli.h"
#include "metadata.h"
#include "pipeline.h"
#include "safe_text.h"
#include "terminal/file.h"
#include "terminal/terminal.h"

/**
 * @brief Main program entry point
 */
int main(int argc, char **argv)
{
	img2_set_log_level(IMG2_LOG_WARN);

	int exit_code = EXIT_FAILURE;

	/* Initialize CLI options with defaults */
	cli_options_t opts = {
		.input_file = NULL,
		.interpolation = "lanczos",
		.filter = IMG2_FILTER_DEFAULT,
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
		.exif_detailed = false,
		.list_fonts = false,

		/* Encoder options */
		.convert_mode = false,
		.output_format = IMG2_FORMAT_NONE,
		.iterm2_format = IMG2_FORMAT_JPEG,
		.output_file = NULL,
		.jpeg_quality = 90,
		.png_compression = 6,
		.heif_quality = 80,
		.webp_quality = 80,
		.jxl_quality = 80,
		.avif_quality = 80,
		.frame_index = 0,
		.enc_lossless = false,
		.enc_effort = 0,
		.enc_chroma = IMG2_CHROMA_DEFAULT,
		.enc_alpha_quality = 0,
		.enc_progressive = false,
		.enc_threads = 0,
		.enc_keep_metadata = false,

		.terminal = {
			.rows = 0,
			.cols = 0,
			.width = 0,
			.height = 0,
			.is_iterm2 = terminal_is_iterm2(),
			.is_ghostty = terminal_is_ghostty(),
			.is_kitty = terminal_is_kitty(),
			.is_wezterm = terminal_is_wezterm(),
			.is_konsole = terminal_is_konsole(),
			.is_tmux = terminal_is_tmux(),

			/* Kitty graphics protocol: Ghostty, Kitty, and Konsole (WezTerm uses iTerm2 protocol) */
			.has_kitty = terminal_is_ghostty() || terminal_is_kitty() || terminal_is_konsole(),
		},
	};

	/* No terminal (output piped, no /dev/tty): the defaults, reported once below with -v only */
	terminal_get_pixels(&opts.terminal.width, &opts.terminal.height);
	const bool terminal_size_failed = terminal_get_size(&opts.terminal.rows, &opts.terminal.cols) < 0;
	if (terminal_size_failed) {
		opts.terminal.rows = DEFAULT_TERM_ROWS;
		opts.terminal.cols = DEFAULT_TERM_COLS;
	}

	/* Parse command-line arguments (1: --help / --version printed, a success) */
	const int parsed = parse_arguments(argc, argv, &opts);
	if (parsed > 0) {
		return EXIT_SUCCESS;

	} else if (parsed < 0) {
		return EXIT_FAILURE;

		/* Validate options */
	} else if (validate_options(&opts) < 0) {
		return EXIT_FAILURE;

		/* Handle --fonts option: list available fonts and exit */
	} else if (opts.list_fonts) {
		img2_init();
		print_font_list();
		img2_shutdown();
		return EXIT_SUCCESS;
	}

	if (!opts.silent) {
		img2_set_log_level(IMG2_LOG_INFO);
		if (terminal_size_failed) {
			fprintf(stderr, "Warning: Failed to get terminal size, using defaults\n");
		}
	}

	if (!opts.silent) {
		const char *terminal_type = opts.terminal.is_iterm2 ? "iTerm2" : opts.terminal.is_wezterm ? "WezTerm" : opts.terminal.is_ghostty ? "Ghostty" : opts.terminal.is_kitty ? "Kitty" : opts.terminal.is_konsole ? "Konsole" : "ANSI";

		fprintf(stderr, "Terminal size: %dx%d (%dx%d) pixels, is %s\n", opts.terminal.width, opts.terminal.height, opts.terminal.cols, opts.terminal.rows, terminal_type);
	}

	if (opts.terminal.width == 0 || opts.terminal.height == 0) {
		opts.force_ansi = true;
		if (!opts.silent) {
			fprintf(stderr, "Warning: Terminal pixel size unknown, forcing ANSI rendering\n");
		}
	}

	/* Initialize the library (logs the encoder and decoder tables at INFO) */
	img2_init();

	/* Pipeline variables */
	uint8_t *buffer = NULL;
	size_t buffer_size = 0;
	img2_frames_t frames = { NULL, 0, 0 };
	img2_image_t **scaled_frames = NULL;

	/* STEP 1: Read input (file or stdin) */
	if (pipeline_read(&opts, &buffer, &buffer_size) < 0) {
		fprintf(stderr, "Error: Failed to read input\n");
		goto cleanup;
	}

	if (!opts.silent) {
		char safe[CLI_SAFE_PATH_SIZE]; /* [P39] the path with control characters escaped */
		fprintf(stderr, "Read %zu bytes from %s\n", buffer_size, opts.input_file ? cli_safe_str(opts.input_file, safe, sizeof(safe)) : "stdin");
	}

	/* DECISION POINT: Kitty / ANSI rendering */

	/* iTerm2 and WezTerm both support the iTerm2 inline image protocol (OSC 1337) */
	if (!opts.force_ansi && (opts.terminal.is_iterm2 || opts.terminal.is_wezterm)) {
		/* Check if format is supported by iTerm2 protocol */
		if (iterm2_is_format_supported(buffer, buffer_size, &opts)) {
			if (!opts.silent) {
				fprintf(stderr, "iTerm2 terminal detected, will use decode → scale → encode pipeline\n");
			}
		}

	} else if (!opts.force_ansi && opts.terminal.has_kitty) {
		/* Check if format is supported by Kitty graphics protocol */
		if (kitty_is_format_supported(buffer, buffer_size, &opts)) {
			if (!opts.silent) {
				fprintf(stderr, "Using Kitty graphics protocol\n");
			}

		} else {
			opts.terminal.has_kitty = false;
			opts.force_ansi = true;

			if (!opts.silent) {
				fprintf(stderr, "Format not supported by Kitty graphics protocol, using ANSI rendering\n");
			}
		}
	}

	/* STEP 2: Decode image with MIME detection */
	if (pipeline_decode(&opts, buffer, buffer_size, &frames.frames, &frames.count) < 0) {
		fprintf(stderr, "Error: Failed to decode image\n");
		goto cleanup;
	}

	if (!opts.silent) {
		fprintf(stderr, "Decoded %d frame(s)\n", frames.count);
	}

	/* STEP 2.5: Output metadata and exit if --info specified */
	if (opts.info_mode) {
		/* Re-detect MIME type for output (forced IMG2_MIME_YUV for --yuv) */
		img2_mime_t mime = pipeline_input_mime(&opts, buffer, buffer_size);

		if (opts.json_output) {
#if IMG2_HAS_EXIF
			/* Build complete JSON with EXIF/XMP data */
			const char *type = img2_mime_name(mime);
			const char *mime_str = img2_mime_string(mime);
			printf("{\"type\":\"%s\",\"mime\":\"%s\",\"width\":%u,\"height\":%u,\"frames\":%d", type, mime_str, frames.frames[0]->width, frames.frames[0]->height, frames.count);
			output_exif_json(frames.frames[0]->exif, false);
			output_xmp_json(frames.frames[0]->xmp, false);
			printf("}\n");

#else
			output_metadata_json(mime, frames.frames[0]->width, frames.frames[0]->height, frames.count);
#endif

		} else {
			output_metadata_text(mime, frames.frames[0]->width, frames.frames[0]->height, frames.count);
#if IMG2_HAS_EXIF
			output_exif_text(frames.frames[0]->exif);
			output_xmp_text(frames.frames[0]->xmp);
#endif
		}

		/* Success - skip scaling and rendering */
		exit_code = EXIT_SUCCESS;
		goto cleanup;
	}

	/* STEP 3: Convert mode or terminal rendering */
	if (opts.convert_mode) {
		/* Convert mode: only scale if user explicitly requested dimensions */
		if (opts.has_custom_dimensions) {
			if (pipeline_scale(frames.frames,frames.count, &opts, &scaled_frames) < 0) {
				fprintf(stderr, "Error: Failed to scale images\n");
				goto cleanup;
			}

			if (!opts.silent) {
				fprintf(stderr, "Scaled to %ux%u pixels\n", scaled_frames[0]->width, scaled_frames[0]->height);
			}
		}

		/* Use scaled frames if available, otherwise original frames */
		img2_image_t **render_frames = scaled_frames ? scaled_frames : frames.frames;

		/* Validate frame index */
		if (opts.frame_index >= frames.count) {
			fprintf(stderr, "Error: Frame index %d out of range (0-%d)\n", opts.frame_index, frames.count - 1);
		} else if (file_render(render_frames, frames.count, &opts) < 0) {
			fprintf(stderr, "Error: Failed to convert and save image\n");
		} else {
			exit_code = EXIT_SUCCESS;
		}

		goto cleanup;
	}

	/* STEP 3.1: Scale images to terminal dimensions (non-convert mode) */
	if (pipeline_scale(frames.frames,frames.count, &opts, &scaled_frames) < 0) {
		fprintf(stderr, "Error: Failed to scale images\n");
		goto cleanup;
	}

	if (!opts.silent) {
		fprintf(stderr, "Scaled to %ux%u pixels\n", scaled_frames[0]->width, scaled_frames[0]->height);
	}

	/* STEP 4.2: Render using Kitty or iTerm2 graphics protocol */
	/* Render using iTerm2 protocol for iTerm2 and WezTerm */
	if ((opts.terminal.is_iterm2 || opts.terminal.is_wezterm) && !opts.force_ansi) {
		if (iterm2_render(scaled_frames, frames.count, &opts, buffer_size) == 0) {
			exit_code = EXIT_SUCCESS;
			goto cleanup;
		}

	} else if (opts.terminal.has_kitty && !opts.force_ansi) {
		if (kitty_render(scaled_frames, frames.count, &opts) == 0) {
			exit_code = EXIT_SUCCESS;
			goto cleanup;
		}
	}

	/* STEP 4.3: Render to terminal */
	if (pipeline_render(scaled_frames, frames.count, &opts) < 0) {
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

	/* Free scaled frames (the CLI owns the array, the library the images) */
	if (scaled_frames != NULL) {
		for (int i = 0; i < frames.count; i++) {
			img2_image_destroy(scaled_frames[i]);
		}
		free(scaled_frames);
	}

	/* Free decoded frames */
	img2_frames_free(&frames);

	img2_shutdown();
	return exit_code;
}
