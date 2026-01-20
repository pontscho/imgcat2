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

#ifdef HAVE_EXIF_READER
#include "../metadata/exif_reader.h"
#endif

/** Maximum number of AVIF frames to decode (prevents DoS) */
#define MAX_AVIF_FRAMES 200

/**
 * @brief Check if AVIF is an image sequence (has multiple images)
 *
 * Quickly checks if an AVIF file contains multiple top-level images without
 * fully decoding the image data. Useful for determining whether
 * to use static or animated decoder.
 *
 * @param data Raw AVIF file data
 * @param len Size of data in bytes
 * @return true if AVIF has multiple images, false otherwise or on error
 *
 * @note Returns false if data is invalid or not an AVIF
 * @note Does not validate that images are decodable, only checks count
 */
bool avif_is_animated(const uint8_t *data, size_t len)
{
	if (data == NULL || len == 0) {
		return false;
	}

	// Create HEIF context (libheif handles AVIF too)
	struct heif_context *ctx = heif_context_alloc();
	if (ctx == NULL) {
		return false;
	}

	// Read from memory
	struct heif_error err = heif_context_read_from_memory_without_copy(ctx, data, len, NULL);
	if (err.code != heif_error_Ok) {
		heif_context_free(ctx);
		return false;
	}

	// Get number of top-level images
	int num_images = heif_context_get_number_of_top_level_images(ctx);

	// Cleanup
	heif_context_free(ctx);

	// Image sequence if more than 1 image
	return num_images > 1;
}

/**
 * @brief Decode static AVIF image (single frame)
 *
 * Decodes a static AVIF image to RGBA8888 format.
 * For AVIF image sequences, use decode_avif_animated().
 *
 * @param data Raw AVIF file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (single frame)
 * @return Array with single image_t*, or NULL on error
 *
 * @note Output format is RGBA8888
 */
static image_t **decode_avif_static(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_avif_static\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Create HEIF context (libheif handles AVIF)
	struct heif_context *ctx = heif_context_alloc();
	if (ctx == NULL) {
		fprintf(stderr, "Error: Failed to allocate HEIF context for AVIF\n");
		return NULL;
	}

	// Read from memory
	struct heif_error err = heif_context_read_from_memory_without_copy(ctx, data, len, NULL);
	if (err.code != heif_error_Ok) {
		fprintf(stderr, "Error: Failed to read AVIF data: %s\n", err.message);
		heif_context_free(ctx);
		return NULL;
	}

	// Get primary image handle
	struct heif_image_handle *handle = NULL;
	err = heif_context_get_primary_image_handle(ctx, &handle);
	if (err.code != heif_error_Ok) {
		fprintf(stderr, "Error: Failed to get primary image handle: %s\n", err.message);
		heif_context_free(ctx);
		return NULL;
	}

	// Decode image to RGBA
	struct heif_image *img = NULL;
	err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, NULL);
	if (err.code != heif_error_Ok) {
		fprintf(stderr, "Error: Failed to decode AVIF image: %s\n", err.message);
		heif_image_handle_release(handle);
		heif_context_free(ctx);
		return NULL;
	}

	// Get image dimensions
	int width = heif_image_get_width(img, heif_channel_interleaved);
	int height = heif_image_get_height(img, heif_channel_interleaved);

	// Get RGBA plane
	int stride = 0;
	const uint8_t *plane = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);
	if (plane == NULL) {
		fprintf(stderr, "Error: Failed to get AVIF image plane\n");
		heif_image_release(img);
		heif_image_handle_release(handle);
		heif_context_free(ctx);
		return NULL;
	}

	// Create image_t structure
	image_t *output = image_create((uint32_t)width, (uint32_t)height);
	if (output == NULL) {
		fprintf(stderr, "Error: Failed to create image_t structure\n");
		heif_image_release(img);
		heif_image_handle_release(handle);
		heif_context_free(ctx);
		return NULL;
	}

	// Copy pixels row-by-row (handle stride != width*4)
	for (int y = 0; y < height; y++) {
		const uint8_t *src_row = plane + y * stride;
		uint8_t *dst_row = output->pixels + y * width * 4;
		memcpy(dst_row, src_row, width * 4);
	}

	// Parse EXIF/XMP metadata (Phase 4)
