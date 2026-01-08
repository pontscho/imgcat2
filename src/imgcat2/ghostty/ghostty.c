/**
 * @file ghostty.c
 * @brief Ghostty Terminal Kitty Graphics Protocol implementation
 *
 * Implements the Kitty graphics protocol for high-quality image
 * rendering in Ghostty and other Kitty-compatible terminals.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../decoders/decoder.h"
#include "../decoders/magic.h"
#include "../iterm2/base64.h"
#include "../terminal/terminal.h"
#include "ghostty.h"

bool ghostty_is_format_supported(const uint8_t *data, size_t size, cli_options_t *opts)
{
	/* Validate inputs */
	if (data == NULL || size == 0) {
		return false;
	}

	/* Detect MIME type using magic bytes */
	mime_type_t mime = detect_mime_type(data, size);

	/*
	 * Kitty graphics protocol officially supports:
	 * - f=24: RGB raw pixel data
	 * - f=32: RGBA raw pixel data
	 * - f=100: PNG format
	 *
	 * Implementation:
	 * - PNG: Send directly with f=100 (no decoding needed)
	 * - JPEG: Decode to RGBA and send with f=32
	 * - Static GIF: Decode to RGBA and send with f=32
	 * - Animated GIF: NOT supported (fall back to ANSI rendering)
	 */
	switch (mime) {
		case MIME_PNG: return true;
		case MIME_JPEG: return true;

#ifdef HAVE_GIFLIB
		case MIME_GIF:
			if (gif_is_animated(data, size) && opts->animate) {
				opts->force_ansi = true;
				return false;
			}
			return true;
#endif

		default: return false;
	}
}

bool ghostty_is_tmux(void)
{
	/* Check if TMUX environment variable is set */
	return getenv("TMUX") != NULL;
}

/**
 * @brief Convert pixel dimensions to approximate terminal cell dimensions
 *
 * Estimates terminal cell size assuming typical dimensions:
 * - Cell width: 8-10 pixels (using 9 as average)
 * - Cell height: 16-20 pixels (using 18 as average)
 *
 * @param pixels Pixel dimension
 * @param is_width true for width conversion, false for height
 * @return Approximate number of terminal cells
 */
static int pixels_to_cells(int pixels, bool is_width)
{
	if (pixels <= 0) {
		return 0;
	}

	/* Typical cell dimensions (can vary by terminal and font) */
	const int CELL_WIDTH_PX = 9;
	const int CELL_HEIGHT_PX = 18;

	int cells;
	if (is_width) {
		cells = (pixels + CELL_WIDTH_PX - 1) / CELL_WIDTH_PX;

	} else {
		cells = (pixels + CELL_HEIGHT_PX - 1) / CELL_HEIGHT_PX;
	}

	return cells > 0 ? cells : 1;
}

