/**
 * @file decoder_avif.c
 * @brief AVIF decoder implementation using libheif
 *
 * Decodes AVIF images (both static and image sequences) to RGBA8888 format.
 * AVIF uses AV1 codec for compression.
 */

/* clang-format off */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libheif/heif.h>
/* clang-format on */

#include "decoder.h"
#include "../core/log.h"

#ifdef IMG2_HAVE_EXIF_READER
#include "../metadata/exif_reader.h"
#include "../metadata/meta_raw.h"
#endif

/** Maximum number of AVIF frames to decode (prevents DoS) */
#define MAX_AVIF_FRAMES 200

/* The "is animated" probe and the list / sequence / single classification are shared with HEIF
 * (img2_heif_is_animated, img2_heif_layout in decoder_heif.c). */

/**
 * @brief Decode static AVIF image (single frame)
 *
 * Decodes a static AVIF image to RGBA8888 format.
 * For AVIF image sequences, use decode_avif_animated().
 *
 * @param data Raw AVIF file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (single frame)
 * @return Array with single img2_image_t*, or NULL on error
 *
 * @note Output format is RGBA8888
 */
static img2_image_t **decode_avif_static(const uint8_t *data, size_t len, int *frame_count, img2_status_t *st)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_avif_static");
		return img2_decode_fail(st, IMG2_E_ARG);
	}

	// Initialize output
	*frame_count = 0;

	// Create HEIF context (libheif handles AVIF)
	struct heif_context *ctx = heif_context_alloc();
	if (ctx == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate HEIF context for AVIF");
		return img2_decode_fail(st, IMG2_E_NOMEM);
	}

	// Read from memory
	struct heif_error err = heif_context_read_from_memory_without_copy(ctx, data, len, NULL);
	if (err.code != heif_error_Ok) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to read AVIF data: %s", err.message);
		heif_context_free(ctx);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Get primary image handle
	struct heif_image_handle *handle = NULL;
	err = heif_context_get_primary_image_handle(ctx, &handle);
	if (err.code != heif_error_Ok) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to get primary image handle: %s", err.message);
		heif_context_free(ctx);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// [P33] The handle's size (after transformations: the decoded size) before heif_decode_image allocates
	img2_status_t pre_st = img2_decode_check_header_size("AVIF", heif_image_handle_get_width(handle), heif_image_handle_get_height(handle));
	if (pre_st != IMG2_OK) {
		heif_image_handle_release(handle);
		heif_context_free(ctx);
		return img2_decode_fail(st, pre_st);
	}

	// Decode image to RGBA
	struct heif_image *img = NULL;
	err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, NULL);
	if (err.code != heif_error_Ok) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to decode AVIF image: %s", err.message);
		heif_image_handle_release(handle);
		heif_context_free(ctx);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Get image dimensions
	int width = heif_image_get_width(img, heif_channel_interleaved);
	int height = heif_image_get_height(img, heif_channel_interleaved);

	// Get RGBA plane
	int stride = 0;
	const uint8_t *plane = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);
	if (plane == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to get AVIF image plane");
		heif_image_release(img);
		heif_image_handle_release(handle);
		heif_context_free(ctx);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Create img2_image_t structure
	img2_image_t *output = NULL;
	img2_status_t alloc_st = img2_decode_image_alloc((uint32_t)width, (uint32_t)height, &output);
	if (alloc_st != IMG2_OK) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create image_t structure");
		heif_image_release(img);
		heif_image_handle_release(handle);
		heif_context_free(ctx);
		return img2_decode_fail(st, alloc_st);
	}

	// Copy pixels row-by-row (handle stride != width*4)
	for (int y = 0; y < height; y++) {
		const uint8_t *src_row = plane + y * stride;
		uint8_t *dst_row = output->pixels + y * width * 4;
		memcpy(dst_row, src_row, width * 4);
	}

	// Parse EXIF/XMP metadata (Phase 4)
