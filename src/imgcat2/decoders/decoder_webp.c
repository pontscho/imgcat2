/**
 * @file decoder_webp.c
 * @brief WebP decoder implementation using libwebp
 *
 * Decodes WebP images (both static and animated) to RGBA8888 format.
 * Handles transparency and animation frame composition.
 */

/* clang-format off */
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

#ifdef HAVE_EXIF_READER
#include "../metadata/exif_reader.h"
#endif

/** Maximum number of WebP frames to decode (prevents DoS) */
#define MAX_WEBP_FRAMES 200

#ifdef HAVE_EXIF_READER
/**
 * @brief Extract EXIF metadata from WebP using WebPDemux
 *
 * Uses WebPDemux to access the EXIF chunk and parse it as TIFF.
 * WebP stores EXIF data in raw TIFF format in the EXIF chunk.
 *
 * @param data Raw WebP file data
 * @param len Length of data in bytes
 * @return Pointer to exif_info_t structure, or NULL if no EXIF found or on error
 */
static exif_info_t *extract_webp_exif(const uint8_t *data, size_t len)
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
			/* Allocate and initialize exif_info_t */
			exif_info_t *exif = malloc(sizeof(exif_info_t));
			if (exif == NULL) {
				WebPDemuxReleaseChunkIterator(&chunk_iter);
				WebPDemuxDelete(demux);
				return NULL;
			}

			exif_info_init(exif);

			/* Parse EXIF data (raw TIFF format) */
			if (parse_exif_from_tiff(exif, chunk_iter.chunk.bytes, chunk_iter.chunk.size) != 0) {
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
 * @return Pointer to xmp_info_t structure, or NULL if no XMP found or on error
 */
static xmp_info_t *extract_webp_xmp(const uint8_t *data, size_t len)
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
			/* Allocate and initialize xmp_info_t */
			xmp_info_t *xmp = malloc(sizeof(xmp_info_t));
			if (xmp == NULL) {
				WebPDemuxReleaseChunkIterator(&chunk_iter);
				WebPDemuxDelete(demux);
				return NULL;
			}

			xmp_info_init(xmp);

			/* Parse XMP data (raw XML string) */
			if (parse_xmp_from_xml(xmp, (const char *)chunk_iter.chunk.bytes, chunk_iter.chunk.size) != 0) {
				/* Parse failed */
				xmp_info_free(xmp);
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
#endif /* HAVE_EXIF_READER */

/**
 * @brief Decode static WebP image (single frame)
 *
 * Decodes a static WebP image to RGBA8888 format.
 * For animated WebP images, use decode_webp_animated().
 *
 * @param data Raw WebP file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (single frame)
 * @return Array with single image_t*, or NULL on error
 *
 * @note Output format is RGBA8888
 */
static image_t **decode_webp_static(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_webp_static\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Decode WebP to RGBA
	int width = 0;
	int height = 0;
	uint8_t *pixels = WebPDecodeRGBA(data, len, &width, &height);
	if (pixels == NULL) {
		fprintf(stderr, "Error: Failed to decode WebP image\n");
		return NULL;
	}

	// Create image_t structure
	image_t *img = image_create((uint32_t)width, (uint32_t)height);
	if (img == NULL) {
		fprintf(stderr, "Error: Failed to create image_t structure\n");
		WebPFree(pixels);
		return NULL;
	}

	// Copy pixels to image_t (WebP returns RGBA8888, same as our format)
	size_t pixel_size = (size_t)width * (size_t)height * 4;
	memcpy(img->pixels, pixels, pixel_size);

	// Free WebP decoder buffer
	WebPFree(pixels);

	// Parse EXIF/XMP metadata
#ifdef HAVE_EXIF_READER
	// Extract EXIF from EXIF chunk using WebPDemux
	exif_info_t *exif = extract_webp_exif(data, len);

	// Extract XMP from XMP chunk using WebPDemux
	xmp_info_t *xmp = extract_webp_xmp(data, len);

	// Store metadata in image structure
	img->exif = exif;
	img->xmp = xmp;
#endif

	// Allocate frames array (single frame)
	image_t **frames = (image_t **)malloc(sizeof(image_t *));
	if (frames == NULL) {
		fprintf(stderr, "Error: Failed to allocate frames array\n");
		image_destroy(img);
		return NULL;
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
bool webp_is_animated(const uint8_t *data, size_t len)
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
 * @return Array of image_t* frames, or NULL on error
 *
 * @note Maximum MAX_WEBP_FRAMES frames (200) to prevent DoS
 * @note WebP decoder returns fully composited frames automatically
 * @note Output format is RGBA8888
 */
static image_t **decode_webp_animated(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_webp_animated\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Setup WebPData
	WebPData webp_data;
	webp_data.bytes = data;
	webp_data.size = len;

	// Initialize decoder options
	WebPAnimDecoderOptions dec_options;
	if (!WebPAnimDecoderOptionsInit(&dec_options)) {
		fprintf(stderr, "Error: Failed to initialize WebP decoder options\n");
		return NULL;
	}

	dec_options.color_mode = MODE_RGBA;
	dec_options.use_threads = 1;

	// Create decoder
	WebPAnimDecoder *dec = WebPAnimDecoderNew(&webp_data, &dec_options);
	if (dec == NULL) {
		fprintf(stderr, "Error: Failed to create WebP animation decoder\n");
		return NULL;
	}

	// Get animation info
	WebPAnimInfo anim_info;
	if (!WebPAnimDecoderGetInfo(dec, &anim_info)) {
		fprintf(stderr, "Error: Failed to get WebP animation info\n");
		WebPAnimDecoderDelete(dec);
		return NULL;
	}

	// Check frame count
	int num_frames = (int)anim_info.frame_count;
	if (num_frames == 0) {
		fprintf(stderr, "Error: WebP animation has no frames\n");
		WebPAnimDecoderDelete(dec);
		return NULL;
	}

	if (num_frames > MAX_WEBP_FRAMES) {
		fprintf(stderr, "Warning: WebP has %d frames, limiting to %d\n", num_frames, MAX_WEBP_FRAMES);
		num_frames = MAX_WEBP_FRAMES;
	}

	// Get canvas dimensions
	uint32_t canvas_width = anim_info.canvas_width;
	uint32_t canvas_height = anim_info.canvas_height;

	// Allocate frames array
	image_t **frames = (image_t **)malloc(sizeof(image_t *) * num_frames);
	if (frames == NULL) {
		fprintf(stderr, "Error: Failed to allocate frames array\n");
		WebPAnimDecoderDelete(dec);
		return NULL;
	}

	// Initialize frames to NULL for cleanup
	for (int i = 0; i < num_frames; i++) {
		frames[i] = NULL;
	}

	// Decode each frame
	int frame_idx = 0;
	while (WebPAnimDecoderHasMoreFrames(dec) && frame_idx < num_frames) {
		uint8_t *frame_buf;
		int timestamp;

		if (!WebPAnimDecoderGetNext(dec, &frame_buf, &timestamp)) {
			fprintf(stderr, "Error: Failed to decode WebP frame %d\n", frame_idx);
			goto cleanup_error;
		}

		// Create output frame
		frames[frame_idx] = image_create(canvas_width, canvas_height);
		if (frames[frame_idx] == NULL) {
			fprintf(stderr, "Error: Failed to create output frame %d\n", frame_idx);
			goto cleanup_error;
		}

		// Copy frame buffer to image_t
		// WebP decoder returns fully composited RGBA frames
		size_t pixel_size = (size_t)canvas_width * (size_t)canvas_height * 4;
		memcpy(frames[frame_idx]->pixels, frame_buf, pixel_size);

		frame_idx++;
	}

	// Cleanup
	WebPAnimDecoderDelete(dec);

	*frame_count = frame_idx;

	return frames;

cleanup_error:
	// Cleanup on error
	for (int i = 0; i < num_frames; i++) {
		if (frames[i] != NULL) {
			image_destroy(frames[i]);
		}
	}
	free(frames);
	WebPAnimDecoderDelete(dec);
	return NULL;
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
 * @return Array of image_t* frames, or NULL on error
 *
 * @note Caller must free returned array with decoder_free_frames()
 * @note For static images, frame_count = 1
 * @note For animated images, frame_count = N (max MAX_WEBP_FRAMES)
 * @note Output format is RGBA8888
 */
image_t **decode_webp(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_webp\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Check if animated
	if (webp_is_animated(data, len)) {
		return decode_webp_animated(data, len, frame_count);
	}

	return decode_webp_static(data, len, frame_count);
}
