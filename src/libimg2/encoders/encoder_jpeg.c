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
#include <jerror.h>
/* clang-format on */

#include "encoder.h"
#include "../core/log.h"
#include "../core/log_jpeg.h"

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
 * return control to img2_encode_jpeg() instead of calling exit().
 */
static void jpeg_error_exit(j_common_ptr cinfo)
{
	struct jpeg_error_mgr_extended *err = (struct jpeg_error_mgr_extended *)cinfo->err;

	/* Print error message */
	char buffer[JMSG_LENGTH_MAX];
	(*cinfo->err->format_message)(cinfo, buffer);
	img2_log(IMG2_LOG_ERROR, "Error: libjpeg error: %s", buffer);

	/* Jump back to img2_encode_jpeg() */
	longjmp(err->setjmp_buffer, 1);
}

/**
 * @brief Growable memory destination owned by img2_encode_jpeg()
 *
 * Not jpeg_mem_dest(): after a libjpeg error its current buffer is only reachable through libjpeg's private
 * destination struct, so it could not be freed. This one keeps the buffer where the caller can always free it
 * (a failed growth leaves the old buffer in place).
 */
typedef struct {
	struct jpeg_destination_mgr pub; /**< libjpeg destination interface (first member) */
	uint8_t *buffer; /**< output buffer (malloc'ed by the caller) */
	size_t capacity; /**< allocated size of buffer */
} jpeg_memory_dest;

static void jpeg_memory_init(j_compress_ptr cinfo)
{
	jpeg_memory_dest *dest = (jpeg_memory_dest *)cinfo->dest;
	dest->pub.next_output_byte = dest->buffer;
	dest->pub.free_in_buffer = dest->capacity;
}

/** @brief Buffer full: double it (libjpeg error, i.e. longjmp, if that fails) */
static boolean jpeg_memory_grow(j_compress_ptr cinfo)
{
	jpeg_memory_dest *dest = (jpeg_memory_dest *)cinfo->dest;
	size_t new_capacity = dest->capacity * 2;
	uint8_t *new_buffer = (uint8_t *)realloc(dest->buffer, new_capacity);
	if (new_buffer == NULL) {
		ERREXIT1(cinfo, JERR_OUT_OF_MEMORY, 0);
	}
	dest->pub.next_output_byte = new_buffer + dest->capacity;
	dest->pub.free_in_buffer = new_capacity - dest->capacity;
	dest->buffer = new_buffer;
	dest->capacity = new_capacity;
	return TRUE;
}

static void jpeg_memory_term(j_compress_ptr cinfo)
{
	(void)cinfo; /* the data size is capacity - free_in_buffer */
}

/**
 * @brief Run libjpeg over the image into dest
 *
 * The only function with a setjmp(): everything that must survive a libjpeg error (compressor state, error
 * manager, destination buffer, row buffer) is owned by the caller, because automatic objects changed between
 * setjmp() and longjmp() are indeterminate afterwards.
 *
 * @return IMG2_OK, or IMG2_E_CODEC (libjpeg error, including a failed allocation inside libjpeg or a failed
 *         destination growth)
 */
static img2_status_t encode_jpeg_rows(const img2_image_t *img, int quality, const img2_encode_opts_t *o, struct jpeg_compress_struct *cinfo,
                                        struct jpeg_error_mgr_extended *jerr, jpeg_memory_dest *dest, uint8_t *row_buffer)
{
	/* Setup error handler with longjmp */
	cinfo->err = jpeg_std_error(&jerr->pub);
	jerr->pub.error_exit = jpeg_error_exit;
	jerr->pub.output_message = img2_jpeg_output_message; /* warnings to img2_log, never stderr */

	if (setjmp(jerr->setjmp_buffer)) {
		/* longjmp returns here if error occurs */
		jpeg_destroy_compress(cinfo);
		return IMG2_E_CODEC;
	}

	/* Create compressor */
	jpeg_create_compress(cinfo);

	/* Setup memory destination */
	dest->pub.init_destination = jpeg_memory_init;
	dest->pub.empty_output_buffer = jpeg_memory_grow;
	dest->pub.term_destination = jpeg_memory_term;
	cinfo->dest = &dest->pub;

	/* Set image parameters */
	cinfo->image_width = img->width;
	cinfo->image_height = img->height;
	cinfo->input_components = 3; /* RGB */
	cinfo->in_color_space = JCS_RGB;

	/* Set default compression parameters and quality */
	jpeg_set_defaults(cinfo);
	jpeg_set_quality(cinfo, quality, TRUE);

