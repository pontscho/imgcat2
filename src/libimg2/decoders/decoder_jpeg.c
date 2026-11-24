/**
 * @file decoder_jpeg.c
 * @brief JPEG decoder implementation using libjpeg-turbo
 *
 * Decodes JPEG images (baseline, progressive) to RGBA8888 format.
 * Uses libjpeg-turbo for high-performance JPEG decoding.
 */

/* clang-format off */
#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jpeglib.h>
/* clang-format on */

#include "decoder.h"
#include "../core/log.h"
#include "../core/log_jpeg.h"

#ifdef IMG2_HAVE_JPEGQS
#include "libjpegqs.h"
#endif

/* EXIF/XMP metadata support */
#ifdef IMG2_HAVE_EXIF_READER
#include "../metadata/exif_reader.h"
#include "../metadata/meta_raw.h"
#endif

/**
 * @struct jpeg_error_mgr_ext
 * @brief Extended JPEG error manager with longjmp support
 *
 * libjpeg uses setjmp/longjmp for error handling. We need to
 * store a jmp_buf to handle errors gracefully.
 */
struct jpeg_error_mgr_ext {
	struct jpeg_error_mgr pub; /**< Public jpeg error manager */
	jmp_buf setjmp_buffer; /**< longjmp buffer for error recovery */
};

/**
 * @brief Custom JPEG error handler
 *
 * Called by libjpeg when a fatal error occurs. Uses longjmp to
 * return control to img2_decode_jpeg() instead of calling exit().
 */
static void jpeg_error_exit(j_common_ptr cinfo)
{
	struct jpeg_error_mgr_ext *err = (struct jpeg_error_mgr_ext *)cinfo->err;

	/* Print error message */
	char buffer[JMSG_LENGTH_MAX];
	(*cinfo->err->format_message)(cinfo, buffer);
	img2_log(IMG2_LOG_ERROR, "Error: libjpeg error: %s", buffer);

	/* Jump back to img2_decode_jpeg() */
	longjmp(err->setjmp_buffer, 1);
}

/**
 * @brief Decode JPEG image using libjpeg-turbo
 *
 * Decodes baseline and progressive JPEG images to RGBA8888 format.
 * Converts RGB output from libjpeg to RGBA by adding alpha=255.
 * Also extracts EXIF/XMP metadata if available.
 *
 * @param data Raw JPEG file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (JPEG is static)
 * @param deblock -1 default (jpegqs if built), 0 plain libjpeg, 1 jpegqs if built
 * @param st Output: status on error
 * @return Array with single img2_image_t*, or NULL on error
 *
 * @note JPEG does not support animation or alpha channel
 * @note Alpha channel is added (opaque, 255) during RGB→RGBA conversion
 */
static img2_image_t **decode_jpeg_frames(const uint8_t *data, size_t len, int *frame_count, int deblock, img2_status_t *st)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_jpeg");
		return img2_decode_fail(st, IMG2_E_ARG);
	}

	// Initialize output
	*frame_count = 0;

#ifdef IMG2_HAVE_JPEGQS
	/* Per-call jpegqs settings (formerly a mutable global); threads = -1 keeps the OpenMP default,
	 * so OMP_NUM_THREADS is honoured */
	jpegqs_control_t qs = { .flags = JPEGQS_DIAGONALS | JPEGQS_JOINT_YUV | JPEGQS_UPSAMPLE_UV, .niter = 5, .threads = -1, .progprec = 0, .userdata = NULL, .progress = NULL };
	const bool use_qs = (deblock != 0);
#else
	(void)deblock;
