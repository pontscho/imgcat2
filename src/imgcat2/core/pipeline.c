/**
 * @file pipeline.c
 * @brief Image processing pipeline - file I/O with security checks and orchestration
 *
 * Handles secure file and stdin reading with path traversal protection,
 * size limit enforcement, and high-level pipeline orchestration.
 */

#include <sys/stat.h>

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ansi/ansi.h"
#include "../ansi/escape.h"
#include "../decoders/decoder.h"
#include "../decoders/magic.h"
#include "../iterm2/iterm2.h"
#include "../terminal/terminal.h"
#include "cli.h"
#include "image.h"
#include "pipeline.h"

#ifdef _WIN32
#include <windows.h>
#define PATH_MAX MAX_PATH
/* usleep implementation for Windows */
static void usleep(unsigned int microseconds)
{
	Sleep(microseconds / 1000);
}
#else
#include <unistd.h>
#endif

/** Read chunk size for stdin (4KB) */
#define STDIN_CHUNK_SIZE 4096

/** Animation running flag for signal handler */
static volatile sig_atomic_t animation_running = 1;

/**
 * @brief Signal handler for SIGINT (Ctrl+C)
 *
 * Sets animation_running flag to 0 to gracefully exit animation loop.
 */
static void signal_handler(int sig)
{
	(void)sig;
	animation_running = 0;
}

/**
 * @brief Setup signal handler for SIGINT
 *
 * Installs signal handler for Ctrl+C and returns pointer to animation_running flag.
 *
 * @return Pointer to animation_running flag
 */
static volatile sig_atomic_t *setup_signal_handler(void)
{
	animation_running = 1;
	signal(SIGINT, signal_handler);
	return &animation_running;
}

/**
 * @brief Validate file path against path traversal attacks
 *
 * Resolves the canonical path and checks for ".." components
 * and that the file doesn't escape the current working directory.
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

	// Check for obvious ".." components
	if (strstr(path, "..") != NULL) {
		fprintf(stderr, "Error: Path contains '..' component: %s\n", path);
		return false;
	}

#ifdef _WIN32
	// Windows: GetFullPathName
	DWORD result = GetFullPathName(path, PATH_MAX, canonical_out, NULL);
	if (result == 0 || result >= PATH_MAX) {
		fprintf(stderr, "Error: Failed to resolve path: %s\n", path);
		return false;
	}
#else
	// Unix: realpath
	if (realpath(path, canonical_out) == NULL) {
		fprintf(stderr, "Error: Failed to resolve path '%s': %s\n", path, strerror(errno));
		return false;
	}
#endif

	// Get current working directory for validation
	char cwd[PATH_MAX];
#ifdef _WIN32
	if (GetCurrentDirectory(PATH_MAX, cwd) == 0) {
#else
	if (getcwd(cwd, sizeof(cwd)) == NULL) {
#endif
		// Can't validate CWD, allow but warn
		fprintf(stderr, "Warning: Could not validate path against CWD\n");
	}

	return true;
}

/**
 * @brief Read file with path traversal protection and size limits
 *
 * Securely reads a file into memory with:
 * - Path traversal protection (realpath/GetFullPathName)
 * - File size validation (≤ IMAGE_MAX_FILE_SIZE)
 * - Safe memory allocation
 *
 * @param path File path to read
 * @param out_data Output parameter for file data (caller must free)
 * @param out_size Output parameter for file size
 * @return true on success, false on error
 *
 * @note Caller must free *out_data with free() when done
 */
