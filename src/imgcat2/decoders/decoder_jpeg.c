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
 * return control to decode_jpeg() instead of calling exit().
 */
static void jpeg_error_exit(j_common_ptr cinfo)
{
	struct jpeg_error_mgr_ext *err = (struct jpeg_error_mgr_ext *)cinfo->err;

	/* Print error message */
	char buffer[JMSG_LENGTH_MAX];
	(*cinfo->err->format_message)(cinfo, buffer);
	fprintf(stderr, "Error: libjpeg error: %s\n", buffer);

	/* Jump back to decode_jpeg() */
	longjmp(err->setjmp_buffer, 1);
}

/**
 * @brief Decode JPEG image using libjpeg-turbo
 *
 * Decodes baseline and progressive JPEG images to RGBA8888 format.
 * Converts RGB output from libjpeg to RGBA by adding alpha=255.
 *
 * @param data Raw JPEG file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (JPEG is static)
 * @return Array with single image_t*, or NULL on error
 *
 * @note JPEG does not support animation or alpha channel
 * @note Alpha channel is added (opaque, 255) during RGB→RGBA conversion
 */
image_t **decode_jpeg(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_jpeg\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Create JPEG decompressor
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr_ext jerr;

	// Setup error handler with longjmp
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = jpeg_error_exit;

	if (setjmp(jerr.setjmp_buffer)) {
		// longjmp returns here if error occurs
		jpeg_destroy_decompress(&cinfo);
		return NULL;
	}

	// Create decompressor
	jpeg_create_decompress(&cinfo);

	// Set memory source
	jpeg_mem_src(&cinfo, data, (unsigned long)len);

	// Read JPEG header
	if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
		fprintf(stderr, "Error: Failed to read JPEG header\n");
		jpeg_destroy_decompress(&cinfo);
		return NULL;
	}

	// Set output format to RGB (3 channels)
	cinfo.out_color_space = JCS_RGB;

	// Start decompression
	if (!jpeg_start_decompress(&cinfo)) {
		fprintf(stderr, "Error: Failed to start JPEG decompression\n");
		jpeg_destroy_decompress(&cinfo);
		return NULL;
	}

	// Get output dimensions
	uint32_t width = cinfo.output_width;
	uint32_t height = cinfo.output_height;
	int channels = cinfo.output_components; // Should be 3 for RGB

	if (channels != 3) {
		fprintf(stderr, "Error: Unexpected JPEG output channels: %d (expected 3)\n", channels);
		jpeg_destroy_decompress(&cinfo);
		return NULL;
	}

	// Create image_t structure for RGBA output
	image_t *img = image_create(width, height);
	if (img == NULL) {
		fprintf(stderr, "Error: Failed to create image_t structure\n");
		jpeg_destroy_decompress(&cinfo);
		return NULL;
	}

	// Allocate row buffer for RGB scanline (libjpeg outputs RGB)
	size_t row_stride = width * channels; // RGB: 3 bytes per pixel
	uint8_t *row_buffer = (uint8_t *)malloc(row_stride);
	if (row_buffer == NULL) {
		fprintf(stderr, "Error: Failed to allocate JPEG row buffer\n");
		image_destroy(img);
		jpeg_destroy_decompress(&cinfo);
		return NULL;
	}

	// Read scanlines and convert RGB to RGBA
	JSAMPROW row_pointer[1];
	row_pointer[0] = row_buffer;

	uint32_t y = 0;
	while (cinfo.output_scanline < cinfo.output_height) {
		// Read one scanline (RGB)
		if (jpeg_read_scanlines(&cinfo, row_pointer, 1) != 1) {
			fprintf(stderr, "Error: Failed to read JPEG scanline %u\n", y);
			free(row_buffer);
			image_destroy(img);
			jpeg_destroy_decompress(&cinfo);
			return NULL;
		}

		// Convert RGB to RGBA (add alpha=255)
		for (uint32_t x = 0; x < width; x++) {
			uint8_t r = row_buffer[x * 3 + 0];
			uint8_t g = row_buffer[x * 3 + 1];
			uint8_t b = row_buffer[x * 3 + 2];
			image_set_pixel(img, x, y, r, g, b, 255);
		}

		y++;
	}

	// Free row buffer
	free(row_buffer);

	// Finish decompression
	if (!jpeg_finish_decompress(&cinfo)) {
		fprintf(stderr, "Warning: jpeg_finish_decompress() returned false\n");
	}

	// Cleanup
	jpeg_destroy_decompress(&cinfo);

	// Allocate frames array (single frame for JPEG)
	image_t **frames = (image_t **)malloc(sizeof(image_t *));
	if (frames == NULL) {
		fprintf(stderr, "Error: Failed to allocate frames array\n");
		image_destroy(img);
		return NULL;
	}

	frames[0] = img;
	*frame_count = 1;

	// fprintf(stderr, "JPEG decoded: %ux%u, RGB→RGBA\n", width, height);

	return frames;
}
