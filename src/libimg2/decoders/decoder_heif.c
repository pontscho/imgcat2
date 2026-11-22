/**
 * @file decoder_heif.c
 * @brief HEIF decoder implementation using libheif
 *
 * Decodes HEIF/HEIC images (both static and image sequences) to RGBA8888 format.
 * Handles both single images and burst photo sequences.
 */

/* clang-format off */
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libheif/heif.h>
/* clang-format on */

#include "decoder.h"
#include "../core/log.h"

#ifdef IMG2_HAVE_EXIF_READER
#include "../metadata/exif_reader.h"
#endif

/** Maximum number of HEIF frames to decode (prevents DoS) */
#define MAX_HEIF_FRAMES 200

/* The sequence (track) API with heif_track_get_number_of_repetitions and
 * heif_decoding_options.ignore_sequence_editlist: the vendored libheif 1.23 has it; an older host libheif
 * (IMG2_USE_SYSTEM_HEIF) decodes top-level images only, as before. */
#if LIBHEIF_HAVE_VERSION(1, 23, 0)
#define IMG2_HEIF_HAVE_SEQUENCES 1
#include <libheif/heif_sequences.h>
#else
#define IMG2_HEIF_HAVE_SEQUENCES 0
#endif

#if IMG2_HEIF_HAVE_SEQUENCES
/**
 * @brief Count the samples of the first visual sequence track, up to 2, without decoding them
 *
 * Raw sample reads follow the edit list (the public raw API cannot ignore it), so with a repeating edit
 * list a one-sample track would yield its sample twice: in that case two byte-identical samples count as 1.
 *
 * @return 0 (no sequence / visual track), 1 or 2
 */
static int heif_count_track_samples(struct heif_context *ctx)
{
	if (!heif_context_has_sequence(ctx)) {
		return 0;
	}
	heif_track *track = heif_context_get_track(ctx, 0);
	if (track == NULL) {
		return 0;
	}
	uint32_t repetitions = heif_track_get_number_of_repetitions(track);
	bool repeated = repetitions != 0 && repetitions != 1;

	heif_raw_sequence_sample *samples[2] = { NULL, NULL };
	int count = 0;
	while (count < 2) {
		struct heif_error err = heif_track_get_next_raw_sequence_sample(track, &samples[count]);
		if (err.code != heif_error_Ok || samples[count] == NULL) {
			samples[count] = NULL;
			break;
		}
		count++;
	}
	if (count == 2 && repeated) {
		size_t size0 = 0;
		size_t size1 = 0;
		const uint8_t *d0 = heif_raw_sequence_sample_get_data(samples[0], &size0);
		const uint8_t *d1 = heif_raw_sequence_sample_get_data(samples[1], &size1);
		if (d0 != NULL && d1 != NULL && size0 == size1 && memcmp(d0, d1, size0) == 0) {
			count = 1;
		}
	}
	heif_raw_sequence_sample_release(samples[0]);
	heif_raw_sequence_sample_release(samples[1]);
	heif_track_release(track);
	return count;
}
#endif

img2_heif_layout_t img2_heif_layout(const uint8_t *data, size_t len, int *frames_hint)
{
	if (frames_hint != NULL) {
		*frames_hint = 1;
	}
	if (data == NULL || len == 0) {
		return IMG2_HEIF_LAYOUT_SINGLE;
	}

	struct heif_context *ctx = heif_context_alloc();
	if (ctx == NULL) {
		return IMG2_HEIF_LAYOUT_SINGLE;
	}
	struct heif_error err = heif_context_read_from_memory_without_copy(ctx, data, len, NULL);
	if (err.code != heif_error_Ok) {
		heif_context_free(ctx);
		return IMG2_HEIF_LAYOUT_SINGLE;
	}

	img2_heif_layout_t layout = IMG2_HEIF_LAYOUT_SINGLE;
	int num_images = heif_context_get_number_of_top_level_images(ctx);
	if (num_images > 1) {
		// Top-level image list: unchanged behaviour, even if the file also carries a track
		layout = IMG2_HEIF_LAYOUT_LIST;
		if (frames_hint != NULL) {
			*frames_hint = num_images;
		}
	}
#if IMG2_HEIF_HAVE_SEQUENCES
	else {
		// No top-level image: any visual track sample makes it a sequence. With a primary image (e.g. the
		// still that animated AVIF files carry for compatibility) only an actual animation (>= 2 samples).
		int samples = heif_count_track_samples(ctx);
		if (samples >= (num_images <= 0 ? 1 : 2)) {
			layout = IMG2_HEIF_LAYOUT_SEQUENCE;
			if (frames_hint != NULL) {
				*frames_hint = samples;
			}
		}
	}
#endif

	heif_context_free(ctx);
	return layout;
}

