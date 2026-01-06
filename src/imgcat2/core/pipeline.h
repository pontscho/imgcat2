/**
 * @file pipeline.h
 * @brief Image processing pipeline API - secure file I/O
 *
 * Provides secure file and stdin reading with path traversal protection,
 * size limit enforcement, and safe memory management.
 */

#ifndef IMGCAT2_PIPELINE_H
#define IMGCAT2_PIPELINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Read file with path traversal protection and size limits
 *
 * Securely reads a file into memory with:
 * - Path traversal protection (realpath/GetFullPathName)
 * - ".." component validation
 * - File size validation (≤ IMAGE_MAX_FILE_SIZE from image.h)
 * - Safe memory allocation
 *
 * @param path File path to read
 * @param out_data Output parameter for file data (caller must free with free())
 * @param out_size Output parameter for file size in bytes
 * @return true on success, false on error
 *
 * @note Caller must free *out_data with free() when done
 * @note Returns false if path contains "..", file too large, or I/O error
 *
 * @example
 * uint8_t* data;
 * size_t size;
 * if (read_file_secure("image.png", &data, &size)) {
 *     // Process data...
 *     free(data);
 * }
 */
bool read_file_secure(const char *path, uint8_t **out_data, size_t *out_size);

/**
 * @brief Read from stdin with size limits (pipe support)
 *
 * Reads stdin into dynamically allocated buffer with:
 * - Chunk-based reading (4KB chunks)
 * - Dynamic buffer growth (realloc strategy)
 * - Size limit enforcement (≤ IMAGE_MAX_FILE_SIZE)
 * - Graceful EOF handling
 * - TTY detection (expects piped input)
 *
 * @param out_data Output parameter for stdin data (caller must free with free())
 * @param out_size Output parameter for data size in bytes
 * @return true on success, false on error
 *
 * @note Caller must free *out_data with free() when done
 * @note Designed for piped input: cat image.png | imgcat
 * @note Returns false if stdin is TTY, size limit exceeded, or I/O error
 *
 * @example
 * uint8_t* data;
 * size_t size;
 * if (read_stdin_secure(&data, &size)) {
 *     // Process data...
 *     free(data);
 * }
 */
bool read_stdin_secure(uint8_t **out_data, size_t *out_size);

#endif /* IMGCAT2_PIPELINE_H */