#ifdef IMG2_HAVE_EXIF_READER
	img2_exif_t *exif = malloc(sizeof(img2_exif_t));
	img2_xmp_t *xmp = malloc(sizeof(img2_xmp_t));

	if (exif && xmp) {
		img2_exif_init(exif);
		img2_xmp_init(xmp);

		// Get list of metadata block IDs
		int num_metadata = heif_image_handle_get_number_of_metadata_blocks(handle, NULL);
		if (num_metadata > 0) {
			heif_item_id *metadata_ids = malloc(sizeof(heif_item_id) * num_metadata);
			if (metadata_ids != NULL) {
				heif_image_handle_get_list_of_metadata_block_IDs(handle, NULL, metadata_ids, num_metadata);

				// Iterate through metadata blocks
				for (int i = 0; i < num_metadata; i++) {
					const char *type = heif_image_handle_get_metadata_type(handle, metadata_ids[i]);
					if (type == NULL) {
						continue;
					}

					// Extract EXIF metadata
					if (strcmp(type, "Exif") == 0) {
						size_t exif_size = heif_image_handle_get_metadata_size(handle, metadata_ids[i]);
						if (exif_size > 0) {
							uint8_t *exif_data = malloc(exif_size);
							if (exif_data != NULL) {
								struct heif_error err = heif_image_handle_get_metadata(handle, metadata_ids[i], exif_data);
								if (err.code == heif_error_Ok) {
									// Handle HEIF EXIF format: 4-byte offset + "Exif\0\0" + TIFF data
									// The 4-byte offset indicates where TIFF data starts relative to "Exif"
									const uint8_t *tiff_data = exif_data;
									size_t tiff_size = exif_size;

									// Check if it starts with 4-byte offset + "Exif" marker
									if (exif_size > 10 && exif_data[4] == 'E' && exif_data[5] == 'x' && exif_data[6] == 'i' && exif_data[7] == 'f') {
										// Get offset from first 4 bytes (big-endian)
										uint32_t offset = ((uint32_t)exif_data[0] << 24) | ((uint32_t)exif_data[1] << 16) | ((uint32_t)exif_data[2] << 8) | (uint32_t)exif_data[3];

										// Skip 4-byte prefix + offset (usually "Exif\0\0" = 6 bytes)
										size_t skip = 4 + offset;
										if (skip < exif_size) {
											tiff_data = exif_data + skip;
											tiff_size = exif_size - skip;
										}
									} else {
										// Try legacy format: just 4-byte offset prefix
										if (exif_size > 4) {
											bool has_magic_at_0 = (exif_data[0] == 'I' && exif_data[1] == 'I') || (exif_data[0] == 'M' && exif_data[1] == 'M');
											if (!has_magic_at_0 && exif_size > 8) {
												bool has_magic_at_4 = (exif_data[4] == 'I' && exif_data[5] == 'I') || (exif_data[4] == 'M' && exif_data[5] == 'M');
												if (has_magic_at_4) {
													tiff_data = exif_data + 4;
													tiff_size = exif_size - 4;
												}
											}
										}
									}

									// Raw block for the encoders (the first Exif item in TIFF-header form)
									if (output->exif_raw == NULL) {
										img2_meta_keep_exif(output, "AVIF", tiff_data, tiff_size);
									}

									// Parse EXIF from TIFF data
									if (img2_exif_parse_tiff(exif, tiff_data, tiff_size) != 0) {
										free(exif);
										exif = NULL;
									}
								}
								free(exif_data);
							}
						}
					}

					// Extract XMP metadata
					if (strcmp(type, "mime") == 0) {
						const char *content_type = heif_image_handle_get_metadata_content_type(handle, metadata_ids[i]);
						if (content_type != NULL && strcmp(content_type, "application/rdf+xml") == 0) {
							size_t xmp_size = heif_image_handle_get_metadata_size(handle, metadata_ids[i]);
							if (xmp_size > 0) {
								char *xmp_data = malloc(xmp_size + 1);
								if (xmp_data != NULL) {
									struct heif_error err = heif_image_handle_get_metadata(handle, metadata_ids[i], xmp_data);
									if (err.code == heif_error_Ok) {
										xmp_data[xmp_size] = '\0'; // Null-terminate XML string
										if (output->xmp_raw == NULL) {
											img2_meta_keep_xmp(output, "AVIF", (const uint8_t *)xmp_data, xmp_size);
										}
										if (img2_xmp_parse_xml(xmp, xmp_data, xmp_size) != 0) {
											img2_xmp_clear(xmp);
											free(xmp);
											xmp = NULL;
										}
									}
									free(xmp_data);
								}
							}
						}
					}
				}

				free(metadata_ids);
			}
		}

		// Store metadata in image structure
		// Only keep exif if we actually found any data
		// Check multiple fields to determine if EXIF has useful data
		if (exif != NULL) {
			if (strlen(exif->make) > 0 || strlen(exif->model) > 0 || strlen(exif->software) > 0 || strlen(exif->description) > 0 || exif->has_iso || exif->has_gps || exif->has_exposure_time || exif->has_f_number || exif->has_focal_length || exif->orientation > 1) {
				output->exif = exif;
			} else {
				free(exif);
				output->exif = NULL;
			}
		} else {
			output->exif = NULL;
		}

		// Only keep xmp if we actually found any data
		if (xmp != NULL && (strlen(xmp->creator) > 0 || strlen(xmp->title) > 0)) {
			output->xmp = xmp;
		} else {
			img2_xmp_clear(xmp);
			free(xmp);
			output->xmp = NULL;
		}

	} else {
		// Allocation failed - free what we have (never initialised: no img2_xmp_clear)
		free(exif);
		free(xmp);
		output->exif = NULL;
		output->xmp = NULL;
	}
