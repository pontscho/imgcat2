/**
 * @file decoder_internal.h
 * @brief Internal decoder function declarations for unit testing
 *
 * This header exposes internal decoder functions for unit testing purposes.
 * Production code should use the public decoder.h API instead.
 */

#ifndef IMGCAT2_TESTS_DECODER_INTERNAL_H
#define IMGCAT2_TESTS_DECODER_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "../libimg2/core/image.h"

/* Forward declarations of internal decoder functions */
extern img2_image_t **img2_decode_png(const uint8_t *data, size_t len, int *frame_count);
extern img2_image_t **img2_decode_jpeg(const uint8_t *data, size_t len, int *frame_count);
extern img2_image_t **img2_decode_stb(const uint8_t *data, size_t len, int *frame_count);
extern img2_image_t **img2_decode_pnm(const uint8_t *data, size_t len, int *frame_count);

#endif /* IMGCAT2_TESTS_DECODER_INTERNAL_H */
