/**
 * @file decoder_webp.c
 * @brief WebP decoder implementation using libwebp
 *
 * Decodes WebP images (both static and animated) to RGBA8888 format.
 * Handles transparency and animation frame composition.
 */

/* clang-format off */
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <webp/decode.h>
#include <webp/demux.h>
#include <webp/mux.h>
/* clang-format on */

#include "decoder.h"
#include "../core/log.h"

#ifdef IMG2_HAVE_EXIF_READER
#include "../metadata/exif_reader.h"
#endif

/** Maximum number of WebP frames to decode (prevents DoS) */
#define MAX_WEBP_FRAMES 200

#ifdef IMG2_HAVE_EXIF_READER
/**
 * @brief Extract EXIF metadata from WebP using WebPDemux
 *
 * Uses WebPDemux to access the EXIF chunk and parse it as TIFF.
 * WebP stores EXIF data in raw TIFF format in the EXIF chunk.
 *
 * @param data Raw WebP file data
 * @param len Length of data in bytes
 * @return Pointer to img2_exif_t structure, or NULL if no EXIF found or on error
 */
static img2_exif_t *extract_webp_exif(const uint8_t *data, size_t len)
{
	if (data == NULL || len == 0) {
		return NULL;
	}

	/* Setup WebPData */
	WebPData webp_data;
	webp_data.bytes = data;
	webp_data.size = len;

	/* Create demuxer */
	WebPDemuxer *demux = WebPDemux(&webp_data);
	if (demux == NULL) {
		return NULL;
	}

	/* Try to get EXIF chunk */
	WebPChunkIterator chunk_iter;
	if (WebPDemuxGetChunk(demux, "EXIF", 1, &chunk_iter)) {
		/* EXIF chunk found */
		if (chunk_iter.chunk.bytes != NULL && chunk_iter.chunk.size > 0) {
			/* Allocate and initialize img2_exif_t */
			img2_exif_t *exif = malloc(sizeof(img2_exif_t));
			if (exif == NULL) {
				WebPDemuxReleaseChunkIterator(&chunk_iter);
				WebPDemuxDelete(demux);
				return NULL;
			}

			img2_exif_init(exif);

			/* Handle WebP EXIF chunk format:
			 * Some encoders prepend "Exif\0\0" (6 bytes) before TIFF data,
			 * following JPEG convention. We need to skip this prefix to find
			 * the actual TIFF data starting with "II" or "MM" magic.
			 */
			const uint8_t *tiff_data = chunk_iter.chunk.bytes;
			size_t tiff_size = chunk_iter.chunk.size;

			/* Check if chunk starts with "Exif\0\0" prefix */
			if (tiff_size >= 6 && memcmp(tiff_data, "Exif\0\0", 6) == 0) {
				/* Skip "Exif\0\0" prefix (6 bytes) */
				tiff_data += 6;
				tiff_size -= 6;
			}

			/* Verify we have enough data for TIFF header (minimum 8 bytes) */
			if (tiff_size < 8) {
				free(exif);
				WebPDemuxReleaseChunkIterator(&chunk_iter);
				WebPDemuxDelete(demux);
				return NULL;
			}

			/* Parse EXIF data (raw TIFF format) */
			if (img2_exif_parse_tiff(exif, tiff_data, tiff_size) != 0) {
				/* Parse failed */
				free(exif);
				exif = NULL;
			}

			WebPDemuxReleaseChunkIterator(&chunk_iter);
			WebPDemuxDelete(demux);
			return exif;
		}
		WebPDemuxReleaseChunkIterator(&chunk_iter);
	}

	/* No EXIF data found */
	WebPDemuxDelete(demux);
	return NULL;
}

/**
 * @brief Extract XMP metadata from WebP using WebPDemux
 *
 * Uses WebPDemux to access the XMP chunk and parse it as XML.
 * WebP stores XMP data in raw XML format in the XMP chunk.
 *
 * @param data Raw WebP file data
 * @param len Length of data in bytes
 * @return Pointer to img2_xmp_t structure, or NULL if no XMP found or on error
 */