#endif

	// Cleanup HEIF resources
	heif_image_release(img);
	heif_image_handle_release(handle);
	heif_context_free(ctx);

	// Allocate frames array (single frame)
	img2_image_t **frames = (img2_image_t **)malloc(sizeof(img2_image_t *));
	if (frames == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate frames array");
		img2_image_destroy(output);
		return img2_decode_fail(st, IMG2_E_NOMEM);
	}

	frames[0] = output;
	*frame_count = 1;

	return frames;
}

/**
 * @brief [F14] Sum the sizes of the first count top-level images (heif_image_handle_get_width/height, i.e.
 *        after transformations) against IMG2_MAX_TOTAL_PIXELS without decoding them
 *
 * @return IMG2_OK, IMG2_E_LIMIT (total exceeded, logged), IMG2_E_CODEC (handle lookup failed, logged)
 */
static img2_status_t avif_check_total_pixels(struct heif_context *ctx, const heif_item_id *ids, int count)
{
	uint64_t total = 0;
	for (int i = 0; i < count; i++) {
		struct heif_image_handle *handle = NULL;
		struct heif_error err = heif_context_get_image_handle(ctx, ids[i], &handle);
		if (err.code != heif_error_Ok) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to get image handle %d: %s", i, err.message);
			return IMG2_E_CODEC;
		}
		int width = heif_image_handle_get_width(handle);
		int height = heif_image_handle_get_height(handle);
		heif_image_handle_release(handle);
		if (img2_decode_check_header_size("AVIF", width, height) != IMG2_OK) {
			return IMG2_E_LIMIT; /* [P33] one image alone over the limits */
		} else if (width > 0 && height > 0 && img2_decode_add_frame_pixels(&total, (uint32_t)width, (uint32_t)height) != IMG2_OK) {
			return IMG2_E_LIMIT;
		}
	}
	return IMG2_OK;
}