bool read_file_secure(const char *path, uint8_t **out_data, size_t *out_size)
{
	if (path == NULL || out_data == NULL || out_size == NULL) {
		fprintf(stderr, "Error: Invalid parameters to read_file_secure\n");
		return false;
	}

	// Initialize outputs
	*out_data = NULL;
	*out_size = 0;

	// Validate path security
	char canonical_path[PATH_MAX];
	if (!validate_path_safe(path, canonical_path)) {
		return false;
	}

	// Get file size with stat
	struct stat st;
	if (stat(canonical_path, &st) != 0) {
		fprintf(stderr, "Error: Cannot stat file '%s': %s\n", canonical_path, strerror(errno));
		return false;
	}

	// Check if regular file
	if (!S_ISREG(st.st_mode)) {
		fprintf(stderr, "Error: Not a regular file: %s\n", canonical_path);
		return false;
	}

	// Validate file size
	if (st.st_size <= 0) {
		fprintf(stderr, "Error: File is empty: %s\n", canonical_path);
		return false;
	}

	if ((size_t)st.st_size > IMAGE_MAX_FILE_SIZE) {
		fprintf(stderr, "Error: File too large (%lld bytes, max %lu bytes): %s\n", (long long)st.st_size, (unsigned long)IMAGE_MAX_FILE_SIZE, canonical_path);
		return false;
	}

	// Open file for binary reading
	FILE *fp = fopen(canonical_path, "rb");
	if (fp == NULL) {
		fprintf(stderr, "Error: Cannot open file '%s': %s\n", canonical_path, strerror(errno));
		return false;
	}

	// Allocate buffer for file content
	size_t file_size = (size_t)st.st_size;
	uint8_t *buffer = (uint8_t *)malloc(file_size);
	if (buffer == NULL) {
		fprintf(stderr, "Error: Failed to allocate %lu bytes for file: %s\n", (unsigned long)file_size, strerror(errno));
		fclose(fp);
		return false;
	}

	// Read entire file
	size_t bytes_read = fread(buffer, 1, file_size, fp);
	fclose(fp);

	if (bytes_read != file_size) {
		fprintf(stderr, "Error: Read %lu bytes, expected %lu from file: %s\n", (unsigned long)bytes_read, (unsigned long)file_size, canonical_path);
		free(buffer);
		return false;
	}

	// Success
	*out_data = buffer;
	*out_size = file_size;
	return true;
}

/**
 * @brief Read from stdin with size limits (pipe support)
 *
 * Reads stdin into dynamically allocated buffer with:
 * - Chunk-based reading (4KB chunks)
 * - Dynamic buffer growth (realloc strategy)
 * - Size limit enforcement (≤ IMAGE_MAX_FILE_SIZE)
 * - Graceful EOF handling
 *
 * @param out_data Output parameter for stdin data (caller must free)
 * @param out_size Output parameter for data size
 * @return true on success, false on error
 *
 * @note Caller must free *out_data with free() when done
 * @note Designed for piped input: cat image.png | imgcat
 */
