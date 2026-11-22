/**
 * @file fuzz_decode.c
 * @brief libFuzzer target: img2_decode() with content auto-detection (every built decoder), the
 *        img2_detect_mime() / img2_is_animated() probes, and the CLI's post-decode steps
 *        (EXIF orientation, scaling) on the first frame
 *
 * max_frames 3 trims long animations after decoding (the library still decodes up to its 200-frame cap,
 * bounded by IMG2_MAX_TOTAL_PIXELS); jpeg_deblock 0 keeps quantsmooth (vendored, slow) out of this target,
 * fuzz_decode_forced switches it on.
 */

#include "fuzz_common.h"

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

	img2_mime_t mime = img2_detect_mime(data, size);
	if (mime == IMG2_MIME_YUV) {
		abort(); /* never detected */
	}
	(void)img2_is_animated(data, size);

	opts.max_frames = 3;
	opts.jpeg_deblock = 0;
	if (img2_decode(data, size, &opts, &frames) == IMG2_OK) {
		if (frames.count > 3) {
			abort();
		}
		fuzz_touch_frames(&frames);
		fuzz_post_decode(&frames);
		img2_frames_free(&frames);
	} else if (frames.frames != NULL || frames.count != 0) {
		abort(); /* *out must be zeroed on error */
	}
	return 0;
}