/**
 * @brief Decode AVIF image sequence with all frames
 *
 * Decodes all images in an AVIF image sequence (animated AVIF).
 * Each image is decoded independently.
 *
 * @param data Raw AVIF file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded
 * @return Array of img2_image_t* frames, or NULL on error
 *
 * @note Maximum MAX_AVIF_FRAMES frames (200) to prevent DoS
 * @note AVIF sequences may have timing info in metadata
 * @note Output format is RGBA8888
 *
 * Top-level image lists carry no timing: delay_ms / loop_count stay 0. Sequence tracks
 * (animated AVIF, with timing) are decoded by img2_heif_decode_sequence().
 */
static img2_image_t **decode_avif_animated(const uint8_t *data, size_t len, int *frame_count, img2_status_t *st)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_avif_animated");
		return img2_decode_fail(st, IMG2_E_ARG);
	}

	// Initialize output
	*frame_count = 0;

	// Create HEIF context (libheif handles AVIF)
	struct heif_context *ctx = heif_context_alloc();
	if (ctx == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate HEIF context for AVIF");
		return img2_decode_fail(st, IMG2_E_NOMEM);
	}

	// Read from memory
	struct heif_error err = heif_context_read_from_memory_without_copy(ctx, data, len, NULL);
	if (err.code != heif_error_Ok) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to read AVIF data: %s", err.message);
		heif_context_free(ctx);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Get number of top-level images
	int num_images = heif_context_get_number_of_top_level_images(ctx);
	if (num_images == 0) {
		img2_log(IMG2_LOG_ERROR, "Error: AVIF image sequence has no images");
		heif_context_free(ctx);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Enforce MAX_AVIF_FRAMES limit
	if (num_images > MAX_AVIF_FRAMES) {
		img2_log(IMG2_LOG_WARN, "Warning: AVIF has %d images, limiting to %d", num_images, MAX_AVIF_FRAMES);
		num_images = MAX_AVIF_FRAMES;
	}

	// Get list of image IDs
	heif_item_id *image_ids = (heif_item_id *)malloc(sizeof(heif_item_id) * num_images);
	if (image_ids == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate image ID array");
		heif_context_free(ctx);
		return img2_decode_fail(st, IMG2_E_NOMEM);
	}

	int actual_count = heif_context_get_list_of_top_level_image_IDs(ctx, image_ids, num_images);
	if (actual_count != num_images) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to get image ID list");
		free(image_ids);
		heif_context_free(ctx);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// [F14] reject before decoding anything: the images' (transformed) sizes must fit IMG2_MAX_TOTAL_PIXELS
	img2_status_t pre_st = avif_check_total_pixels(ctx, image_ids, num_images);
	if (pre_st != IMG2_OK) {
		free(image_ids);
		heif_context_free(ctx);
		return img2_decode_fail(st, pre_st);
	}

	// Allocate frames array
	img2_image_t **frames = (img2_image_t **)malloc(sizeof(img2_image_t *) * num_images);
	if (frames == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate frames array");
		free(image_ids);
		heif_context_free(ctx);
		return img2_decode_fail(st, IMG2_E_NOMEM);
	}

	// Initialize frames to NULL for cleanup
	for (int i = 0; i < num_images; i++) {
		frames[i] = NULL;
	}

	// Decode each image
	uint64_t total_pixels = 0;
	for (int i = 0; i < num_images; i++) {
		// Get image handle
		struct heif_image_handle *handle = NULL;
		err = heif_context_get_image_handle(ctx, image_ids[i], &handle);
		if (err.code != heif_error_Ok) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to get image handle %d: %s", i, err.message);
			*st = IMG2_E_CODEC;
			goto cleanup_error;
		}

		// Decode image to RGBA
		struct heif_image *img = NULL;
		err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, NULL);
		if (err.code != heif_error_Ok) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to decode AVIF image %d: %s", i, err.message);
			heif_image_handle_release(handle);
			*st = IMG2_E_CODEC;
			goto cleanup_error;
		}

		// Get dimensions
		int width = heif_image_get_width(img, heif_channel_interleaved);
		int height = heif_image_get_height(img, heif_channel_interleaved);

		// Get RGBA plane
		int stride = 0;
		const uint8_t *plane = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);
		if (plane == NULL) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to get AVIF image plane %d", i);
			heif_image_release(img);
			heif_image_handle_release(handle);
			*st = IMG2_E_CODEC;
			goto cleanup_error;
		}

		// Create output frame ([F14] within the running pixel total)
		if (img2_decode_add_frame_pixels(&total_pixels, (uint32_t)width, (uint32_t)height) != IMG2_OK) {
			heif_image_release(img);
			heif_image_handle_release(handle);
			*st = IMG2_E_LIMIT;
			goto cleanup_error;
		}
		img2_status_t alloc_st = img2_decode_image_alloc((uint32_t)width, (uint32_t)height, &frames[i]);
		if (alloc_st != IMG2_OK) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to create output frame %d", i);
			heif_image_release(img);
			heif_image_handle_release(handle);
			*st = alloc_st;
			goto cleanup_error;
		}

		// Copy pixels row-by-row (handle stride != width*4)
		for (int y = 0; y < height; y++) {
			const uint8_t *src_row = plane + y * stride;
			uint8_t *dst_row = frames[i]->pixels + y * width * 4;
			memcpy(dst_row, src_row, width * 4);
		}

		// Release this image's resources
		heif_image_release(img);
		heif_image_handle_release(handle);
	}

	// Cleanup
	free(image_ids);
	heif_context_free(ctx);

	*frame_count = num_images;

	return frames;