#endif

	// Create JPEG decompressor
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr_ext jerr;

	// Setup error handler with longjmp
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = jpeg_error_exit;
	jerr.pub.output_message = img2_jpeg_output_message; /* warnings to img2_log, never stderr */

	// Allocated after setjmp and freed on the longjmp path (libjpeg errors in jpeg_read_scanlines, e.g. a
	// 12-bit file, or in jpeg_finish_decompress, e.g. a second scan): must be volatile
	img2_image_t *volatile img = NULL;
	uint8_t *volatile row_buffer = NULL;

	if (setjmp(jerr.setjmp_buffer)) {
		// longjmp returns here if error occurs
		free(row_buffer);
		img2_image_destroy(img);
		jpeg_destroy_decompress(&cinfo);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Create decompressor
	jpeg_create_decompress(&cinfo);

	// Set memory source
	jpeg_mem_src(&cinfo, data, (unsigned long)len);

	// Read JPEG header
	if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to read JPEG header");
		jpeg_destroy_decompress(&cinfo);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// [P33] Before jpeg(qs)_start_decompress: buffered image mode (jpeg-quantsmooth, progressive files) allocates
	// whole-image coefficient arrays there. The output size equals the header size (no scaling).
	img2_status_t pre_st = img2_decode_check_header_size("JPEG", cinfo.image_width, cinfo.image_height);
	if (pre_st != IMG2_OK) {
		jpeg_destroy_decompress(&cinfo);
		return img2_decode_fail(st, pre_st);
	}

	// Set output format to RGB (3 channels)
	cinfo.out_color_space = JCS_RGB;

	// Start decompression
#ifdef IMG2_HAVE_JPEGQS
	const boolean started = use_qs ? jpegqs_start_decompress(&cinfo, &qs) : jpeg_start_decompress(&cinfo);
#else
	const boolean started = jpeg_start_decompress(&cinfo);
#endif
	if (!started) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to start JPEG decompression");
		jpeg_destroy_decompress(&cinfo);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Get output dimensions
	uint32_t width = cinfo.output_width;
	uint32_t height = cinfo.output_height;
	int channels = cinfo.output_components; // Should be 3 for RGB

	if (channels != 3) {
		img2_log(IMG2_LOG_ERROR, "Error: Unexpected JPEG output channels: %d (expected 3)", channels);
		jpeg_destroy_decompress(&cinfo);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Create img2_image_t structure for RGBA output
	img2_image_t *new_img = NULL;
	img2_status_t alloc_st = img2_decode_image_alloc(width, height, &new_img);
	if (alloc_st != IMG2_OK) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create image_t structure");
		jpeg_destroy_decompress(&cinfo);
		return img2_decode_fail(st, alloc_st);
	}
	img = new_img;

	// Allocate row buffer for RGB scanline (libjpeg outputs RGB)
	size_t row_stride = width * channels; // RGB: 3 bytes per pixel
	row_buffer = (uint8_t *)malloc(row_stride);
	if (row_buffer == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate JPEG row buffer");
		img2_image_destroy(img);
		jpeg_destroy_decompress(&cinfo);
		return img2_decode_fail(st, IMG2_E_NOMEM);
	}

	// Read scanlines and convert RGB to RGBA
	JSAMPROW row_pointer[1];
	row_pointer[0] = row_buffer;

	uint32_t y = 0;
	while (cinfo.output_scanline < cinfo.output_height) {
		// Read one scanline (RGB)
		if (jpeg_read_scanlines(&cinfo, row_pointer, 1) != 1) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to read JPEG scanline %u", y);
			free(row_buffer);
			img2_image_destroy(img);
			jpeg_destroy_decompress(&cinfo);
			return img2_decode_fail(st, IMG2_E_CODEC);
		}

		// Convert RGB to RGBA (add alpha=255)
		for (uint32_t x = 0; x < width; x++) {
			uint8_t r = row_buffer[x * 3 + 0];
			uint8_t g = row_buffer[x * 3 + 1];
			uint8_t b = row_buffer[x * 3 + 2];
			img2_image_set_pixel(img, x, y, r, g, b, 255);
		}

		y++;
	}

	// Free row buffer
	free(row_buffer);
	row_buffer = NULL;

	// Finish decompression
#ifdef IMG2_HAVE_JPEGQS
	if (use_qs) {
		if (!jpegqs_finish_decompress(&cinfo)) {
			img2_log(IMG2_LOG_WARN, "Warning: jpegqs_finish_decompress() returned false");
		}
	} else if (!jpeg_finish_decompress(&cinfo)) {
		img2_log(IMG2_LOG_WARN, "Warning: jpeg_finish_decompress() returned false");
	}
#else
	if (!jpeg_finish_decompress(&cinfo)) {
		img2_log(IMG2_LOG_WARN, "Warning: jpeg_finish_decompress() returned false");
	}
#endif

	// Cleanup
	jpeg_destroy_decompress(&cinfo);

	// Parse EXIF/XMP metadata (Phase 3)
#ifdef IMG2_HAVE_EXIF_READER
	img2_exif_t *exif = malloc(sizeof(img2_exif_t));
	img2_xmp_t *xmp = malloc(sizeof(img2_xmp_t));

	if (exif && xmp) {
		img2_exif_init(exif);
		img2_xmp_init(xmp);

		// Try to parse EXIF data
		if (img2_exif_parse_jpeg(exif, data, len) != 0) {
			// No EXIF data or parse failed
			free(exif);
			exif = NULL;
		}

		// Try to parse XMP data
		if (img2_xmp_parse_jpeg(xmp, data, len) != 0) {
			// No XMP data or parse failed
			img2_xmp_clear(xmp);
			free(xmp);
			xmp = NULL;
		}

		// Store metadata in image structure
		img->exif = exif;
		img->xmp = xmp;

	} else {
		// Allocation failed - free what we have
		if (exif) {
			free(exif);
		}
		if (xmp) {
			free(xmp); /* never initialised here: img2_xmp_clear() would free garbage pointers */
		}
	}

	// Raw blocks for the encoders (APP1 "Exif\0\0" + TIFF, APP1 XMP packet) and the orientation to apply
	const uint8_t *raw = NULL;
	size_t raw_len = 0;
	if (img2_jpeg_find_app1(data, len, IMG2_JPEG_EXIF_ID, IMG2_JPEG_EXIF_ID_LEN, &raw, &raw_len) == IMG2_OK) {
		img2_meta_keep_exif(img, "JPEG", raw, raw_len);
	}
	if (img2_jpeg_find_app1(data, len, IMG2_JPEG_XMP_ID, IMG2_JPEG_XMP_ID_LEN, &raw, &raw_len) == IMG2_OK) {
		img2_meta_keep_xmp(img, "JPEG", raw, raw_len);
	}
	img->orientation = img2_meta_orientation(img->exif);
