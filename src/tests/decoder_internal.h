/**
 * @file decoder_internal.h
 * @brief Decoder declarations and legacy-shaped call shims for unit testing
 *
 * The decoder entry points (img2_decode_png/jpeg/gif_animated/stb/pnm/...) are
 * declared by the library's decoders/decoder.h with the S6 contract
 * (img2_status_t + img2_frames_t). The shims below run a decoder through that
 * contract and hand back the (array, count) view the older test bodies assert
 * on; tests that check status codes call the decoders directly.
 */

#ifndef IMGCAT2_TESTS_DECODER_INTERNAL_H
#define IMGCAT2_TESTS_DECODER_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "../libimg2/decoders/decoder.h"

/* Adopt a successful result; NULL (and *frame_count = 0) on any error status */
static inline img2_image_t **test_frames_take(img2_status_t st, img2_frames_t *fr, int *frame_count)
{
	if (st != IMG2_OK) {
		img2_frames_free(fr);
		return NULL;
	}
	*frame_count = fr->count;
	return fr->frames;
}

/* fn(data, len, NULL opts) through the S6 contract */
static inline img2_image_t **test_decode(img2_decode_fn fn, const uint8_t *data, size_t len, int *frame_count)
{
	if (frame_count == NULL) {
		return NULL;
	}
	*frame_count = 0;
	img2_frames_t fr = { 0 };
	return test_frames_take(fn(data, len, NULL, &fr), &fr, frame_count);
}

/* img2_decode_yuv with opts.yuv = params */
static inline img2_image_t **test_decode_yuv(const uint8_t *data, size_t len, const img2_yuv_params_t *params, int *frame_count)
{
	if (frame_count == NULL) {
		return NULL;
	}
	*frame_count = 0;
	img2_decode_opts_t opts = { .force_mime = IMG2_MIME_UNKNOWN, .yuv = params, .max_frames = 0, .jpeg_deblock = -1 };
	img2_frames_t fr = { 0 };
	return test_frames_take(img2_decode_yuv(data, len, params != NULL ? &opts : NULL, &fr), &fr, frame_count);
}

/* img2_decoder_dispatch */
static inline img2_image_t **test_dispatch(const img2_decode_opts_t *opts, const uint8_t *data, size_t len, img2_mime_t mime, int *frame_count)
{
	if (frame_count == NULL) {
		return NULL;
	}
	*frame_count = 0;
	img2_frames_t fr = { 0 };
	return test_frames_take(img2_decoder_dispatch(opts, data, len, mime, &fr), &fr, frame_count);
}

#endif /* IMGCAT2_TESTS_DECODER_INTERNAL_H */