cleanup_error:
	// Cleanup on error
	for (int i = 0; i < num_images; i++) {
		if (frames[i] != NULL) {
			img2_image_destroy(frames[i]);
		}
	}
	free(frames);
	free(image_ids);
	heif_context_free(ctx);
	return NULL; /* *st set at the goto */
}

/**
 * @brief Decode AVIF image (static or image sequence)
 *
 * Main entry point for AVIF decoding. Automatically detects if the image
 * is a sequence and routes to the appropriate decoder function.
 *
 * @param data Raw AVIF file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded (1 for static, N for sequence)
 * @return Array of img2_image_t* frames, or NULL on error
 *
 * @note Caller must free returned array with img2_decoder_free_frames()
 * @note For static images, frame_count = 1
 * @note For image sequences, frame_count = N (max MAX_AVIF_FRAMES)
 * @note Output format is RGBA8888
 */
static img2_image_t **decode_avif_frames(const uint8_t *data, size_t len, int *frame_count, int *loop_count, img2_status_t *st)
{
	if (data == NULL || len == 0 || frame_count == NULL || loop_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_avif");
		return img2_decode_fail(st, IMG2_E_ARG);
	}

	// Initialize output
	*frame_count = 0;
	*loop_count = 0;

	// Image list, sequence track (animated AVIF) or single image
	switch (img2_heif_layout(data, len, NULL)) {
	case IMG2_HEIF_LAYOUT_LIST:
		return decode_avif_animated(data, len, frame_count, st);
	case IMG2_HEIF_LAYOUT_SEQUENCE:
		return img2_heif_decode_sequence(data, len, "AVIF", frame_count, loop_count, st);
	case IMG2_HEIF_LAYOUT_SINGLE:
	default:
		return decode_avif_static(data, len, frame_count, st);
	}
}

img2_status_t img2_decode_avif(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out)
{
	(void)opts;
	if (out == NULL) {
		return IMG2_E_ARG;
	}
	img2_status_t st = IMG2_E_INTERNAL;
	int count = 0;
	int loop_count = 0;
	img2_image_t **frames = decode_avif_frames(data, len, &count, &loop_count, &st);
	st = img2_frames_adopt(out, frames, count, st);
	if (st == IMG2_OK) {
		out->loop_count = loop_count;
	}
	return st;
}
