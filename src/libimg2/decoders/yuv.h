/**
 * @file yuv.h
 * @brief Raw (headerless) 8-bit YUV input: format descriptors, spec parsing, decoding
 *
 * Types and functions are public (<img2/yuv.h>); img2_decode_yuv() is declared in decoder.h.
 *
 * Each plane occupies stride * rows bytes, except the very last row of the
 * last plane, of which only the visible bytes must be present (lenient size).
 *
 * Conversion: BT.601 limited range (Y 16-235, C 16-240), fixed-point integer
 * math, nearest-neighbour (replicated) chroma upsampling, alpha = 255.
 */

#ifndef IMG2_INTERNAL_DECODERS_YUV_H
#define IMG2_INTERNAL_DECODERS_YUV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <img2/yuv.h>

#include "../core/image.h"

#endif /* IMG2_INTERNAL_DECODERS_YUV_H */