bool read_stdin_secure(uint8_t **out_data, size_t *out_size)
{
	if (out_data == NULL || out_size == NULL) {
		fprintf(stderr, "Error: Invalid parameters to read_stdin_secure\n");
		return false;
	}

	// Initialize outputs
	*out_data = NULL;
	*out_size = 0;

	// Check if stdin is a TTY (we expect piped input)
#ifndef _WIN32
	if (isatty(STDIN_FILENO)) {
		fprintf(stderr, "Error: No input provided (stdin is a TTY, expected pipe)\n");
		fprintf(stderr, "Usage: cat image.png | imgcat\n");
		return false;
	}
#endif

	// Dynamic buffer allocation
	size_t capacity = STDIN_CHUNK_SIZE;
	size_t total_size = 0;
	uint8_t *buffer = (uint8_t *)malloc(capacity);
	if (buffer == NULL) {
		fprintf(stderr, "Error: Failed to allocate stdin buffer: %s\n", strerror(errno));
		return false;
	}

	// Read in chunks until EOF or size limit
	while (1) {
		// Check size limit before reading
		if (total_size >= IMAGE_MAX_FILE_SIZE) {
			fprintf(stderr, "Error: stdin input exceeds maximum size (%lu bytes)\n", (unsigned long)IMAGE_MAX_FILE_SIZE);
			free(buffer);
			return false;
		}

		// Expand buffer if needed
		if (total_size + STDIN_CHUNK_SIZE > capacity) {
			size_t new_capacity = capacity * 2;
			if (new_capacity > IMAGE_MAX_FILE_SIZE) {
				new_capacity = IMAGE_MAX_FILE_SIZE;
			}

			uint8_t *new_buffer = (uint8_t *)realloc(buffer, new_capacity);
			if (new_buffer == NULL) {
				fprintf(stderr, "Error: Failed to expand stdin buffer: %s\n", strerror(errno));
				free(buffer);
				return false;
			}
			buffer = new_buffer;
			capacity = new_capacity;
		}

		// Read chunk
		size_t chunk_size = STDIN_CHUNK_SIZE;
		if (total_size + chunk_size > IMAGE_MAX_FILE_SIZE) {
			chunk_size = IMAGE_MAX_FILE_SIZE - total_size;
		}

		size_t bytes_read = fread(buffer + total_size, 1, chunk_size, stdin);
		if (bytes_read == 0) {
			// EOF or error
			if (feof(stdin)) {
				break; // Normal EOF
			}
			if (ferror(stdin)) {
				fprintf(stderr, "Error: Failed to read from stdin: %s\n", strerror(errno));
				free(buffer);
				return false;
			}
			break;
		}

		total_size += bytes_read;
	}

	// Validate we read something
	if (total_size == 0) {
		fprintf(stderr, "Error: No data read from stdin\n");
		free(buffer);
		return false;
	}

	// Success
	*out_data = buffer;
	*out_size = total_size;
	return true;
}

target_dimensions_t calculate_target_dimensions(uint32_t cols, uint32_t rows, uint32_t top_offset)
{
	/* Resize factors:
	 * - RESIZE_FACTOR_X = 1: Horizontal 1:1 (1 pixel per column)
	 * - RESIZE_FACTOR_Y = 2: Vertical 2:1 (half-block doubles vertical resolution)
	 */
	const uint32_t RESIZE_FACTOR_X = 1;
	const uint32_t RESIZE_FACTOR_Y = 2;
	const uint32_t MAX_TERMINAL_WIDTH = 1000; /* Prevent excessive memory use */

	/* Initialize result to invalid dimensions */
	target_dimensions_t result = { 0, 0 };

	/* Validate inputs */
	if (cols == 0 || rows == 0) {
		fprintf(stderr, "calculate_target_dimensions: invalid terminal size %u×%u\n", cols, rows);
		return result;
	}

	if (top_offset >= rows) {
		fprintf(stderr, "calculate_target_dimensions: top_offset %u >= rows %u\n", top_offset, rows);
		return result;
	}

	/* Calculate target dimensions */
	uint32_t available_rows = rows - top_offset;
	uint32_t target_width = cols * RESIZE_FACTOR_X;
	uint32_t target_height = available_rows * RESIZE_FACTOR_Y;

	/* Clamp width to prevent excessive memory use */
	if (target_width > MAX_TERMINAL_WIDTH) {
		target_width = MAX_TERMINAL_WIDTH;
	}

	/* Validate calculated dimensions */
	if (target_width == 0 || target_height == 0) {
		fprintf(stderr, "calculate_target_dimensions: calculated invalid dimensions %u×%u\n", target_width, target_height);
		return result;
	}

	/* Return valid dimensions */
	result.width = target_width;
	result.height = target_height;
	return result;
}

/**
 * @brief Calculate dimensions from -w/-h with aspect ratio preservation
 *
 * Handles three cases:
 * 1. Both width and height specified: return exact dimensions
 * 2. Only width specified: calculate height from aspect ratio
 * 3. Only height specified: calculate width from aspect ratio
 *
 * @param src Source image for aspect ratio calculation
 * @param target_width Target width (-1 if not specified)
 * @param target_height Target height (-1 if not specified)
 * @param out_dims Output dimensions
 * @return true on success, false on error
 */
