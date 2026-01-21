/**
 * @file iterm2.c
 * @brief iTerm2 Inline Images Protocol implementation
 *
 * Implements the iTerm2 inline images protocol (OSC 1337) for
 * high-quality image rendering in iTerm2 terminal emulator.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/base64.h"
#include "../core/cli.h"
#include "../decoders/decoder.h"
#include "../decoders/magic.h"
#include "../encoders/encoder.h"
#include "iterm2.h"

bool iterm2_is_format_supported(const uint8_t *data, size_t size, cli_options_t *opts)
{
	/* Validate inputs */
	if (data == NULL || size == 0) {
		return false;
	}

	/* Detect MIME type using magic bytes */
	mime_type_t mime = detect_mime_type(data, size);

	/*
	 * iTerm2 will use decode → scale → encode pipeline for all formats.
	 * However, animated formats with -a flag should fall back to ANSI
	 * rendering (following Kitty pattern).
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

int iterm2_render(image_t **frames, int frame_count, const cli_options_t *opts, size_t original_size)
{
	/* Validate inputs */
	if (frames == NULL || frame_count <= 0 || opts == NULL) {
		fprintf(stderr, "Error: iTerm2 render called with invalid parameters\n");
		return -1;
	}

	/* Get first frame */
	image_t *img = frames[0];

	/* Task-008: Dimension limit check (8192x8192 max) */
#define ITERM2_MAX_DIMENSION 8192
	if (img->width > ITERM2_MAX_DIMENSION || img->height > ITERM2_MAX_DIMENSION) {
		fprintf(stderr, "Error: Image too large for iTerm2 encoding (%ux%u, max %ux%u)\n", img->width, img->height, ITERM2_MAX_DIMENSION, ITERM2_MAX_DIMENSION);
		return -1;
	}

	/* Task-009: Conditional PNG/JPEG encoding */
	uint8_t *encoded_data = NULL;
	size_t encoded_size = 0;
	const char *format_name;

	if (opts->iterm2_format == FORMAT_PNG) {
		/* PNG encoding with compression level 3 for speed */
		int compression = 3;
		if (encode_png(img, compression, &encoded_data, &encoded_size) != 0) {
			fprintf(stderr, "Error: Failed to encode PNG for iTerm2\n");
			return -1;
		}
		format_name = "PNG";

	} else {
		/* JPEG encoding with configured quality */
		if (encode_jpeg(img, opts->jpeg_quality, &encoded_data, &encoded_size) != 0) {
			fprintf(stderr, "Error: Failed to encode JPEG for iTerm2\n");
			return -1;
		}
		format_name = "JPEG";
	}

	/* Task-010: Bandwidth savings diagnostic output */
	if (!opts->silent && original_size > 0) {
		double reduction = ((double)(original_size - encoded_size) / original_size) * 100.0;
		fprintf(stderr, "iTerm2: Encoded to %s: %.1f MB → %.1f MB (%.1f%% reduction)\n", format_name, original_size / 1048576.0, encoded_size / 1048576.0, reduction);
	}

	/* Task-011: Base64 encode the PNG/JPEG data */
	size_t b64_size = 0;
	char *b64_encoded = base64_encode(encoded_data, encoded_size, &b64_size);
	if (b64_encoded == NULL) {
		fprintf(stderr, "Error: Failed to base64 encode image data\n");
		free(encoded_data);
		return -1;
	}

	/* Base64 encode filename if provided */
	char *encoded_filename = NULL;
	if (opts->input_file != NULL) {
		size_t filename_encoded_size = 0;
		encoded_filename = base64_encode((const uint8_t *)opts->input_file, strlen(opts->input_file), &filename_encoded_size);
	}

	/* Construct iTerm2 inline images escape sequence (OSC 1337) */
	if (opts->terminal.is_tmux) {
		/* Wrap with tmux DCS sequence: \033Ptmux;\033 ... \033\\ */
		printf("\033Ptmux;\033\033]1337;File=inline=1;size=%zu", encoded_size);
	} else {
		/* Standard OSC sequence */
		printf("\033]1337;File=inline=1;size=%zu", encoded_size);
	}

	/* Add filename parameter if available */
	if (encoded_filename != NULL) {
		printf(";name=%s", encoded_filename);
	}

	/* Add base64 image data */
	printf(":%s", b64_encoded);

	/* Terminate escape sequence */
	if (opts->terminal.is_tmux) {
		printf("\a\033\\"); /* BEL + tmux end DCS */
	} else {
		printf("\a"); /* BEL */
	}

	printf("\n");
	fflush(stdout);

	/* Cleanup */
	free(encoded_data);
	free(b64_encoded);
	if (encoded_filename != NULL) {
		free(encoded_filename);
	}

	return 0;
}
