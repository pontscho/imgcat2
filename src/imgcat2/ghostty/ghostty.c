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

#include "../decoders/magic.h"
#include "../iterm2/base64.h"
#include "../terminal/terminal.h"
#include "ghostty.h"

bool ghostty_is_format_supported(const uint8_t *data, size_t size)
{
	/* Validate inputs */
	if (data == NULL || size == 0) {
		return false;
	}

	/* Detect MIME type using magic bytes */
	mime_type_t mime = detect_mime_type(data, size);

	/* Check if format is supported by Kitty graphics protocol */
	switch (mime) {
		case MIME_PNG:
		case MIME_JPEG:
		case MIME_GIF: return true;
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

int ghostty_render(const uint8_t *data, size_t size, const char *filename, bool fit_mode, int target_width, int target_height)
{
	/* Validate inputs */
	if (data == NULL || size == 0) {
		fprintf(stderr, "Error: Ghostty render called with invalid data\n");
		return -1;
	}

	/* Suppress unused parameter warning (filename not used in Kitty protocol) */
	(void)filename;

	/* Base64 encode the image data */
	size_t encoded_size = 0;
	char *encoded = base64_encode(data, size, &encoded_size);
	if (encoded == NULL) {
		fprintf(stderr, "Error: Failed to base64 encode image data\n");
		return -1;
	}

	/* Check if running inside tmux */
	bool in_tmux = ghostty_is_tmux();

	/* Kitty graphics protocol format: \033_G<keys>;<base64>\033\\ */

	/* Start escape sequence */
	if (in_tmux) {
		/* Wrap with tmux DCS sequence: \033Ptmux;\033 ... \033\\ */
		printf("\033Ptmux;\033\033_G");
	} else {
		printf("\033_G");
	}

	/* Add control keys */
	/* a=T: transmit and display, f=100: PNG format, t=d: direct transmission */
	printf("a=T,f=100,t=d");

	/* Add dimensions based on fit_mode and target dimensions */
	if (fit_mode) {
		/* Fit mode: get terminal size and use it */
		int term_rows, term_cols;
		if (terminal_get_size(&term_rows, &term_cols) == 0) {
			/* Use terminal dimensions for fitting */
			/* Subtract some rows for prompt/spacing (e.g., 2 rows) */
			int available_rows = term_rows - 2;
			if (available_rows < 1) {
				available_rows = term_rows;
			}

			/* Set columns only - Kitty will auto-calculate rows with aspect ratio */
			printf(",c=%d", term_cols);
		}
		/* If terminal size detection fails, use original size (no c/r params) */

	} else if (target_width > 0 && target_height > 0) {
		/* Both dimensions specified: convert pixels to cells */
		int cols = pixels_to_cells(target_width, true);
		int rows = pixels_to_cells(target_height, false);
		printf(",c=%d,r=%d", cols, rows);

	} else if (target_width > 0) {
		/* Only width specified: convert to columns, auto-calculate rows */
		int cols = pixels_to_cells(target_width, true);
		printf(",c=%d", cols);

	} else if (target_height > 0) {
		/* Only height specified: convert to rows, auto-calculate columns */
		int rows = pixels_to_cells(target_height, false);
		printf(",r=%d", rows);
	}
	/* Default: no dimensions specified, use original image size */

	/* Add base64 image data after semicolon */
	printf(";%s", encoded);

	/* Terminate escape sequence */
	if (in_tmux) {
		printf("\033\\\033\\"); /* End graphics command + end tmux DCS */
	} else {
		printf("\033\\"); /* End graphics command */
	}

	/* Print newline after image */
	printf("\n");

	/* Flush output */
	fflush(stdout);

	/* Cleanup */
	free(encoded);

	return 0;
}