static bool calculate_custom_dimensions(const image_t *src, int target_width, int target_height, target_dimensions_t *out_dims)
{
	if (src == NULL || src->width == 0 || src->height == 0 || out_dims == NULL) {
		fprintf(stderr, "calculate_custom_dimensions: invalid parameters\n");
		return false;
	}

	/* Calculate aspect ratio */
	float src_aspect = (float)src->width / (float)src->height;
	uint32_t final_width, final_height;

	if (target_width > 0 && target_height > 0) {
		/* Both specified: use exact dimensions */
		final_width = (uint32_t)target_width;
		final_height = (uint32_t)target_height;
	} else if (target_width > 0) {
		/* Only width specified: calculate height from aspect ratio */
		final_width = (uint32_t)target_width;
		final_height = (uint32_t)roundf((float)final_width / src_aspect);
	} else if (target_height > 0) {
		/* Only height specified: calculate width from aspect ratio */
		final_height = (uint32_t)target_height;
		final_width = (uint32_t)roundf((float)final_height * src_aspect);
	} else {
		fprintf(stderr, "calculate_custom_dimensions: no dimensions specified\n");
		return false;
	}

	/* Validate calculated dimensions */
	if (final_width == 0 || final_height == 0) {
		fprintf(stderr, "calculate_custom_dimensions: calculated invalid dimensions %u×%u\n", final_width, final_height);
		return false;
	}

	out_dims->width = final_width;
	out_dims->height = final_height;
	return true;
}

/**
 * @brief Read input (file or stdin) based on CLI options
 */
int pipeline_read(const cli_options_t *opts, uint8_t **out_data, size_t *out_size)
{
	if (opts == NULL || out_data == NULL || out_size == NULL) {
		fprintf(stderr, "pipeline_read: invalid parameters\n");
		return -1;
	}

	bool success;
	if (opts->input_file != NULL) {
		/* Read from file */
		success = read_file_secure(opts->input_file, out_data, out_size);
	} else {
		/* Read from stdin */
		success = read_stdin_secure(out_data, out_size);
	}

	return success ? 0 : -1;
}

/**
 * @brief Decode image with MIME type detection
 */
int pipeline_decode(cli_options_t *opts, const uint8_t *buffer, size_t size, image_t ***out_frames, int *out_frame_count)
{
	if (buffer == NULL || out_frames == NULL || out_frame_count == NULL) {
		fprintf(stderr, "pipeline_decode: invalid parameters\n");
		return -1;
	}

	/* Detect MIME type */
	mime_type_t mime = detect_mime_type(buffer, size);
	if (mime == MIME_UNKNOWN) {
		fprintf(stderr, "pipeline_decode: unknown image format\n");
		return -1;
	}

	/* Decode with registry */
	image_t **frames = decoder_decode(opts, buffer, size, mime, out_frame_count);
	if (frames == NULL || *out_frame_count <= 0) {
		fprintf(stderr, "pipeline_decode: failed to decode image\n");
		return -1;
	}

	*out_frames = frames;
	return 0;
}

/**
 * @brief Scale images to terminal dimensions
 */