int ghostty_render(const uint8_t *data, size_t size, const char *filename, const cli_options_t *opts, int target_width, int target_height)
{
	/* Validate inputs */
	if (data == NULL || size == 0) {
		fprintf(stderr, "Error: Ghostty render called with invalid data\n");
		return -1;
	}

	/* Suppress unused parameter warning (filename not used in Kitty protocol) */
	(void)filename;

	/* Detect MIME type to determine rendering path */
	mime_type_t mime = detect_mime_type(data, size);

	/* Check if running inside tmux */
	bool in_tmux = ghostty_is_tmux();

	/* PNG: Send directly with f=100 (no decoding needed) */
	if (0 && mime == MIME_PNG) {
		/* Base64 encode the PNG data */
		size_t encoded_size = 0;
		char *encoded = base64_encode(data, size, &encoded_size);
		if (encoded == NULL) {
			fprintf(stderr, "Error: Failed to base64 encode PNG data\n");
			return -1;
		}

		/* Start escape sequence */
		if (in_tmux) {
			printf("\033Ptmux;\033\033_G");
		} else {
			printf("\033_G");
		}

		/* a=T: transmit and display, f=100: PNG format, t=d: direct transmission */
		printf("a=T,f=100,t=d");

		/* Add dimensions based on fit_mode and target dimensions */
		if (opts->fit_mode) {
			int term_rows, term_cols;
			if (terminal_get_size(&term_rows, &term_cols) == 0) {
				int available_rows = term_rows - 2;
				if (available_rows < 1) {
					available_rows = term_rows;
				}
				printf(",c=%d", term_cols);
			}

		} else if (target_width > 0 && target_height > 0) {
			int cols = pixels_to_cells(target_width, true);
			int rows = pixels_to_cells(target_height, false);
			printf(",c=%d,r=%d", cols, rows);

		} else if (target_width > 0) {
			int cols = pixels_to_cells(target_width, true);
			printf(",c=%d", cols);

		} else if (target_height > 0) {
			int rows = pixels_to_cells(target_height, false);
			printf(",r=%d", rows);
		}

		/* Add base64 PNG data */
		printf(";%s", encoded);

		/* Terminate escape sequence */
		if (in_tmux) {
			printf("\033\\\033\\");
		} else {
			printf("\033\\");
		}

		printf("\n");
		fflush(stdout);
		free(encoded);
		return 0;
	}

	/* JPEG and static GIF: Decode to RGBA and send with f=32 */
	int frame_count = 0;
	image_t **frames = decoder_decode(NULL, data, size, mime, &frame_count);
	if (frames == NULL || frame_count < 1 || frames[0] == NULL) {
		fprintf(stderr, "Error: Failed to decode image for Ghostty rendering\n");
		if (frames != NULL) {
			decoder_free_frames(frames, frame_count);
		}
		return -1;
	}


	/* Get first frame */
	image_t *img = frames[0];
	uint32_t img_width = img->width;
	uint32_t img_height = img->height;

	/* Calculate RGBA data size */
	size_t rgba_size = (size_t)img_width * img_height * 4;

	/* Base64 encode the RGBA pixel data */
	size_t encoded_size = 0;
	char *encoded = base64_encode(img->pixels, rgba_size, &encoded_size);
	if (encoded == NULL) {
		fprintf(stderr, "Error: Failed to base64 encode RGBA data\n");
		decoder_free_frames(frames, frame_count);
		return -1;
	}

	/* Start escape sequence */
	if (in_tmux) {
		printf("\033Ptmux;\033\033_G");
	} else {
		printf("\033_G");
	}

	/* a=T: transmit and display, f=32: RGBA format, t=d: direct transmission */
	/* s=width, v=height: pixel dimensions (required for f=32) */
	printf("a=T,f=32,t=d,s=%u,v=%u", img_width, img_height);

	/* Add terminal cell dimensions based on fit_mode and target dimensions */
	if (opts->fit_mode) {
		int term_rows, term_cols;
		if (terminal_get_size(&term_rows, &term_cols) == 0) {
			int available_rows = term_rows - 2;
			if (available_rows < 1) {
				available_rows = term_rows;
			}
			printf(",c=%d", term_cols);
		}

	} else if (target_width > 0 && target_height > 0) {
		int cols = pixels_to_cells(target_width, true);
		int rows = pixels_to_cells(target_height, false);
		printf(",c=%d,r=%d", cols, rows);

	} else if (target_width > 0) {
		int cols = pixels_to_cells(target_width, true);
		printf(",c=%d", cols);

	} else if (target_height > 0) {
		int rows = pixels_to_cells(target_height, false);
		printf(",r=%d", rows);
	}

	/* Add base64 RGBA data */
	printf(";%s", encoded);

	/* Terminate escape sequence */
	if (in_tmux) {
		printf("\033\\\033\\");
	} else {
		printf("\033\\");
	}

	printf("\n");
	fflush(stdout);

	/* Cleanup */
	free(encoded);
	decoder_free_frames(frames, frame_count);

	return 0;
}

int ghostty_render2(image_t **frames, int frame_count, const char *filename, const cli_options_t *opts, int target_width, int target_height)
{

	/* Suppress unused parameter warning (filename not used in Kitty protocol) */
	(void)filename;
	(void)opts;
	(void)target_width;
	(void)target_height;

	/* Check if running inside tmux */
	bool in_tmux = ghostty_is_tmux();

	int term_rows, term_cols;
	terminal_get_size(&term_rows, &term_cols);

	/* Get first frame */
	image_t *img = frames[0];
	uint32_t img_width = img->width;
	uint32_t img_height = img->height;

	printf(" ghostty_render2: img_width=%u img_height=%u \n", img_width,  img_height);
	printf(" opts->fit_mode=%d \n", opts->fit_mode);

	/* Calculate RGBA data size */
	size_t rgba_size = (size_t)img_width * img_height * 4;

	/* Base64 encode the RGBA pixel data */
	size_t encoded_size = 0;
	char *encoded = base64_encode(img->pixels, rgba_size, &encoded_size);
	if (encoded == NULL) {
		fprintf(stderr, "Error: Failed to base64 encode RGBA data\n");
		decoder_free_frames(frames, frame_count);
		return -1;
	}

	/* Start escape sequence */
	if (in_tmux) {
		printf("\033Ptmux;\033\033_G");
	} else {
		printf("\033_G");
	}

	/* a=T: transmit and display, f=32: RGBA format, t=d: direct transmission */
	/* s=width, v=height: pixel dimensions (required for f=32) */
	printf("a=T,f=32,t=d,s=%u,v=%u", img_width, img_height);
	// printf(",c=%d", 10);

	/* Add terminal cell dimensions based on fit_mode and target dimensions */
	if (opts->fit_mode) {
		if (terminal_get_size(&term_rows, &term_cols) == 0) {
			printf(",c=%d", term_cols);
		}

	// } else if (target_width > 0 && target_height > 0) {
	// 	int cols = pixels_to_cells(target_width, true);
	// 	int rows = pixels_to_cells(target_height, false);
	// 	printf(",c=%d,r=%d", cols, rows);

	// } else if (target_width > 0) {
	// 	int cols = pixels_to_cells(target_width, true);
	// 	printf(",c=%d", cols);

	// } else if (target_height > 0) {
	// 	int rows = pixels_to_cells(target_height, false);
	// 	printf(",r=%d", rows);
	}

	/* Add base64 RGBA data */
	printf(";%s", encoded);

	/* Terminate escape sequence */
	if (in_tmux) {
		printf("\033\\\033\\");
	} else {
		printf("\033\\");
	}

	printf("\n");
	fflush(stdout);

	/* Cleanup */
	free(encoded);

	return 0;
}