static img2_xmp_t *extract_webp_xmp(const uint8_t *data, size_t len)
{
	if (data == NULL || len == 0) {
		return NULL;
	}

	/* Setup WebPData */
	WebPData webp_data;
	webp_data.bytes = data;
	webp_data.size = len;

	/* Create demuxer */
	WebPDemuxer *demux = WebPDemux(&webp_data);
	if (demux == NULL) {
		return NULL;
	}

	/* Try to get XMP chunk */
	WebPChunkIterator chunk_iter;
	if (WebPDemuxGetChunk(demux, "XMP ", 1, &chunk_iter)) {
		/* XMP chunk found */
		if (chunk_iter.chunk.bytes != NULL && chunk_iter.chunk.size > 0) {
			/* Allocate and initialize img2_xmp_t */
			img2_xmp_t *xmp = malloc(sizeof(img2_xmp_t));
			if (xmp == NULL) {
				WebPDemuxReleaseChunkIterator(&chunk_iter);
				WebPDemuxDelete(demux);
				return NULL;
			}

			img2_xmp_init(xmp);

			/* Parse XMP data (raw XML string) */
			if (img2_xmp_parse_xml(xmp, (const char *)chunk_iter.chunk.bytes, chunk_iter.chunk.size) != 0) {
				/* Parse failed */
				img2_xmp_clear(xmp);
				free(xmp);
				xmp = NULL;
			}

			WebPDemuxReleaseChunkIterator(&chunk_iter);
			WebPDemuxDelete(demux);
			return xmp;
		}
		WebPDemuxReleaseChunkIterator(&chunk_iter);
	}

	/* No XMP data found */
	WebPDemuxDelete(demux);
	return NULL;
}
#endif /* IMG2_HAVE_EXIF_READER */

/**
 * @brief Decode static WebP image (single frame)
 *
 * Decodes a static WebP image to RGBA8888 format.
 * For animated WebP images, use decode_webp_animated().
 *
 * @param data Raw WebP file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (single frame)
 * @return Array with single img2_image_t*, or NULL on error
 *
 * @note Output format is RGBA8888
 */
static img2_image_t **decode_webp_static(const uint8_t *data, size_t len, int *frame_count, img2_status_t *st)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_webp_static");
		return img2_decode_fail(st, IMG2_E_ARG);
	}

	// Initialize output
	*frame_count = 0;

	// Decode WebP to RGBA
	int width = 0;
	int height = 0;
	uint8_t *pixels = WebPDecodeRGBA(data, len, &width, &height);
	if (pixels == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to decode WebP image");
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Create img2_image_t structure
	img2_image_t *img = NULL;
	img2_status_t alloc_st = img2_decode_image_alloc((uint32_t)width, (uint32_t)height, &img);
	if (alloc_st != IMG2_OK) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create image_t structure");
		WebPFree(pixels);
		return img2_decode_fail(st, alloc_st);
	}

	// Copy pixels to img2_image_t (WebP returns RGBA8888, same as our format)
	size_t pixel_size = (size_t)width * (size_t)height * 4;
	memcpy(img->pixels, pixels, pixel_size);

	// Free WebP decoder buffer
	WebPFree(pixels);

	// Parse EXIF/XMP metadata
#ifdef IMG2_HAVE_EXIF_READER
	// Extract EXIF from EXIF chunk using WebPDemux
	img2_exif_t *exif = extract_webp_exif(data, len);

	// Extract XMP from XMP chunk using WebPDemux
	img2_xmp_t *xmp = extract_webp_xmp(data, len);

	// Store metadata in image structure
	img->exif = exif;
	img->xmp = xmp;
#endif

	// Allocate frames array (single frame)
	img2_image_t **frames = (img2_image_t **)malloc(sizeof(img2_image_t *));
	if (frames == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate frames array");
		img2_image_destroy(img);
		return img2_decode_fail(st, IMG2_E_NOMEM);
	}

	frames[0] = img;
	*frame_count = 1;

	return frames;
}