bool img2_heif_is_animated(const uint8_t *data, size_t len)
{
	int frames = 1;
	img2_heif_layout_t layout = img2_heif_layout(data, len, &frames);
	return layout != IMG2_HEIF_LAYOUT_SINGLE && frames > 1;
}

#if IMG2_HEIF_HAVE_SEQUENCES
/** @brief Sample duration in track ticks -> ms, rounded to nearest; 0 if unknown (no duration or timescale) */
static uint32_t heif_ticks_to_ms(uint32_t ticks, uint32_t timescale)
{
	if (ticks == 0 || timescale == 0) {
		return 0;
	}
	uint64_t ms = ((uint64_t)ticks * 1000u + timescale / 2u) / timescale;
	return ms > UINT32_MAX ? UINT32_MAX : (uint32_t)ms;
}

/**
 * @brief heif_track_get_number_of_repetitions -> img2 loop_count
 *
 * libheif: 1 = no edit list (plays once), N = plays N times, UINT32_MAX = indefinite, 0 = an edit list
 * libheif does not interpret as a loop count (it advises a single playback; this is also what a file
 * whose track timescale differs from the movie timescale reports, see docs/libimg2.md).
 */
static int heif_loop_count(uint32_t repetitions)
{
	if (repetitions == heif_sequence_track_number_of_repetitions_infinite) {
		return 0;
	}
	if (repetitions == 0) {
		return 1;
	}
	return repetitions > (uint32_t)INT_MAX ? INT_MAX : (int)repetitions;
}
#endif

img2_image_t **img2_heif_decode_sequence(const uint8_t *data, size_t len, const char *fmt, int *frame_count, int *loop_count, img2_status_t *st)
{
	if (data == NULL || len == 0 || fmt == NULL || frame_count == NULL || loop_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_heif_sequence");
		return img2_decode_fail(st, IMG2_E_ARG);
	}
	*frame_count = 0;
	*loop_count = 0;

#if !IMG2_HEIF_HAVE_SEQUENCES
	img2_log(IMG2_LOG_ERROR, "Error: %s image sequences need libheif >= 1.23", fmt);
	return img2_decode_fail(st, IMG2_E_UNSUPPORTED);
#else
	struct heif_context *ctx = heif_context_alloc();
	if (ctx == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate HEIF context for %s", fmt);
		return img2_decode_fail(st, IMG2_E_NOMEM);
	}

	struct heif_error err = heif_context_read_from_memory_without_copy(ctx, data, len, NULL);
	if (err.code != heif_error_Ok) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to read %s data: %s", fmt, err.message);
		heif_context_free(ctx);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	heif_track *track = heif_context_get_track(ctx, 0);
	if (track == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: %s image sequence has no visual track", fmt);
		heif_context_free(ctx);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	img2_image_t **frames = NULL;
	heif_decoding_options *dopts = NULL;
	int count = 0;
	uint64_t total_pixels = 0;
	uint16_t track_w = 0;
	uint16_t track_h = 0;
	size_t byte_count = 0;
	uint32_t timescale = heif_track_get_timescale(track); // durations are in TRACK ticks, not the movie timescale
	uint32_t repetitions = heif_track_get_number_of_repetitions(track);

	// Reject an oversized track resolution before libheif decodes (and allocates) the first sample
	err = heif_track_get_image_resolution(track, &track_w, &track_h);
	if (err.code == heif_error_Ok && track_w > 0 && track_h > 0 && !img2_image_calc_size(track_w, track_h, &byte_count)) {
		img2_log(IMG2_LOG_ERROR, "Error: %s image sequence resolution %ux%u exceeds the limits", fmt, (unsigned)track_w, (unsigned)track_h);
		*st = IMG2_E_LIMIT;
		goto cleanup_error;
	}

	// Play the media timeline once: repetition is reported as loop_count, not as repeated frames
	dopts = heif_decoding_options_alloc();
	if (dopts == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate HEIF decoding options");
		*st = IMG2_E_NOMEM;
		goto cleanup_error;
	}
	dopts->ignore_sequence_editlist = 1;

	frames = (img2_image_t **)calloc(MAX_HEIF_FRAMES, sizeof(img2_image_t *));
	if (frames == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate frames array");
		*st = IMG2_E_NOMEM;
		goto cleanup_error;
	}

	for (;;) {
		struct heif_image *img = NULL;
		err = heif_track_decode_next_image(track, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, dopts);
		if (err.code == heif_error_End_of_sequence) {
			break;
		}
		if (err.code != heif_error_Ok || img == NULL) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to decode %s sequence frame %d: %s", fmt, count, err.message);
			if (img != NULL) {
				heif_image_release(img);
			}
			*st = IMG2_E_CODEC;
			goto cleanup_error;
		}
		if (count == MAX_HEIF_FRAMES) {
			// A 201st frame exists: keep the first 200 (the rest is not decoded)
			heif_image_release(img);
			img2_log(IMG2_LOG_WARN, "Warning: %s sequence has more than %d frames, limiting to %d", fmt, MAX_HEIF_FRAMES, MAX_HEIF_FRAMES);
			break;
		}

		int width = heif_image_get_width(img, heif_channel_interleaved);
		int height = heif_image_get_height(img, heif_channel_interleaved);
		int stride = 0;
		const uint8_t *plane = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);
		if (plane == NULL || width <= 0 || height <= 0) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to get %s sequence plane %d", fmt, count);
			heif_image_release(img);
			*st = IMG2_E_CODEC;
			goto cleanup_error;
		}

		// [F14] running pixel total, then the per-frame LIMIT/NOMEM allocation gate
		if (img2_decode_add_frame_pixels(&total_pixels, (uint32_t)width, (uint32_t)height) != IMG2_OK) {
			heif_image_release(img);
			*st = IMG2_E_LIMIT;
			goto cleanup_error;
		}
		img2_status_t alloc_st = img2_decode_image_alloc((uint32_t)width, (uint32_t)height, &frames[count]);
		if (alloc_st != IMG2_OK) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to create output frame %d", count);
			heif_image_release(img);
			*st = alloc_st;
			goto cleanup_error;
		}

		size_t row_bytes = (size_t)width * 4u;
		for (int y = 0; y < height; y++) {
			memcpy(frames[count]->pixels + (size_t)y * row_bytes, plane + (size_t)y * (size_t)stride, row_bytes);
		}
		frames[count]->delay_ms = heif_ticks_to_ms(heif_image_get_duration(img), timescale);
		heif_image_release(img);
		count++;
	}

	if (count == 0) {
		img2_log(IMG2_LOG_ERROR, "Error: %s image sequence has no frames", fmt);
		*st = IMG2_E_CODEC;
		goto cleanup_error;
	}

	if (count == 1) {
		frames[0]->delay_ms = 0; // single frame: not animated
	} else {
		*loop_count = heif_loop_count(repetitions);
	}

	heif_decoding_options_free(dopts);
	heif_track_release(track);
	heif_context_free(ctx);
	*frame_count = count;
	return frames;

