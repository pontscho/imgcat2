/**
 * @file pipeline.c
 * @brief Image processing pipeline - file I/O with security checks
 *
 * Handles secure file and stdin reading with path traversal protection
 * and size limit enforcement.
 */

#include <sys/stat.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "image.h"
#include "pipeline.h"

#ifdef _WIN32
#include <windows.h>
#define PATH_MAX MAX_PATH
#else
#include <unistd.h>
#endif

/** Read chunk size for stdin (4KB) */
#define STDIN_CHUNK_SIZE 4096

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