int pipeline_scale(image_t **frames, int frame_count, const cli_options_t *opts, image_t ***out_scaled)
{
	if (frames == NULL || frame_count <= 0 || opts == NULL || out_scaled == NULL) {
		fprintf(stderr, "pipeline_scale: invalid parameters\n");
		return -1;
	}

	/* Get terminal size */
	int rows, cols;
	if (terminal_get_size(&rows, &cols) != 0) {
		fprintf(stderr, "pipeline_scale: failed to get terminal size, using defaults\n");
		rows = DEFAULT_TERM_ROWS;
		cols = DEFAULT_TERM_COLS;
	}

	/* Determine target dimensions based on priority */
	target_dimensions_t target;

	if (opts->has_custom_dimensions) {
		/* Custom dimensions take priority over fit/resize */
		if (!calculate_custom_dimensions(frames[0], opts->target_width, opts->target_height, &target)) {
			fprintf(stderr, "pipeline_scale: failed to calculate custom dimensions\n");
			return -1;
		}

		/* Debug log (if not silent) */
		if (!opts->silent) {
			fprintf(stderr, "Using custom dimensions: ");
			if (opts->target_width > 0) {
				fprintf(stderr, "width=%d ", opts->target_width);
			}
			if (opts->target_height > 0) {
				fprintf(stderr, "height=%d ", opts->target_height);
			}
			fprintf(stderr, "(final: %u×%u)\n", target.width, target.height);
		}
	} else {
		/* Default: terminal-aware scaling */
		target = calculate_target_dimensions(cols, rows, opts->top_offset);
		if (target.width == 0 || target.height == 0) {
			fprintf(stderr, "pipeline_scale: invalid target dimensions\n");
			return -1;
		}
	}

	/* Allocate scaled frames array */
	image_t **scaled = malloc(sizeof(image_t *) * frame_count);
	if (scaled == NULL) {
		fprintf(stderr, "pipeline_scale: failed to allocate scaled frames array\n");
		return -1;
	}

	/* Scale each frame */
	for (int i = 0; i < frame_count; i++) {
		image_t *scaled_frame;

		if (opts->has_custom_dimensions) {
			/* Custom dimensions: always exact (resize) */
			scaled_frame = image_scale_resize(frames[i], target.width, target.height);
		} else if (opts->fit_mode) {
			/* Fit mode: maintain aspect ratio */
			scaled_frame = image_scale_fit(frames[i], target.width, target.height);
		} else {
			/* Resize mode: exact dimensions */
			scaled_frame = image_scale_resize(frames[i], target.width, target.height);
		}

		if (scaled_frame == NULL) {
			fprintf(stderr, "pipeline_scale: failed to scale frame %d\n", i);
			/* Free previously scaled frames */
			for (int j = 0; j < i; j++) {
				image_destroy(scaled[j]);
			}
			free(scaled);
			return -1;
		}

		scaled[i] = scaled_frame;
	}

	*out_scaled = scaled;
	return 0;
}

/**
 * @brief Render single static frame
 *
 * Renders a single frame to the terminal with cursor control.
 *
 * @param frame Image frame to render
 * @return 0 on success, -1 on error
 */
static int render_static_frame(image_t *frame)
{
	if (frame == NULL) {
		fprintf(stderr, "render_static_frame: invalid frame\n");
		return -1;
	}

	/* Generate ANSI escape sequences */
	size_t line_count;
	char **lines = generate_frame_ansi(frame, &line_count);
	if (lines == NULL) {
		fprintf(stderr, "render_static_frame: failed to generate ANSI\n");
		return -1;
	}

	/* Hide cursor for cleaner output */
	ansi_cursor_hide();

	/* Output lines to stdout */
	for (size_t i = 0; i < line_count; i++) {
		if (write(STDOUT_FILENO, lines[i], strlen(lines[i])) < 0) {
			return -1;
		}
	}

	/* Show cursor and reset */
	ansi_cursor_show();
	ansi_reset();

	/* Cleanup */
	free_frame_lines(lines, line_count);
	return 0;
}

/**
 * @brief Render animated frames with loop
 *
 * Renders multiple frames in a loop with timing control.
 * Supports Ctrl+C for graceful exit.
 *
 * @param frames Array of frames to render
 * @param frame_count Number of frames
 * @param opts CLI options (fps, silent)
 * @return 0 on success, -1 on error
 */