cleanup_error:
	if (frames != NULL) {
		for (int i = 0; i < MAX_HEIF_FRAMES; i++) {
			if (frames[i] != NULL) {
				img2_image_destroy(frames[i]);
			}
		}
		free(frames);
	}
	if (dopts != NULL) {
		heif_decoding_options_free(dopts);
	}
	heif_track_release(track);
	heif_context_free(ctx);
	return NULL; /* *st set at the goto */
#endif
}

/**
 * @brief Decode static HEIF image (single frame)
 *
 * Decodes a static HEIF image to RGBA8888 format.
 * For HEIF image sequences, use decode_heif_animated().
 *
 * @param data Raw HEIF file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (single frame)
 * @return Array with single img2_image_t*, or NULL on error
 *
 * @note Output format is RGBA8888
 */
static img2_image_t **decode_heif_static(const uint8_t *data, size_t len, int *frame_count, img2_status_t *st)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_heif_static");
		return img2_decode_fail(st, IMG2_E_ARG);
	}

	// Initialize output
	*frame_count = 0;

	// Create HEIF context
	struct heif_context *ctx = heif_context_alloc();
	if (ctx == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate HEIF context");
		return img2_decode_fail(st, IMG2_E_NOMEM);
	}

	// Read from memory
	struct heif_error err = heif_context_read_from_memory_without_copy(ctx, data, len, NULL);
	if (err.code != heif_error_Ok) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to read HEIF data: %s", err.message);
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
	img2_status_t pre_st = img2_decode_check_header_size("HEIF", heif_image_handle_get_width(handle), heif_image_handle_get_height(handle));
	if (pre_st != IMG2_OK) {
		heif_image_handle_release(handle);
		heif_context_free(ctx);
		return img2_decode_fail(st, pre_st);
	}

	// Decode image to RGBA
	struct heif_image *img = NULL;
	err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, NULL);
	if (err.code != heif_error_Ok) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to decode HEIF image: %s", err.message);
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
		img2_log(IMG2_LOG_ERROR, "Error: Failed to get HEIF image plane");
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
static img2_status_t heif_check_total_pixels(struct heif_context *ctx, const heif_item_id *ids, int count)
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
		if (img2_decode_check_header_size("HEIF", width, height) != IMG2_OK) {
			return IMG2_E_LIMIT; /* [P33] one image alone over the limits */
		} else if (width > 0 && height > 0 && img2_decode_add_frame_pixels(&total, (uint32_t)width, (uint32_t)height) != IMG2_OK) {
			return IMG2_E_LIMIT;
		}
	}
	return IMG2_OK;
}

