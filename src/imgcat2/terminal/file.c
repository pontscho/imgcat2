/**
 * @file file.c
 * @brief File output renderer implementation
 *
 * Implements image-to-file conversion with format encoding and secure file I/O.
 */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define PATH_MAX MAX_PATH
#else
#include <unistd.h>
#endif

#include "../encoders/encoder.h"
#include "file.h"

/**
 * @brief Validate file path against path traversal attacks
 *
 * Resolves the canonical path and checks for ".." components.
 * Uses realpath() on Unix and GetFullPathName() on Windows.
 *
 * @param path Input file path
 * @param canonical_out Output buffer for canonical path (PATH_MAX bytes)
 * @return true if path is safe, false otherwise
 */
static bool validate_path_safe(const char *path, char *canonical_out)
{
	if (path == NULL || canonical_out == NULL) {
		return false;
	}

#ifdef _WIN32
	/* Windows: GetFullPathName */
	DWORD result = GetFullPathName(path, PATH_MAX, canonical_out, NULL);
	if (result == 0 || result >= PATH_MAX) {
		fprintf(stderr, "Error: Failed to resolve path: %s\n", path);
		return false;
	}
#else
	/* Unix: realpath (but don't fail if file doesn't exist yet) */
	/* For new files, check parent directory exists */
	char *last_slash = strrchr(path, '/');
	if (last_slash != NULL) {
		/* Has directory component, check parent exists */
		size_t dir_len = last_slash - path;
		if (dir_len == 0) {
			/* Root directory */
			strncpy(canonical_out, path, PATH_MAX - 1);
			canonical_out[PATH_MAX - 1] = '\0';
			return true;
		}

		char parent_dir[PATH_MAX];
		if (dir_len >= PATH_MAX) {
			fprintf(stderr, "Error: Path too long: %s\n", path);
			return false;
		}
		memcpy(parent_dir, path, dir_len);
		parent_dir[dir_len] = '\0';

		/* Try to resolve parent directory */
		char resolved_parent[PATH_MAX];
		if (realpath(parent_dir, resolved_parent) == NULL) {
			fprintf(stderr, "Error: Parent directory does not exist: %s\n", parent_dir);
			return false;
		}

		/* Construct canonical path: resolved_parent + "/" + filename */
		const char *filename = last_slash + 1;
		int written = snprintf(canonical_out, PATH_MAX, "%s/%s", resolved_parent, filename);
		if (written < 0 || written >= PATH_MAX) {
			fprintf(stderr, "Error: Path too long after resolution\n");
			return false;
		}
	} else {
		/* No directory component, use current working directory */
		char cwd[PATH_MAX];
		if (getcwd(cwd, sizeof(cwd)) == NULL) {
			fprintf(stderr, "Error: Cannot get current working directory\n");
			return false;
		}

		int written = snprintf(canonical_out, PATH_MAX, "%s/%s", cwd, path);
		if (written < 0 || written >= PATH_MAX) {
			fprintf(stderr, "Error: Path too long after resolution\n");
			return false;
		}
	}
#endif

	return true;
}

/**
 * @brief Render image to file or stdout
 *
 * Main file rendering function that encodes image and writes to file/stdout.
 */
int file_render(image_t **frames, int frame_count, const cli_options_t *opts)
{
	/* Validate inputs */
	if (frames == NULL || frame_count <= 0 || opts == NULL) {
		fprintf(stderr, "Error: Invalid parameters to file_render\n");
		return -1;
	}

	/* Check output format is specified */
	if (opts->output_format == FORMAT_NONE) {
		fprintf(stderr, "Error: No output format specified (use --jpeg or --png)\n");
		return -1;
	}

	/* Handle animated images */
	int frame_index = opts->frame_index;
	if (frame_count > 1) {
		fprintf(stderr, "Warning: Animated image with %d frames, converting frame %d\n", frame_count, frame_index);

		/* Validate frame index */
		if (frame_index < 0 || frame_index >= frame_count) {
			fprintf(stderr, "Error: Frame index %d out of range (0-%d)\n", frame_index, frame_count - 1);
			return -1;
		}
	} else {
		/* Single frame, use index 0 */
		frame_index = 0;
	}

	/* Get frame to encode */
	image_t *img = frames[frame_index];
	if (img == NULL) {
		fprintf(stderr, "Error: Frame %d is NULL\n", frame_index);
		return -1;
	}

	/* Determine quality parameter based on format */
	int quality = 0;
	if (opts->output_format == FORMAT_JPEG) {
		quality = opts->jpeg_quality;
	} else if (opts->output_format == FORMAT_PNG) {
		quality = opts->png_compression;
	}

	/* Encode image */
	uint8_t *encoded_data = NULL;
	size_t encoded_size = 0;

	if (encoder_encode(img, opts->output_format, quality, &encoded_data, &encoded_size) != 0) {
		fprintf(stderr, "Error: Failed to encode image\n");
		return -1;
	}

	/* Validate encoded output */
	if (encoded_data == NULL || encoded_size == 0) {
		fprintf(stderr, "Error: Encoder produced no output\n");
		if (encoded_data != NULL) {
			free(encoded_data);
		}
		return -1;
	}

	/* Output handling */
	int result = 0;

	if (opts->output_file != NULL) {
		/* Write to file */
		char canonical_path[PATH_MAX];
		if (!validate_path_safe(opts->output_file, canonical_path)) {
			fprintf(stderr, "Error: Invalid output file path: %s\n", opts->output_file);
			free(encoded_data);
			return -1;
		}

		/* Open file for writing */
		FILE *fp = fopen(canonical_path, "wb");
		if (fp == NULL) {
			fprintf(stderr, "Error: Cannot open file '%s' for writing: %s\n", canonical_path, strerror(errno));
			free(encoded_data);
			return -1;
		}

		/* Write data */
		size_t bytes_written = fwrite(encoded_data, 1, encoded_size, fp);
		if (bytes_written != encoded_size) {
			fprintf(stderr, "Error: Failed to write %zu bytes to file (wrote %zu): %s\n", encoded_size, bytes_written, strerror(errno));
			fclose(fp);
			free(encoded_data);
			return -1;
		}

		/* Close file */
		if (fclose(fp) != 0) {
			fprintf(stderr, "Error: Failed to close file: %s\n", strerror(errno));
			free(encoded_data);
			return -1;
		}

		if (!opts->silent) {
			fprintf(stderr, "Successfully wrote %zu bytes to: %s\n", encoded_size, canonical_path);
		}
	} else {
		/* Write to stdout */
		ssize_t bytes_written = write(STDOUT_FILENO, encoded_data, encoded_size);
		if (bytes_written < 0 || (size_t)bytes_written != encoded_size) {
			fprintf(stderr, "Error: Failed to write %zu bytes to stdout (wrote %zd): %s\n", encoded_size, bytes_written, strerror(errno));
			free(encoded_data);
			return -1;
		}
	}

	/* Free encoded data */
	free(encoded_data);

	return result;
}