#endif

	// Allocate frames array (single frame for JPEG)
	img2_image_t **frames = (img2_image_t **)malloc(sizeof(img2_image_t *));
	if (frames == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate frames array");
		img2_image_destroy(img);
		return img2_decode_fail(st, IMG2_E_NOMEM);
	}

	frames[0] = img;
	*frame_count = 1;

	// fprintf(stderr, "JPEG decoded: %ux%u, RGB→RGBA\n", width, height);

	return frames;
}

/** @brief img2_probe_jpeg(): libjpeg fatal error, logged at DEBUG, back to the setjmp of img2_probe_jpeg() */
static void jpeg_probe_error_exit(j_common_ptr cinfo)
{
	struct jpeg_error_mgr_ext *err = (struct jpeg_error_mgr_ext *)cinfo->err;

	char buffer[JMSG_LENGTH_MAX];
	(*cinfo->err->format_message)(cinfo, buffer);
	img2_log(IMG2_LOG_DEBUG, "img2_probe: libjpeg error: %s", buffer);

	longjmp(err->setjmp_buffer, 1);
}

/** @brief img2_probe_jpeg(): libjpeg warning (e.g. extraneous bytes before a marker), logged at DEBUG */
static void jpeg_probe_output_message(j_common_ptr cinfo)
{
	char buffer[JMSG_LENGTH_MAX];
	(*cinfo->err->format_message)(cinfo, buffer);
	img2_log(IMG2_LOG_DEBUG, "img2_probe: libjpeg warning: %s", buffer);
}

img2_status_t img2_probe_jpeg(const uint8_t *data, size_t len, img2_probe_info_t *out)
{
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr_ext jerr;

	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = jpeg_probe_error_exit;
	jerr.pub.output_message = jpeg_probe_output_message;

	if (setjmp(jerr.setjmp_buffer)) {
		jpeg_destroy_decompress(&cinfo);
		return IMG2_E_CODEC;
	}

	jpeg_create_decompress(&cinfo);
	jpeg_mem_src(&cinfo, data, (unsigned long)len);

	// require_image on a memory source (never suspends): JPEG_HEADER_OK, or a libjpeg error (longjmp above)
	(void)jpeg_read_header(&cinfo, TRUE);

	// libjpeg rejected a zero size (get_sof) and a side over JPEG_MAX_DIMENSION, 65500 < IMG2_MAX_DIMENSION
	const uint32_t width = cinfo.image_width;
	const uint32_t height = cinfo.image_height;
	jpeg_destroy_decompress(&cinfo);

	// The decoder's pixel limit ([P33] header check, dispatcher), without its ERROR line
	size_t byte_count;
	if (!img2_image_calc_size(width, height, &byte_count)) {
		img2_log(IMG2_LOG_DEBUG, "img2_probe: JPEG dimensions %ux%u exceed the limits", width, height);
		return IMG2_E_LIMIT;
	}

	out->mime = IMG2_MIME_JPEG;
	out->width = width;
	out->height = height;
	out->orientation = 1;

	// The orientation the decoder leaves to apply (img->orientation)
#ifdef IMG2_HAVE_EXIF_READER
	img2_exif_t exif;
	img2_exif_init(&exif);
	if (img2_exif_parse_jpeg(&exif, data, len) == IMG2_OK) {
		out->orientation = img2_meta_orientation(&exif);
	}
	img2_exif_clear(&exif);
#endif

	return IMG2_OK;
}

img2_status_t img2_decode_jpeg(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out)
{
	if (out == NULL) {
		return IMG2_E_ARG;
	}
	img2_status_t st = IMG2_E_INTERNAL;
	int count = 0;
	img2_image_t **frames = decode_jpeg_frames(data, len, &count, opts != NULL ? opts->jpeg_deblock : -1, &st);
	return img2_frames_adopt(out, frames, count, st);
}
