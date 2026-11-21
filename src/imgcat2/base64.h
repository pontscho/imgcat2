/**
 * @file base64.h
 * @brief Base64 encoding for iTerm2 inline images protocol
 *
 * Provides base64 encoding functionality required by the iTerm2
 * inline images protocol (OSC 1337). Used to encode image data
 * for transmission to the terminal.
 */

#ifndef IMGCAT2_BASE64_H
#define IMGCAT2_BASE64_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Encode data to base64
 *
 * Encodes binary data to base64 string format according to RFC 4648.
 * The output is null-terminated and allocated with malloc().
 *
 * @param data Input data to encode (must not be NULL)
 * @param input_size Size of input data in bytes (must be > 0)
 * @param output_size Output parameter for encoded string size (excluding null terminator)
 *
 * @return Allocated base64 string, or NULL on error
 *
 * @note Caller must free returned string with free()
 * @note Output size will be ceil(input_size / 3) * 4 bytes
 * @note Returns NULL if data is NULL, input_size is 0, or allocation fails
 */
char *base64_encode(const uint8_t *data, size_t input_size, size_t *output_size);

#endif /* IMGCAT2_BASE64_H */
