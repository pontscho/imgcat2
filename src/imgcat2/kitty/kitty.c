/**
 * @file kitty.c
 * @brief Terminal Kitty Graphics Protocol implementation
 *
 * Implements the Kitty graphics protocol for high-quality image
 * rendering in Ghostty and other Kitty-compatible terminals.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../decoders/decoder.h"
#include "../decoders/magic.h"
#include "../core/base64.h"
#include "../terminal/terminal.h"
#include "kitty.h"

bool kitty_is_format_supported(const uint8_t *data, size_t size, cli_options_t *opts)
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
 * /
static int pixels_to_cells(int pixels, bool is_width)
{
	if (pixels <= 0) {
		return 0;
	}

	// Typical cell dimensions (can vary by terminal and font)
	const int CELL_WIDTH_PX = 9;
	const int CELL_HEIGHT_PX = 18;

	int cells;
	if (is_width) {
		cells = (pixels + CELL_WIDTH_PX - 1) / CELL_WIDTH_PX;

	} else {
		cells = (pixels + CELL_HEIGHT_PX - 1) / CELL_HEIGHT_PX;
	}

	return cells > 0 ? cells : 1;
} */

int kitty_render(image_t **frames, int frame_count, const cli_options_t *opts)
{
	/* Suppress unused parameter warning (filename not used in Kitty protocol) */
	(void)opts;

	/* Get first frame */
	image_t *img = frames[0];
	uint32_t img_width = img->width;
	uint32_t img_height = img->height;

	printf(" kitty_render2: img_width=%u img_height=%u \n", img_width,  img_height);
	printf(" opts->fit_mode=%d \n", opts->fit_mode);

	/* Calculate RGBA data size */
	size_t rgba_size = (size_t)img_width * img_height * 4;

	/* Base64 encode the RGBA pixel data */
	size_t encoded_size = 0;

	/* Benchmark base64_encode() */
	struct timespec start, end;
	clock_gettime(CLOCK_MONOTONIC, &start);

	char *encoded = base64_encode(img->pixels, rgba_size, &encoded_size);

	clock_gettime(CLOCK_MONOTONIC, &end);
	double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
	printf("base64_encode() took %.3f ms for %zu bytes\n", elapsed_ms, rgba_size);

	if (encoded == NULL) {
		fprintf(stderr, "Error: Failed to base64 encode RGBA data\n");
		decoder_free_frames(frames, frame_count);
		return -1;
	}

	/* Start escape sequence */
	if (opts->terminal.is_tmux) {
		printf("\033Ptmux;\033\033_G");
	} else {
		printf("\033_G");
	}

	/* a=T: transmit and display, f=32: RGBA format, t=d: direct transmission */
	/* s=width, v=height: pixel dimensions (required for f=32) */
	printf("a=T,f=32,t=d,s=%u,v=%u", img_width, img_height);

	/* Add base64 RGBA data */
	printf(";%s", encoded);

	/* Terminate escape sequence */
	if (opts->terminal.is_tmux) {
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
