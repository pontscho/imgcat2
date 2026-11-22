/**
 * @file fuzz_decode_forced.c
 * @brief libFuzzer target: img2_decode() with a forced type (img2_decode_opts_t.force_mime), so every decoder
 *        also sees data that does not carry its magic bytes
 *
 * Input layout: byte 0 selects the type (index into k_mimes), byte 1 the options
 * (bits 0-1: max_frames 0..3, bit 2: jpeg_deblock default (quantsmooth) instead of off), the rest is the image.
 */

#include "fuzz_common.h"

static const img2_mime_t k_mimes[] = {
	IMG2_MIME_PNG, IMG2_MIME_JPEG, IMG2_MIME_GIF, IMG2_MIME_BMP, IMG2_MIME_TGA, IMG2_MIME_PSD,
	IMG2_MIME_HDR, IMG2_MIME_PNM, IMG2_MIME_WEBP, IMG2_MIME_HEIF, IMG2_MIME_TIFF, IMG2_MIME_RAW,
	IMG2_MIME_QOI, IMG2_MIME_ICO, IMG2_MIME_CUR, IMG2_MIME_JXL, IMG2_MIME_SVG, IMG2_MIME_AVIF,
};

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void)argc;
	(void)argv;
	fuzz_setup_logging();
	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	img2_decode_opts_t opts = IMG2_DECODE_OPTS_INIT;
	img2_frames_t frames;

	if (size < 2) {
		return 0;
	}
	opts.force_mime = k_mimes[data[0] % (sizeof(k_mimes) / sizeof(k_mimes[0]))];
	opts.max_frames = data[1] & 3;
	opts.jpeg_deblock = (data[1] & 4) ? -1 : 0;
	if (img2_decode(data + 2, size - 2, &opts, &frames) == IMG2_OK) {
		if (opts.max_frames > 0 && frames.count > opts.max_frames) {
			abort();
		}
		fuzz_touch_frames(&frames);
		fuzz_post_decode(&frames);
		img2_frames_free(&frames);
	} else if (frames.frames != NULL || frames.count != 0) {
		abort();
	}
	return 0;
}