/**
 * @brief Check if WebP is animated (has multiple frames)
 *
 * Quickly checks if a WebP file contains multiple frames without
 * fully decoding the image data. Useful for determining whether
 * to use static or animated decoder.
 *
 * @param data Raw WebP file data
 * @param len Size of data in bytes
 * @return true if WebP has animation, false otherwise or on error
 *
 * @note Returns false if data is invalid or not a WebP
 * @note Does not validate that frames are decodable, only checks for animation flag
 */
bool img2_webp_is_animated(const uint8_t *data, size_t len)
{
	if (data == NULL || len == 0) {
		return false;
	}

	// Get WebP features
	WebPBitstreamFeatures features;
	if (WebPGetFeatures(data, len, &features) != VP8_STATUS_OK) {
		return false;
	}

	// Check if animated
	return features.has_animation != 0;
}

/**
 * @brief Decode animated WebP with all frames
 *
 * Decodes all frames of an animated WebP with proper frame composition.
 * WebP's animation decoder automatically handles frame composition.
 *
 * @param data Raw WebP file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded
 * @return Array of img2_image_t* frames, or NULL on error
 *
 * @note Maximum MAX_WEBP_FRAMES frames (200) to prevent DoS
 * @note WebP decoder returns fully composited frames automatically
 * @note Output format is RGBA8888
 */
static img2_image_t **decode_webp_animated(const uint8_t *data, size_t len, int *frame_count, int *loop_count, img2_status_t *st)
{
	if (data == NULL || len == 0 || frame_count == NULL || loop_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_webp_animated");
		return img2_decode_fail(st, IMG2_E_ARG);
	}

	// Initialize output
	*frame_count = 0;
	*loop_count = 0;

	// Setup WebPData
	WebPData webp_data;
	webp_data.bytes = data;
	webp_data.size = len;

	// Initialize decoder options
	WebPAnimDecoderOptions dec_options;
	if (!WebPAnimDecoderOptionsInit(&dec_options)) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to initialize WebP decoder options");
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	dec_options.color_mode = MODE_RGBA;
	dec_options.use_threads = 1;

	// Create decoder
	WebPAnimDecoder *dec = WebPAnimDecoderNew(&webp_data, &dec_options);
	if (dec == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create WebP animation decoder");
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Get animation info
	WebPAnimInfo anim_info;
	if (!WebPAnimDecoderGetInfo(dec, &anim_info)) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to get WebP animation info");
		WebPAnimDecoderDelete(dec);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Check frame count
	int num_frames = (int)anim_info.frame_count;
	if (num_frames == 0) {
		img2_log(IMG2_LOG_ERROR, "Error: WebP animation has no frames");
		WebPAnimDecoderDelete(dec);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	if (num_frames > MAX_WEBP_FRAMES) {
		img2_log(IMG2_LOG_WARN, "Warning: WebP has %d frames, limiting to %d", num_frames, MAX_WEBP_FRAMES);
		num_frames = MAX_WEBP_FRAMES;
	}

	// Get canvas dimensions
	uint32_t canvas_width = anim_info.canvas_width;
	uint32_t canvas_height = anim_info.canvas_height;

	// [F14] every output frame is a full canvas: reject before allocating anything
	if (num_frames > 1 && img2_decode_check_total_pixels((uint64_t)num_frames, canvas_width, canvas_height) != IMG2_OK) {
		WebPAnimDecoderDelete(dec);
		return img2_decode_fail(st, IMG2_E_LIMIT);
	}

	// Allocate frames array
	img2_image_t **frames = (img2_image_t **)malloc(sizeof(img2_image_t *) * num_frames);
	if (frames == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate frames array");
		WebPAnimDecoderDelete(dec);
		return img2_decode_fail(st, IMG2_E_NOMEM);
	}

	// Initialize frames to NULL for cleanup
	for (int i = 0; i < num_frames; i++) {
		frames[i] = NULL;
	}

	// Decode each frame; timestamps are the end time of each frame in ms
	int frame_idx = 0;
	int prev_timestamp = 0;
	uint64_t total_pixels = 0;
	while (WebPAnimDecoderHasMoreFrames(dec) && frame_idx < num_frames) {
		uint8_t *frame_buf;
		int timestamp;

		if (!WebPAnimDecoderGetNext(dec, &frame_buf, &timestamp)) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to decode WebP frame %d", frame_idx);
			*st = IMG2_E_CODEC;
			goto cleanup_error;
		}

		// Create output frame ([F14] within the running pixel total)
		if (img2_decode_add_frame_pixels(&total_pixels, canvas_width, canvas_height) != IMG2_OK) {
			*st = IMG2_E_LIMIT;
			goto cleanup_error;
		}
		img2_status_t alloc_st = img2_decode_image_alloc(canvas_width, canvas_height, &frames[frame_idx]);
		if (alloc_st != IMG2_OK) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to create output frame %d", frame_idx);
			*st = alloc_st;
			goto cleanup_error;
		}

		// Copy frame buffer to img2_image_t
		// WebP decoder returns fully composited RGBA frames
		size_t pixel_size = (size_t)canvas_width * (size_t)canvas_height * 4;
		memcpy(frames[frame_idx]->pixels, frame_buf, pixel_size);
		frames[frame_idx]->delay_ms = timestamp > prev_timestamp ? (uint32_t)(timestamp - prev_timestamp) : 0;
		prev_timestamp = timestamp;

		frame_idx++;
	}

	// Cleanup
	WebPAnimDecoderDelete(dec);

	*frame_count = frame_idx;
	if (frame_idx > 1) {
		// WebPAnimInfo.loop_count: 0 = forever
		*loop_count = anim_info.loop_count > (uint32_t)INT_MAX ? INT_MAX : (int)anim_info.loop_count;
	} else if (frame_idx == 1) {
		frames[0]->delay_ms = 0; // single frame: not animated
	}

	return frames;

cleanup_error:
	// Cleanup on error
	for (int i = 0; i < num_frames; i++) {
		if (frames[i] != NULL) {
			img2_image_destroy(frames[i]);
		}
	}
	free(frames);
	WebPAnimDecoderDelete(dec);
	return NULL; /* *st set at the goto */
}

