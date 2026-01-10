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

#include "../decoders/magic.h"
#include "../core/base64.h"
#include "../core/cli.h"
#include "iterm2.h"

bool iterm2_is_format_supported(const uint8_t *data, size_t size)
{
	/* Validate inputs */
	if (data == NULL || size == 0) {
		return false;
	}

	/* Detect MIME type using magic bytes */
	mime_type_t mime = detect_mime_type(data, size);

	/* Check if format is supported by iTerm2 on macOS */
	switch (mime) {
		case MIME_PNG:
		case MIME_JPEG:
		case MIME_GIF:
		case MIME_WEBP:
		case MIME_TIFF:
		case MIME_BMP: return true;
		default: return false;
	}
}

int iterm2_render(const uint8_t *data, size_t size, const cli_options_t *opts, int target_width, int target_height)
{
	/* Validate inputs */
	if (data == NULL || size == 0) {
		fprintf(stderr, "Error: iTerm2 render called with invalid data\n");
		return -1;
	}

	/* Base64 encode the image data */
	size_t encoded_size = 0;
	char *encoded = base64_encode(data, size, &encoded_size);
	if (encoded == NULL) {
		fprintf(stderr, "Error: Failed to base64 encode image data\n");
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
		printf("\033Ptmux;\033\033]1337;File=inline=1;size=%zu", size);

	} else {
		/* Standard OSC sequence */
		printf("\033]1337;File=inline=1;size=%zu", size);
	}

	/* Add filename parameter if available */
	if (encoded_filename != NULL) {
		printf(";name=%s", encoded_filename);
	}

	/* Add width/height parameters based on CLI options */
	/* For iTerm2, default to original image size (no parameters) */
	/* Only add dimensions if explicitly specified by user */
	if (opts->fit_mode) {
		/* Fit mode: ignore explicit dimensions, use original size */
		printf(";width=90%%");
		printf(";height=90%%");
		printf(";preserveAspectRatio=1");

	} else if (target_width == -1 && target_height == -1) {
		/* Maximize image height in half of height of the terminal */
		printf(";height=50%%");
		printf(";preserveAspectRatio=1");

	} else if (target_width > 0 && target_height > 0) {
		/* Both dimensions specified: exact pixel dimensions */
		printf(";width=%dpx", target_width);
		printf(";height=%dpx", target_height);
		printf(";preserveAspectRatio=0");

	} else if (target_width > 0) {
		/* Only width specified: scale width, preserve aspect ratio */
		printf(";width=%dpx", target_width);
		printf(";preserveAspectRatio=1");

	} else if (target_height > 0) {
		/* Only height specified: scale height, preserve aspect ratio */
		printf(";height=%dpx", target_height);
		printf(";preserveAspectRatio=1");
	}
	/* Default: no dimensions specified, use original image size */
	/* Note: fit_mode is intentionally ignored in iTerm2 to preserve native quality */

	/* Add base64 image data */
	printf(":%s", encoded);

	/* Terminate escape sequence */
	if (opts->terminal.is_tmux) {
		printf("\a\033\\"); /* BEL + tmux end DCS */

	} else {
		printf("\a"); /* BEL */
	}

	printf("\n");
	fflush(stdout);

	/* Cleanup */
	free(encoded);
	if (encoded_filename != NULL) {
		free(encoded_filename);
	}

	return 0;
}
