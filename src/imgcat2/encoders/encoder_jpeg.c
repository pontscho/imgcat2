/**
 * @file encoder_jpeg.c
 * @brief JPEG encoder implementation using libjpeg-turbo
 *
 * Encodes RGBA8888 images to JPEG format using libjpeg-turbo.
 * Converts RGBA to RGB by stripping alpha channel (JPEG does not support transparency).
 */

/* clang-format off */
#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jpeglib.h>
/* clang-format on */

#include "../encoders/encoder.h"

/**
 * @struct jpeg_error_mgr_extended
 * @brief Extended JPEG error manager with longjmp support
 *
 * libjpeg uses setjmp/longjmp for error handling. We need to
 * store a jmp_buf to handle errors gracefully.
 */
struct jpeg_error_mgr_extended {
	struct jpeg_error_mgr pub; /**< Public jpeg error manager */
	jmp_buf setjmp_buffer; /**< longjmp buffer for error recovery */
};

/**
 * @brief Custom JPEG error handler
 *
 * Called by libjpeg when a fatal error occurs. Uses longjmp to
 * return control to encode_jpeg() instead of calling exit().
 */
static void jpeg_error_exit(j_common_ptr cinfo)
{
	struct jpeg_error_mgr_extended *err = (struct jpeg_error_mgr_extended *)cinfo->err;

	/* Print error message */
	char buffer[JMSG_LENGTH_MAX];
	(*cinfo->err->format_message)(cinfo, buffer);
	fprintf(stderr, "Error: libjpeg error: %s\n", buffer);

	/* Jump back to encode_jpeg() */
	longjmp(err->setjmp_buffer, 1);
}

/**
 * @brief Encode RGBA image to JPEG format
 *
 * Encodes RGBA8888 image to JPEG format with quality control.
 * Converts RGBA to RGB by stripping alpha channel (JPEG does not support transparency).
 *
 * @param img Input image in RGBA8888 format
 * @param quality JPEG quality level (0-100, higher = better quality, larger file)
 * @param out_data Output parameter for encoded data (caller must free)
 * @param out_size Output parameter for encoded data size in bytes
 * @return 0 on success, -1 on error
 *
 * @note JPEG does not support alpha channel, it will be stripped
 * @note Caller must free *out_data with free() when done
 */
int encode_jpeg(const image_t *img, int quality, uint8_t **out_data, size_t *out_size)
{
	if (img == NULL || out_data == NULL || out_size == NULL) {
		fprintf(stderr, "Error: Invalid parameters to encode_jpeg\n");
		return -1;
	}

	/* Initialize outputs */
	*out_data = NULL;
	*out_size = 0;

	/* Clamp quality to valid range [0, 100] */
	/* Use volatile to prevent clobbering by longjmp */
	volatile int jpeg_quality = quality;
	if (jpeg_quality < 0) {
		// fprintf(stderr, "Warning: JPEG quality %d < 0, clamping to 0\n", jpeg_quality);
		jpeg_quality = 0;
	}
	if (jpeg_quality > 100) {
		// fprintf(stderr, "Warning: JPEG quality %d > 100, clamping to 100\n", jpeg_quality);
		jpeg_quality = 100;
	}

	/* Create JPEG compressor */
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr_extended jerr;

	/* Setup error handler with longjmp */
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = jpeg_error_exit;

	if (setjmp(jerr.setjmp_buffer)) {
		/* longjmp returns here if error occurs */
		jpeg_destroy_compress(&cinfo);
		if (*out_data != NULL) {
			free(*out_data);
			*out_data = NULL;
		}
		*out_size = 0;
		return -1;
	}

	/* Create compressor */
	jpeg_create_compress(&cinfo);

	/* Setup memory destination */
	unsigned char *jpeg_buffer = NULL;
	unsigned long jpeg_size = 0;
	jpeg_mem_dest(&cinfo, &jpeg_buffer, &jpeg_size);

	/* Set image parameters */
	cinfo.image_width = img->width;
	cinfo.image_height = img->height;
	cinfo.input_components = 3; /* RGB */
	cinfo.in_color_space = JCS_RGB;

	/* Set default compression parameters */
	jpeg_set_defaults(&cinfo);

	/* Set quality */
	jpeg_set_quality(&cinfo, jpeg_quality, TRUE);

	/* Start compression */
	jpeg_start_compress(&cinfo, TRUE);

	/* Allocate temporary RGB row buffer (convert RGBA→RGB) */
	size_t row_stride = img->width * 3; /* RGB: 3 bytes per pixel */
	uint8_t *row_buffer = (uint8_t *)malloc(row_stride);
	if (row_buffer == NULL) {
		fprintf(stderr, "Error: Failed to allocate JPEG row buffer\n");
		jpeg_destroy_compress(&cinfo);
		return -1;
	}

	/* Write scanlines */
	JSAMPROW row_pointer[1];
	row_pointer[0] = row_buffer;

	while (cinfo.next_scanline < cinfo.image_height) {
		uint32_t y = cinfo.next_scanline;

		/* Convert RGBA to RGB (strip alpha channel) */
		for (uint32_t x = 0; x < img->width; x++) {
			uint32_t src_idx = (y * img->width + x) * 4; /* RGBA */
			uint32_t dst_idx = x * 3; /* RGB */

			row_buffer[dst_idx + 0] = img->pixels[src_idx + 0]; /* R */
			row_buffer[dst_idx + 1] = img->pixels[src_idx + 1]; /* G */
			row_buffer[dst_idx + 2] = img->pixels[src_idx + 2]; /* B */
			/* Alpha channel (src_idx + 3) is discarded */
		}

		/* Write scanline */
		if (jpeg_write_scanlines(&cinfo, row_pointer, 1) != 1) {
			fprintf(stderr, "Error: Failed to write JPEG scanline %u\n", y);
			free(row_buffer);
			jpeg_destroy_compress(&cinfo);
			return -1;
		}
	}

	/* Free row buffer */
	free(row_buffer);

	/* Finish compression */
	jpeg_finish_compress(&cinfo);

	/* Cleanup compressor */
	jpeg_destroy_compress(&cinfo);

	/* Allocate and copy output data */
	if (jpeg_buffer == NULL || jpeg_size == 0) {
		fprintf(stderr, "Error: JPEG encoder produced no output\n");
		return -1;
	}

	*out_data = (uint8_t *)malloc(jpeg_size);
	if (*out_data == NULL) {
		fprintf(stderr, "Error: Failed to allocate output buffer (%lu bytes)\n", jpeg_size);
		free(jpeg_buffer);
		return -1;
	}

	memcpy(*out_data, jpeg_buffer, jpeg_size);
	*out_size = jpeg_size;

	/* Free libjpeg's buffer */
	free(jpeg_buffer);

	return 0;
}