#ifdef HAVE_EXIF_READER
	exif_info_t *exif = malloc(sizeof(exif_info_t));
	xmp_info_t *xmp = malloc(sizeof(xmp_info_t));

	if (exif && xmp) {
		exif_info_init(exif);
		xmp_info_init(xmp);

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
										uint32_t offset = (exif_data[0] << 24) | (exif_data[1] << 16) | (exif_data[2] << 8) | exif_data[3];

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

									// Parse EXIF from TIFF data
									if (parse_exif_from_tiff(exif, tiff_data, tiff_size) != 0) {
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
										if (parse_xmp_from_xml(xmp, xmp_data, xmp_size) != 0) {
											xmp_info_free(xmp);
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
			xmp_info_free(xmp);
			free(xmp);
			output->xmp = NULL;
		}

	} else {
		// Allocation failed - free what we have
		if (exif) {
			free(exif);
		}
		if (xmp) {
			xmp_info_free(xmp);
			free(xmp);
		}
		output->exif = NULL;
		output->xmp = NULL;
	}
#endif

	// Cleanup HEIF resources
	heif_image_release(img);
	heif_image_handle_release(handle);
	heif_context_free(ctx);

	// Allocate frames array (single frame)
	image_t **frames = (image_t **)malloc(sizeof(image_t *));
	if (frames == NULL) {
		fprintf(stderr, "Error: Failed to allocate frames array\n");
		image_destroy(output);
		return NULL;
	}

	frames[0] = output;
	*frame_count = 1;

	return frames;
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
 * @return Array of image_t* frames, or NULL on error
 *
 * @note Maximum MAX_AVIF_FRAMES frames (200) to prevent DoS
 * @note AVIF sequences may have timing info in metadata
 * @note Output format is RGBA8888
 */
static image_t **decode_avif_animated(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_avif_animated\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Create HEIF context (libheif handles AVIF)
	struct heif_context *ctx = heif_context_alloc();
	if (ctx == NULL) {
		fprintf(stderr, "Error: Failed to allocate HEIF context for AVIF\n");
		return NULL;
	}

	// Read from memory
	struct heif_error err = heif_context_read_from_memory_without_copy(ctx, data, len, NULL);
	if (err.code != heif_error_Ok) {
		fprintf(stderr, "Error: Failed to read AVIF data: %s\n", err.message);
		heif_context_free(ctx);
		return NULL;
	}

	// Get number of top-level images
	int num_images = heif_context_get_number_of_top_level_images(ctx);
	if (num_images == 0) {
		fprintf(stderr, "Error: AVIF image sequence has no images\n");
		heif_context_free(ctx);
		return NULL;
	}

	// Enforce MAX_AVIF_FRAMES limit
	if (num_images > MAX_AVIF_FRAMES) {
		fprintf(stderr, "Warning: AVIF has %d images, limiting to %d\n", num_images, MAX_AVIF_FRAMES);
		num_images = MAX_AVIF_FRAMES;
	}

	// Get list of image IDs
	heif_item_id *image_ids = (heif_item_id *)malloc(sizeof(heif_item_id) * num_images);
	if (image_ids == NULL) {
		fprintf(stderr, "Error: Failed to allocate image ID array\n");
		heif_context_free(ctx);
		return NULL;
	}

	int actual_count = heif_context_get_list_of_top_level_image_IDs(ctx, image_ids, num_images);
	if (actual_count != num_images) {
		fprintf(stderr, "Error: Failed to get image ID list\n");
		free(image_ids);
		heif_context_free(ctx);
		return NULL;
	}

	// Allocate frames array
	image_t **frames = (image_t **)malloc(sizeof(image_t *) * num_images);
	if (frames == NULL) {
		fprintf(stderr, "Error: Failed to allocate frames array\n");
		free(image_ids);
		heif_context_free(ctx);
		return NULL;
	}

	// Initialize frames to NULL for cleanup
	for (int i = 0; i < num_images; i++) {
		frames[i] = NULL;
	}

	// Decode each image
	for (int i = 0; i < num_images; i++) {
		// Get image handle
		struct heif_image_handle *handle = NULL;
		err = heif_context_get_image_handle(ctx, image_ids[i], &handle);
		if (err.code != heif_error_Ok) {
			fprintf(stderr, "Error: Failed to get image handle %d: %s\n", i, err.message);
			goto cleanup_error;
		}

		// Decode image to RGBA
		struct heif_image *img = NULL;
		err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, NULL);
		if (err.code != heif_error_Ok) {
			fprintf(stderr, "Error: Failed to decode AVIF image %d: %s\n", i, err.message);
			heif_image_handle_release(handle);
			goto cleanup_error;
		}

		// Get dimensions
		int width = heif_image_get_width(img, heif_channel_interleaved);
		int height = heif_image_get_height(img, heif_channel_interleaved);

		// Get RGBA plane
		int stride = 0;
		const uint8_t *plane = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);
		if (plane == NULL) {
			fprintf(stderr, "Error: Failed to get AVIF image plane %d\n", i);
			heif_image_release(img);
			heif_image_handle_release(handle);
			goto cleanup_error;
		}

		// Create output frame
		frames[i] = image_create((uint32_t)width, (uint32_t)height);
		if (frames[i] == NULL) {
			fprintf(stderr, "Error: Failed to create output frame %d\n", i);
			heif_image_release(img);
			heif_image_handle_release(handle);
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
			image_destroy(frames[i]);
		}
	}
	free(frames);
	free(image_ids);
	heif_context_free(ctx);
	return NULL;
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
 * @return Array of image_t* frames, or NULL on error
 *
 * @note Caller must free returned array with decoder_free_frames()
 * @note For static images, frame_count = 1
 * @note For image sequences, frame_count = N (max MAX_AVIF_FRAMES)
 * @note Output format is RGBA8888
 */
image_t **decode_avif(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_avif\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Check if image sequence
	if (avif_is_animated(data, len)) {
		return decode_avif_animated(data, len, frame_count);
	}

	return decode_avif_static(data, len, frame_count);
}