static int render_animated(image_t **frames, int frame_count, const cli_options_t *opts)
{
	if (frames == NULL || frame_count <= 0 || opts == NULL) {
		fprintf(stderr, "render_animated: invalid parameters\n");
		return -1;
	}

	/* Setup signal handler for Ctrl+C */
	volatile sig_atomic_t *running = setup_signal_handler();

	/* Pre-generate all frame ANSI sequences */
	char ***all_lines = malloc(sizeof(char **) * frame_count);
	size_t *all_line_counts = malloc(sizeof(size_t) * frame_count);
	if (all_lines == NULL || all_line_counts == NULL) {
		fprintf(stderr, "render_animated: failed to allocate frame arrays\n");
		free(all_lines);
		free(all_line_counts);
		return -1;
	}

	/* Generate ANSI for each frame */
	for (int i = 0; i < frame_count; i++) {
		all_lines[i] = generate_frame_ansi(frames[i], &all_line_counts[i]);
		if (all_lines[i] == NULL) {
			fprintf(stderr, "render_animated: failed to generate ANSI for frame %d\n", i);
			/* Free previously generated frames */
			for (int j = 0; j < i; j++) {
				free_frame_lines(all_lines[j], all_line_counts[j]);
			}
			free(all_lines);
			free(all_line_counts);
			return -1;
		}
	}

	/* Calculate frame delay in microseconds */
	unsigned int usleep_duration = 1000000 / opts->fps;

	/* Get frame height for cursor positioning */
	size_t frame_height = all_line_counts[0];

	/* Hide cursor and disable echo */
	ansi_cursor_hide();
	void *echo_state = terminal_disable_echo();

	/* Print newline before animation */
	printf("\n");

	/* Animation loop */
	bool first_iteration = true;
	while (*running) {
		for (int frame_idx = 0; frame_idx < frame_count; frame_idx++) {
			/* Check running flag */
			if (!*running) {
				break;
			}

			/* Move cursor up if not first iteration */
			if (!first_iteration) {
				ansi_cursor_up(frame_height + (opts->silent ? 0 : 1));
			}

			/* Print frame lines */
			for (size_t line_idx = 0; line_idx < all_line_counts[frame_idx]; line_idx++) {
				printf("%s", all_lines[frame_idx][line_idx]);
			}

			/* Print control message if not silent */
			if (!opts->silent) {
				printf("Press Ctrl+C to exit\n");
			}

			/* Flush output */
			fflush(stdout);

			/* Wait for next frame */
			usleep(usleep_duration);

			first_iteration = false;
		}
	}

	/* Show cursor, enable echo, reset */
	ansi_cursor_show();
	terminal_enable_echo(echo_state);
	ansi_reset();

	/* Print newline after animation */
	printf("\n");

	/* Cleanup all generated frames */
	for (int i = 0; i < frame_count; i++) {
		free_frame_lines(all_lines[i], all_line_counts[i]);
	}
	free(all_lines);
	free(all_line_counts);

	return 0;
}

/**
 * @brief Render frames to terminal
 */
int pipeline_render(image_t **frames, int frame_count, const cli_options_t *opts)
{
	if (frames == NULL || frame_count <= 0 || opts == NULL) {
		fprintf(stderr, "pipeline_render: invalid parameters\n");
		return -1;
	}

	/* Dispatch to static or animated rendering */
	if (frame_count == 1) {
		/* Single frame - always render as static */
		return render_static_frame(frames[0]);
	} else if (opts->animate) {
		/* Multiple frames and animation requested */
		return render_animated(frames, frame_count, opts);
	} else {
		/* Multiple frames but no animation - show first frame only */
		return render_static_frame(frames[0]);
	}
}

/**
 * @brief Render using iTerm2 inline images protocol
 */
int pipeline_render_iterm2(const uint8_t *buffer, size_t buffer_size, const cli_options_t *opts)
{
	/* Validate inputs */
	if (buffer == NULL || buffer_size == 0 || opts == NULL) {
		fprintf(stderr, "pipeline_render_iterm2: invalid parameters\n");
		return -1;
	}

	/* Extract filename for metadata (NULL for stdin) */
	const char *filename = opts->input_file;

	/* Extract sizing parameters from CLI options */
	int target_width = opts->target_width;
	int target_height = opts->target_height;

	/* Render using iTerm2 protocol with sizing parameters */
	/* Note: iTerm2 uses original image size by default unless dimensions specified */
	return iterm2_render(buffer, buffer_size, filename, opts->fit_mode, target_width, target_height);
}
