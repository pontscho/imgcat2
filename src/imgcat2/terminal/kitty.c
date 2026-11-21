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

#include <img2/img2.h>

#include "../base64.h"
#include "terminal.h"
#include "kitty.h"

bool kitty_is_format_supported(const uint8_t *data, size_t size, cli_options_t *opts)
{
	(void)opts;

	/* Validate inputs */
	if (data == NULL || size == 0) {
		return false;
	}

	/*
	 * Kitty graphics protocol officially supports:
	 * - f=24: RGB raw pixel data
	 * - f=32: RGBA raw pixel data
	 * - f=100: PNG format
	 *
	 * Implementation:
	 * - Every format is decoded to RGBA and sent with f=32
	 * - Animated GIF/APNG/WebP/HEIF/AVIF with -a: NOT supported (fall back to ANSI rendering)
	 *
	 * The probe runs first (as before), then opts->animate.
	 */
	if (img2_is_animated(data, size) && opts->animate) {
		goto force_ansi;
	}

	return true;

force_ansi:
	/* Format not supported, force ANSI rendering */
	opts->force_ansi = true;
	return false;
}

int kitty_render(img2_image_t **frames, int frame_count, const cli_options_t *opts)
{
	(void)frame_count; /* only the first frame is sent */

	/* Get first frame */
	img2_image_t *img = frames[0];

	/* Calculate RGBA data size */
	size_t raw_size = (size_t)img->width * img->height * 4;

	/* Base64 encode the RGBA pixel data */
	size_t encoded_size = 0;
	char *encoded = base64_encode(img->pixels, raw_size, &encoded_size);
	if (encoded == NULL) {
		fprintf(stderr, "Error: Failed to base64 encode RGBA data\n");
		return -1; /* frames stay owned by the caller */
	}

	/* Use chunked transmission in tmux to avoid DCS size limits (~1MB) */
	if (opts->terminal.is_tmux && encoded_size > 4096) {
		/* Chunked transmission: split into 4KB chunks */
		const size_t chunk_size = 4096;
		size_t offset = 0;
		int chunk_num = 0;

		while (offset < encoded_size) {
			size_t remaining = encoded_size - offset;
			size_t current_chunk = (remaining > chunk_size) ? chunk_size : remaining;
			int more_chunks = (offset + current_chunk < encoded_size) ? 1 : 0;

			/* Start escape sequence */
			printf("\033Ptmux;\033\033_G");

			/* First chunk: include metadata */
			if (chunk_num == 0) {
				printf("a=T,f=32,t=d,s=%u,v=%u,m=%d", img->width, img->height, more_chunks);
			} else {
				/* Subsequent chunks: only m flag */
				printf("m=%d", more_chunks);
			}

			/* Add chunk data */
			printf(";%.*s", (int)current_chunk, encoded + offset);

			/* Terminate escape sequence */
			printf("\033\\\033\\");
			printf("\n");

			offset += current_chunk;
			chunk_num++;
		}

		fflush(stdout);

	} else {
		/* Direct transmission (non-tmux or small images) */
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
	}

	/* Cleanup */
	free(encoded);

	return 0;
}