/**
 * @brief Decode HEIF image sequence with all frames
 *
 * Decodes all images in a HEIF image sequence (e.g., burst photos).
 * Each image is decoded independently.
 *
 * @param data Raw HEIF file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded
 * @return Array of img2_image_t* frames, or NULL on error
 *
 * @note Maximum MAX_HEIF_FRAMES frames (200) to prevent DoS
 * @note Unlike GIF/WebP, HEIF sequences have no timing info - uniform frame rate
 * @note Output format is RGBA8888
 *
 * Top-level image lists carry no timing: delay_ms / loop_count stay 0. Sequence tracks
 * (with timing) are decoded by img2_heif_decode_sequence().
 */
static img2_image_t **decode_heif_animated(const uint8_t *data, size_t len, int *frame_count, img2_status_t *st)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_heif_animated");
		return img2_decode_fail(st, IMG2_E_ARG);
	}

	// Initialize output
	*frame_count = 0;

	// Create HEIF context
	struct heif_context *ctx = heif_context_alloc();
	if (ctx == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate HEIF context");
		return img2_decode_fail(st, IMG2_E_NOMEM);
	}

	// Read from memory
	struct heif_error err = heif_context_read_from_memory_without_copy(ctx, data, len, NULL);
	if (err.code != heif_error_Ok) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to read HEIF data: %s", err.message);
		heif_context_free(ctx);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Get number of top-level images
	int num_images = heif_context_get_number_of_top_level_images(ctx);
	if (num_images == 0) {
		img2_log(IMG2_LOG_ERROR, "Error: HEIF image sequence has no images");
		heif_context_free(ctx);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Enforce MAX_HEIF_FRAMES limit
	if (num_images > MAX_HEIF_FRAMES) {
		img2_log(IMG2_LOG_WARN, "Warning: HEIF has %d images, limiting to %d", num_images, MAX_HEIF_FRAMES);
		num_images = MAX_HEIF_FRAMES;
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
	img2_status_t pre_st = heif_check_total_pixels(ctx, image_ids, num_images);
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
			img2_log(IMG2_LOG_ERROR, "Error: Failed to decode HEIF image %d: %s", i, err.message);
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
			img2_log(IMG2_LOG_ERROR, "Error: Failed to get HEIF image plane %d", i);
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
 * @brief Decode HEIF image (static or image sequence)
 *
 * Main entry point for HEIF decoding. Automatically detects if the image
 * is a sequence and routes to the appropriate decoder function.
 *
 * @param data Raw HEIF file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded (1 for static, N for sequence)
 * @return Array of img2_image_t* frames, or NULL on error
 *
 * @note Caller must free returned array with img2_decoder_free_frames()
 * @note For static images, frame_count = 1
 * @note For image sequences, frame_count = N (max MAX_HEIF_FRAMES)
 * @note Output format is RGBA8888
 */
static img2_image_t **decode_heif_frames(const uint8_t *data, size_t len, int *frame_count, int *loop_count, img2_status_t *st)
{
	if (data == NULL || len == 0 || frame_count == NULL || loop_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_heif");
		return img2_decode_fail(st, IMG2_E_ARG);
	}

	// Initialize output
	*frame_count = 0;
	*loop_count = 0;

	// Image list, sequence track or single image
	switch (img2_heif_layout(data, len, NULL)) {
	case IMG2_HEIF_LAYOUT_LIST:
		return decode_heif_animated(data, len, frame_count, st);
	case IMG2_HEIF_LAYOUT_SEQUENCE:
		return img2_heif_decode_sequence(data, len, "HEIF", frame_count, loop_count, st);
	case IMG2_HEIF_LAYOUT_SINGLE:
	default:
		return decode_heif_static(data, len, frame_count, st);
	}
}

img2_status_t img2_decode_heif(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out)
{
	(void)opts;
	if (out == NULL) {
		return IMG2_E_ARG;
	}
	img2_status_t st = IMG2_E_INTERNAL;
	int count = 0;
	int loop_count = 0;
	img2_image_t **frames = decode_heif_frames(data, len, &count, &loop_count, &st);
	st = img2_frames_adopt(out, frames, count, st);
	if (st == IMG2_OK) {
		out->loop_count = loop_count;
	}
	return st;
}
