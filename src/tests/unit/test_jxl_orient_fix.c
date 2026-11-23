/**
 * @file test_jxl_orient_fix.c
 * @brief JPEG XL orientation: libjxl orients the pixels by the codestream orientation, the CLI must not rotate again
 *
 * decoder_jxl.c never calls JxlDecoderSetKeepOrientation(), so libjxl returns the pixels already transformed by the
 * codestream (image header) orientation. pipeline_decode() treated JXL like JPEG and applied the EXIF box orientation
 * on top: a file with codestream orientation 6 and an Exif box saying 6 (as written by encoders that keep the camera
 * EXIF) was rotated twice. The JPEG XL codestream orientation is authoritative (the Exif box orientation is ignored by
 * conforming viewers), so the CLI applies nothing for JXL, like TIFF / HEIF / AVIF.
 *
 * Fixtures are encoded in-test with the vendored libjxl: 4 x 2 RGBA, red at (0,0), green at (3,0), blue elsewhere.
 * Orientation 6 (rotate 90 CW) gives 2 x 4 with red at (1,0).
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <img2/img2.h>
#include <jxl/encode.h>

#include "../../imgcat2/cli.h"
#include "../../imgcat2/pipeline.h"
#include "../ctest.h"

static void pattern(uint8_t px[4 * 2 * 4])
{
	for (int i = 0; i < 8; i++) {
		px[i * 4 + 0] = 0;
		px[i * 4 + 1] = 0;
		px[i * 4 + 2] = 255;
		px[i * 4 + 3] = 255;
	}
	px[0] = 255; /* (0,0) red */
	px[2] = 0;
	px[12] = 0; /* (3,0) green */
	px[13] = 255;
	px[14] = 0;
}

/* 4 x 2 lossless JXL with codestream orientation cs_orient; exif_orient > 0 adds an Exif box (IFD0: Orientation) */
static uint8_t *jxl_oriented(int cs_orient, int exif_orient, size_t *len)
{
	uint8_t px[4 * 2 * 4];
	pattern(px);
	JxlEncoder *enc = JxlEncoderCreate(NULL);
	if (enc == NULL) {
		return NULL;
	}
	if (exif_orient > 0 && JxlEncoderUseBoxes(enc) != JXL_ENC_SUCCESS) {
		JxlEncoderDestroy(enc);
		return NULL;
	}
	JxlBasicInfo bi;
	JxlEncoderInitBasicInfo(&bi);
	bi.xsize = 4;
	bi.ysize = 2;
	bi.bits_per_sample = 8;
	bi.num_color_channels = 3;
	bi.alpha_bits = 8;
	bi.num_extra_channels = 1;
	bi.uses_original_profile = JXL_TRUE;
	bi.orientation = (JxlOrientation)cs_orient;
	JxlColorEncoding ce;
	JxlColorEncodingSetToSRGB(&ce, JXL_FALSE);
	JxlEncoderFrameSettings *fs = NULL;
	uint8_t *out = NULL;
	if (JxlEncoderSetBasicInfo(enc, &bi) != JXL_ENC_SUCCESS || JxlEncoderSetColorEncoding(enc, &ce) != JXL_ENC_SUCCESS) {
		goto done;
	}
	if (exif_orient > 0) {
		/* 4-byte TIFF header offset (0), then "II*\0", IFD0 at 8 with one entry: Orientation SHORT */
		uint8_t box[4 + 26] = { 0, 0, 0, 0, 'I', 'I', 42, 0, 8, 0, 0, 0, 1, 0, 0x12, 0x01, 3, 0, 1, 0, 0, 0 };
		box[4 + 18] = (uint8_t)exif_orient;
		if (JxlEncoderAddBox(enc, "Exif", box, sizeof(box), JXL_FALSE) != JXL_ENC_SUCCESS) {
			goto done;
		}
		JxlEncoderCloseBoxes(enc);
	}
	fs = JxlEncoderFrameSettingsCreate(enc, NULL);
	JxlPixelFormat pf = { 4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0 };
	if (fs == NULL || JxlEncoderSetFrameLossless(fs, JXL_TRUE) != JXL_ENC_SUCCESS || JxlEncoderAddImageFrame(fs, &pf, px, sizeof(px)) != JXL_ENC_SUCCESS) {
		goto done;
	}
	JxlEncoderCloseInput(enc);
	out = malloc(65536);
	if (out != NULL) {
		uint8_t *next = out;
		size_t avail = 65536;
		if (JxlEncoderProcessOutput(enc, &next, &avail) != JXL_ENC_SUCCESS) {
			free(out);
			out = NULL;
		} else {
			*len = (size_t)(next - out);
		}
	}
done:
	JxlEncoderDestroy(enc);
	return out;
}

/* pipeline_decode() of the file; checks the frame is w x h with the red pixel at (rx, ry) */
static void check_cli(const uint8_t *data, size_t len, uint32_t w, uint32_t h, uint32_t rx, uint32_t ry)
{
	cli_options_t opts;
	memset(&opts, 0, sizeof(opts));
	opts.silent = true;
	img2_image_t **frames = NULL;
	int count = 0;
	ASSERT_EQUAL(0, pipeline_decode(&opts, data, len, &frames, &count));
	ASSERT_EQUAL(1, count);
	ASSERT_EQUAL(w, frames[0]->width);
	ASSERT_EQUAL(h, frames[0]->height);
	const uint8_t *p = img2_image_get_pixel(frames[0], rx, ry);
	ASSERT_NOT_NULL(p);
	ASSERT_EQUAL(255, p[0]);
	ASSERT_EQUAL(0, p[1]);
	ASSERT_EQUAL(0, p[2]);
	img2_frames_t fr = { .frames = frames, .count = count };
	img2_frames_free(&fr);
}

/* codestream 6 + Exif 6: libjxl rotates once, the CLI must not rotate again (was 4 x 2, red at (3,1)) */
CTEST(jxl_orient_fix, codestream_and_exif_rotated_once)
{
	size_t len = 0;
	uint8_t *jxl = jxl_oriented(6, 6, &len);
	ASSERT_NOT_NULL(jxl);
	check_cli(jxl, len, 2, 4, 1, 0);
	free(jxl);
}

/* codestream 6 without Exif, and codestream 6 + Exif 1 (what cjxl writes): one rotation, as before */
CTEST(jxl_orient_fix, codestream_only)
{
	size_t len = 0;
	uint8_t *jxl = jxl_oriented(6, 0, &len);
	ASSERT_NOT_NULL(jxl);
	check_cli(jxl, len, 2, 4, 1, 0);
	free(jxl);
	jxl = jxl_oriented(6, 1, &len);
	ASSERT_NOT_NULL(jxl);
	check_cli(jxl, len, 2, 4, 1, 0);
	free(jxl);
}

/* codestream 1 + Exif 6: the codestream orientation is authoritative, the Exif box orientation is ignored */
CTEST(jxl_orient_fix, exif_box_orientation_ignored)
{
	size_t len = 0;
	uint8_t *jxl = jxl_oriented(1, 6, &len);
	ASSERT_NOT_NULL(jxl);
	check_cli(jxl, len, 4, 2, 0, 0);

	/* the parsed EXIF still reports the file's value (--info unchanged) */
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, img2_decode(jxl, len, NULL, &fr));
	ASSERT_NOT_NULL(fr.frames[0]->exif);
	ASSERT_EQUAL(6, fr.frames[0]->exif->orientation);
	img2_frames_free(&fr);
	free(jxl);
}
