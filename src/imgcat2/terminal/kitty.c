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

#include "../core/base64.h"
#include "../decoders/decoder.h"
#include "../decoders/magic.h"
#include "../terminal/terminal.h"
#include "kitty.h"

bool kitty_is_format_supported(const uint8_t *data, size_t size, cli_options_t *opts)
{
	(void)opts;

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
#ifdef HAVE_WEBP
		case MIME_WEBP:
			if (webp_is_animated(data, size) && opts->animate) {
				goto force_ansi;
			}
			break;
#endif

#ifdef HAVE_HEIF
		case MIME_AVIF:
		case MIME_HEIF:
			if (heif_is_animated(data, size) && opts->animate) {
				goto force_ansi;
			}
			break;
#endif

#ifdef PNG_APNG_SUPPORTED
		case MIME_PNG:
			if (png_is_animated(data, size) && opts->animate) {
				goto force_ansi;
			}
			break;
#endif

#ifdef HAVE_GIFLIB
		case MIME_GIF:
			if (gif_is_animated(data, size) && opts->animate) {
				goto force_ansi;
			}
			break;
#endif
		default: break;
	}

	return true;

force_ansi:
	/* Format not supported, force ANSI rendering */
	opts->force_ansi = true;
	return false;
}

int kitty_render(image_t **frames, int frame_count, const cli_options_t *opts)
{
	/* Get first frame */
	image_t *img = frames[0];

	/* Calculate RGBA data size */
	size_t raw_size = (size_t)img->width * img->height * 4;

	/* Base64 encode the RGBA pixel data */
	size_t encoded_size = 0;
	char *encoded = base64_encode(img->pixels, raw_size, &encoded_size);
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
	printf("a=T,f=32,t=d,s=%u,v=%u", img->width, img->height);

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
