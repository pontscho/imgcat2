/**
 * @file base64.c
 * @brief Base64 encoding implementation
 *
 * Implements RFC 4648 base64 encoding for the iTerm2 inline images protocol.
 */

#include <stdlib.h>
#include <string.h>

#include "base64.h"

/**
 * @brief Base64 encoding table (RFC 4648)
 */
static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *base64_encode(const uint8_t *data, size_t input_size, size_t *output_size)
{
	/* Validate inputs */
	if (data == NULL || input_size == 0 || output_size == NULL) {
		return NULL;
	}

	/* Calculate output size: ceil(input_size / 3) * 4 */
	size_t encoded_size = ((input_size + 2) / 3) * 4;

	/* Allocate output buffer (+1 for null terminator) */
	char *encoded = malloc(encoded_size + 1);
	if (encoded == NULL) {
		return NULL;
	}

	size_t i = 0; /* Input position */
	size_t j = 0; /* Output position */

	/* Process 3-byte chunks */
	while (i + 2 < input_size) {
		/* First 6 bits of byte 0 */
		encoded[j++] = base64_table[(data[i] >> 2) & 0x3F];

		/* Last 2 bits of byte 0 + first 4 bits of byte 1 */
		encoded[j++] = base64_table[((data[i] & 0x03) << 4) | ((data[i + 1] >> 4) & 0x0F)];

		/* Last 4 bits of byte 1 + first 2 bits of byte 2 */
		encoded[j++] = base64_table[((data[i + 1] & 0x0F) << 2) | ((data[i + 2] >> 6) & 0x03)];

		/* Last 6 bits of byte 2 */
		encoded[j++] = base64_table[data[i + 2] & 0x3F];

		i += 3;
	}

	/* Handle remaining bytes with padding */
	if (i < input_size) {
		/* First 6 bits of remaining byte */
		encoded[j++] = base64_table[(data[i] >> 2) & 0x3F];

		if (i + 1 < input_size) {
			/* 2 bytes remaining */
			encoded[j++] = base64_table[((data[i] & 0x03) << 4) | ((data[i + 1] >> 4) & 0x0F)];
			encoded[j++] = base64_table[((data[i + 1] & 0x0F) << 2)];
			encoded[j++] = '=';

		} else {
			/* 1 byte remaining */
			encoded[j++] = base64_table[((data[i] & 0x03) << 4)];
			encoded[j++] = '=';
			encoded[j++] = '=';
		}
	}

	/* Null-terminate the string */
	encoded[j] = '\0';

	/* Set output size */
	*output_size = j;

	return encoded;
}