/**
 * @brief Decode WebP image (static or animated)
 *
 * Main entry point for WebP decoding. Automatically detects if the image
 * is animated and routes to the appropriate decoder function.
 *
 * @param data Raw WebP file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded (1 for static, N for animated)
 * @return Array of img2_image_t* frames, or NULL on error
 *
 * @note Caller must free returned array with img2_decoder_free_frames()
 * @note For static images, frame_count = 1
 * @note For animated images, frame_count = N (max MAX_WEBP_FRAMES)
 * @note Output format is RGBA8888
 */
static img2_image_t **decode_webp_frames(const uint8_t *data, size_t len, int *frame_count, int *loop_count, img2_status_t *st)
{
	if (data == NULL || len == 0 || frame_count == NULL || loop_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_webp");
		return img2_decode_fail(st, IMG2_E_ARG);
	}

	// Initialize output
	*frame_count = 0;
	*loop_count = 0;

	// Check if animated
	if (img2_webp_is_animated(data, len)) {
		return decode_webp_animated(data, len, frame_count, loop_count, st);
	}

	return decode_webp_static(data, len, frame_count, st);
}

img2_status_t img2_decode_webp(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out)
{
	(void)opts;
	if (out == NULL) {
		return IMG2_E_ARG;
	}
	img2_status_t st = IMG2_E_INTERNAL;
	int count = 0;
	int loop_count = 0;
	img2_image_t **frames = decode_webp_frames(data, len, &count, &loop_count, &st);
	st = img2_frames_adopt(out, frames, count, st);
	if (st == IMG2_OK) {
		out->loop_count = loop_count;
	}
	return st;
}