	/* Options (defaults: no Huffman optimisation, 4:2:0, baseline) */
	if (o->effort >= 6) {
		cinfo->optimize_coding = TRUE;
	}
	if (o->chroma != IMG2_CHROMA_DEFAULT) {
		/* luma sampling factors; Cb/Cr stay 1x1 (jpeg_set_defaults gives luma 2x2 = 4:2:0) */
		cinfo->comp_info[0].h_samp_factor = (o->chroma == IMG2_CHROMA_444) ? 1 : 2;
		cinfo->comp_info[0].v_samp_factor = (o->chroma == IMG2_CHROMA_420) ? 2 : 1;
	}
	if (o->progressive) {
		jpeg_simple_progression(cinfo);
	}

	/* Start compression */
	jpeg_start_compress(cinfo, TRUE);

	/* Write scanlines */
	JSAMPROW row_pointer[1];
	row_pointer[0] = row_buffer;

	while (cinfo->next_scanline < cinfo->image_height) {
		uint32_t y = cinfo->next_scanline;

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
		if (jpeg_write_scanlines(cinfo, row_pointer, 1) != 1) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to write JPEG scanline %u", y);
			jpeg_destroy_compress(cinfo);
			return IMG2_E_CODEC;
		}
	}

	/* Finish compression */
	jpeg_finish_compress(cinfo);

	/* Cleanup compressor */
	jpeg_destroy_compress(cinfo);
	return IMG2_OK;
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
img2_status_t img2_encode_jpeg(const img2_image_t *img, int quality, uint8_t **out_data, size_t *out_size)
{
	return img2_encode_jpeg_opts(img, quality, NULL, out_data, out_size);
}

/**
 * @brief img2_encode_jpeg() with encode options (NULL = defaults)
 *
 * effort >= 6: optimised Huffman tables; chroma: luma sampling factors; progressive: jpeg_simple_progression().
 * lossless, alpha_quality and threads have no JPEG equivalent (one WARN each).
 */
img2_status_t img2_encode_jpeg_opts(const img2_image_t *img, int quality, const img2_encode_opts_t *opts, uint8_t **out_data, size_t *out_size)
{
	if (img == NULL || out_data == NULL || out_size == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to encode_jpeg");
		return IMG2_E_ARG;
	}

	/* Initialize outputs */
	*out_data = NULL;
	*out_size = 0;

	const img2_encode_opts_t *o = img2_encode_opts_or_default(opts);
	if (o->lossless) {
		img2_encode_warn_ignored("lossless", "JPEG", "always lossy");
	}
	if (o->alpha_quality > 0) {
		img2_encode_warn_ignored("alpha_quality", "JPEG", "no alpha channel");
	}
	if (o->threads > 0) {
		img2_encode_warn_ignored("threads", "JPEG", "single-threaded encoder");
	}

	/* Clamp quality to valid range [0, 100] */
	int jpeg_quality = quality;
	if (jpeg_quality < 0) {
		jpeg_quality = 0;
	}
	if (jpeg_quality > 100) {
		jpeg_quality = 100;
	}

	/* Output buffer (grown by libjpeg through jpeg_memory_grow) and temporary RGB row buffer (RGBA->RGB) */
	jpeg_memory_dest dest;
	memset(&dest, 0, sizeof(dest));
	dest.capacity = 4096;
	dest.buffer = (uint8_t *)malloc(dest.capacity);
	size_t row_stride = (size_t)img->width * 3; /* RGB: 3 bytes per pixel */
	uint8_t *row_buffer = (uint8_t *)malloc(row_stride);
	if (dest.buffer == NULL || row_buffer == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate JPEG buffers");
		free(dest.buffer);
		free(row_buffer);
		return IMG2_E_NOMEM;
	}

	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr_extended jerr;
	img2_status_t st = encode_jpeg_rows(img, jpeg_quality, o, &cinfo, &jerr, &dest, row_buffer);
	free(row_buffer);
	if (st != IMG2_OK) {
		free(dest.buffer);
		return st;
	}

	/* Allocate and copy output data */
	size_t jpeg_size = dest.capacity - dest.pub.free_in_buffer;
	if (jpeg_size == 0) {
		img2_log(IMG2_LOG_ERROR, "Error: JPEG encoder produced no output");
		free(dest.buffer);
		return IMG2_E_CODEC;
	}

	*out_data = (uint8_t *)malloc(jpeg_size);
	if (*out_data == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate output buffer (%zu bytes)", jpeg_size);
		free(dest.buffer);
		return IMG2_E_NOMEM;
	}

	memcpy(*out_data, dest.buffer, jpeg_size);
	*out_size = jpeg_size;

	/* Free the destination buffer */
	free(dest.buffer);

	return IMG2_OK;
}
