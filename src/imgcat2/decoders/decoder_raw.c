/**
 * @file decoder_raw.c
 * @brief RAW image decoder implementation using libraw
 *
 * Decodes camera RAW images (CR2, NEF, ARW, DNG, RAF, ORF, RW2, etc.) to RGBA8888 format.
 * Supports 100+ RAW formats via LibRAW library.
 */

/* clang-format off */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libraw/libraw.h>
/* clang-format on */

#include "decoder.h"

#ifdef HAVE_EXIF_READER
#include "../metadata/exif_reader.h"
#endif

// Forward declaration for JPEG decoder (used for thumbnail extraction)
extern image_t **decode_jpeg(const uint8_t *data, size_t len, int *frame_count);

/**
 * @brief Decode static RAW image (single frame)
 *
 * Decodes a RAW camera image to RGBA8888 format.
 * RAW files are always single-frame (no animation support).
 *
 * @param data Raw RAW file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (single frame)
 * @return Array with single image_t*, or NULL on error
 *
 * @note Output format is RGBA8888
 * @note Processing parameters: sRGB color space, camera white balance, AHD demosaicing
 */
static image_t **decode_raw_static(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_raw_static\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Create LibRAW processor
	libraw_data_t *raw = libraw_init(0);
	if (raw == NULL) {
		fprintf(stderr, "Error: Failed to initialize LibRAW\n");
		return NULL;
	}

	// Open buffer
	// Cast away const - libraw API doesn't accept const but won't modify data
	int ret = libraw_open_buffer(raw, (void *)data, len);
	if (ret != LIBRAW_SUCCESS) {
		fprintf(stderr, "Error: Failed to open RAW buffer: %s\n", libraw_strerror(ret));
		libraw_close(raw);
		return NULL;
	}

	// Check camera make/model for special handling
	// const char *make = raw->idata.make;
	// const char *model = raw->idata.model;
	// bool is_nikon_z = (make[0] && strstr(make, "Nikon") && model[0] && model[0] == 'Z' && model[1] == ' ');

	// Try embedded thumbnail extraction first
	// For Nikon Z: thumbnail is REQUIRED (high-efficiency compression won't unpack)
	// For others: thumbnail is OPTIONAL (faster if available, fallback to full RAW if not)
	ret = libraw_unpack_thumb(raw);
	if (ret == LIBRAW_SUCCESS) {
		libraw_processed_image_t *thumb = libraw_dcraw_make_mem_thumb(raw, &ret);
		// Check data_size instead of width/height (width/height might be 0 but data valid!)
		if (thumb && thumb->data_size > 0) {
			// if (is_nikon_z) {
			// 	fprintf(stderr, "Info: Nikon Z-series detected, using embedded thumbnail\n");
			// 	fprintf(stderr, "      (high-efficiency RAW compression not supported by LibRaw 0.22.0)\n");
			// } else {
			// 	fprintf(stderr, "Info: Using embedded %s thumbnail (faster)\n", thumb->type == LIBRAW_IMAGE_JPEG ? "JPEG" : "RGB");
			// }

			image_t *output = NULL;
			if (thumb->type == LIBRAW_IMAGE_JPEG) {
				// Decode JPEG thumbnail
				int tc = 0;
				image_t **tf = decode_jpeg(thumb->data, thumb->data_size, &tc);
				if (tf && tc > 0) {
					output = tf[0];
					free(tf);
				}
			} else if (thumb->type == LIBRAW_IMAGE_BITMAP) {
				// Convert RGB bitmap thumbnail
				output = image_create(thumb->width, thumb->height);
				if (output) {
					const uint8_t *s = thumb->data;
					uint8_t *d = output->pixels;
					for (int i = 0; i < thumb->width * thumb->height; i++) {
						*d++ = *s++; // R
						*d++ = *s++; // G
						*d++ = *s++; // B
						*d++ = 0xFF; // A
					}
				}
			}

			if (output) {
				// Add EXIF metadata
#ifdef HAVE_EXIF_READER
				exif_info_t *exif = malloc(sizeof(exif_info_t));
				if (exif) {
					exif_info_init(exif);
					if (raw->idata.make[0]) {
						strncpy(exif->make, raw->idata.make, sizeof(exif->make) - 1);
					}
					if (raw->idata.model[0]) {
						strncpy(exif->model, raw->idata.model, sizeof(exif->model) - 1);
					}
					if (raw->other.iso_speed > 0) {
						exif->iso_speed = (uint16_t)raw->other.iso_speed;
					}
					if (raw->other.shutter > 0) {
						exif->exposure_time = raw->other.shutter;
					}
					if (raw->other.aperture > 0) {
						exif->f_number = raw->other.aperture;
					}
					if (raw->other.focal_len > 0) {
						exif->focal_length = raw->other.focal_len;
					}
					output->exif = exif;
				}
#endif
				libraw_dcraw_clear_mem(thumb);
				libraw_close(raw);

				image_t **frames = malloc(sizeof(image_t *));
				if (frames) {
					frames[0] = output;
					*frame_count = 1;
					return frames;
				}
				image_destroy(output);
				return NULL;
			}
			libraw_dcraw_clear_mem(thumb);
		}
	}

	// Thumbnail extraction failed or unavailable - try full RAW processing
	// fprintf(stderr, "Info: Processing full RAW image...\n");

	// Unpack RAW data
	ret = libraw_unpack(raw);
	if (ret != LIBRAW_SUCCESS) {
		// RAW unpack failed - give helpful error message
		// fprintf(stderr, "Error: Failed to unpack RAW data: %s\n", libraw_strerror(ret));

		// if (is_nikon_z) {
		// 	// Nikon Z-series specific error message
		// 	fprintf(stderr, "\nNote: Nikon Z-series 'high efficiency' compression is not supported by LibRaw 0.22.0\n");
		// 	fprintf(stderr, "      Camera: %s %s\n", make, model);
		// 	fprintf(stderr, "      Hint: Use 'standard' or 'lossless' compression in camera settings\n");
		// 	fprintf(stderr, "      Or convert with: darktable, RawTherapee, Adobe DNG Converter\n");
		// } else {
		// 	fprintf(stderr, "      Camera: %s %s\n", make, model);
		// }

		libraw_close(raw);
		return NULL;
	}

	// Configure processing parameters
	raw->params.output_color = 1; // sRGB color space
	raw->params.output_bps = 8; // 8-bit output
	raw->params.gamm[0] = 1.0 / 2.4; // sRGB gamma curve
	raw->params.gamm[1] = 12.92; // sRGB gamma slope
	raw->params.no_auto_bright = 0; // Enable auto brightness
	raw->params.use_camera_wb = 1; // Use camera white balance
	raw->params.use_auto_wb = 0; // Disable auto white balance
	raw->params.user_qual = 3; // AHD demosaicing
	raw->params.use_fuji_rotate = 1; // Rotate Fuji images correctly

	// Process RAW data (demosaicing, color correction)
	ret = libraw_dcraw_process(raw);
	if (ret != LIBRAW_SUCCESS) {
		fprintf(stderr, "Error: Failed to process RAW data: %s\n", libraw_strerror(ret));
		libraw_close(raw);
		return NULL;
	}

	// Get processed image
	libraw_processed_image_t *processed = libraw_dcraw_make_mem_image(raw, &ret);
	if (processed == NULL) {
		fprintf(stderr, "Error: Failed to create memory image: %s\n", libraw_strerror(ret));
		libraw_close(raw);
		return NULL;
	}

	// Create image_t structure
	image_t *output = image_create(processed->width, processed->height);
	if (output == NULL) {
		fprintf(stderr, "Error: Failed to create image_t structure\n");
		libraw_dcraw_clear_mem(processed);
		libraw_close(raw);
		return NULL;
	}

	// Convert RGB to RGBA8888
	// LibRAW returns RGB (3 bytes/pixel), we need RGBA8888 (4 bytes/pixel)
	const uint8_t *src = processed->data;
	uint8_t *dst = output->pixels;

	for (int y = 0; y < (int)processed->height; y++) {
		for (int x = 0; x < (int)processed->width; x++) {
			*dst++ = *src++; // R
			*dst++ = *src++; // G
			*dst++ = *src++; // B
			*dst++ = 0xFF; // A (fully opaque)
		}
	}

	// Extract EXIF metadata from libraw structures
#ifdef HAVE_EXIF_READER
	exif_info_t *exif = malloc(sizeof(exif_info_t));
	if (exif) {
		exif_info_init(exif);

		// Map libraw fields to exif_info_t
		// Camera make/model
		if (raw->idata.make[0] != '\0') {
			strncpy(exif->make, raw->idata.make, sizeof(exif->make) - 1);
			exif->make[sizeof(exif->make) - 1] = '\0';
		}
		if (raw->idata.model[0] != '\0') {
			strncpy(exif->model, raw->idata.model, sizeof(exif->model) - 1);
			exif->model[sizeof(exif->model) - 1] = '\0';
		}

		// Software
		if (raw->idata.software[0] != '\0') {
			strncpy(exif->software, raw->idata.software, sizeof(exif->software) - 1);
			exif->software[sizeof(exif->software) - 1] = '\0';
		}

		// ISO speed
		if (raw->other.iso_speed > 0) {
			exif->iso_speed = (uint16_t)raw->other.iso_speed;
		}

		// Exposure time (shutter speed)
		if (raw->other.shutter > 0) {
			exif->exposure_time = raw->other.shutter;
		}

		// Aperture (f-number)
		if (raw->other.aperture > 0) {
			exif->f_number = raw->other.aperture;
		}

		// Focal length
		if (raw->other.focal_len > 0) {
			exif->focal_length = raw->other.focal_len;
		}

		// Lens information
		if (raw->lens.Lens[0] != '\0') {
			strncpy(exif->lens_model, raw->lens.Lens, sizeof(exif->lens_model) - 1);
			exif->lens_model[sizeof(exif->lens_model) - 1] = '\0';
		}
		if (raw->lens.LensMake[0] != '\0') {
			strncpy(exif->lens_make, raw->lens.LensMake, sizeof(exif->lens_make) - 1);
			exif->lens_make[sizeof(exif->lens_make) - 1] = '\0';
		}
		if (raw->lens.MinFocal > 0) {
			exif->min_focal_length = raw->lens.MinFocal;
		}
		if (raw->lens.MaxFocal > 0) {
			exif->max_focal_length = raw->lens.MaxFocal;
		}
		if (raw->lens.MaxAp4MinFocal > 0) {
			exif->max_aperture = raw->lens.MaxAp4MinFocal;
		}

		// Image description and artist
		if (raw->other.desc[0] != '\0') {
			strncpy(exif->description, raw->other.desc, sizeof(exif->description) - 1);
			exif->description[sizeof(exif->description) - 1] = '\0';
		}
		if (raw->other.artist[0] != '\0') {
			strncpy(exif->artist, raw->other.artist, sizeof(exif->artist) - 1);
			exif->artist[sizeof(exif->artist) - 1] = '\0';
		}

		// GPS coordinates
		if (raw->other.parsed_gps.gpsparsed) {
			exif->has_gps = true;
			exif->gps_latitude = raw->other.parsed_gps.latitude[0] + raw->other.parsed_gps.latitude[1] / 60.0 + raw->other.parsed_gps.latitude[2] / 3600.0;
			exif->gps_longitude = raw->other.parsed_gps.longitude[0] + raw->other.parsed_gps.longitude[1] / 60.0 + raw->other.parsed_gps.longitude[2] / 3600.0;
			exif->gps_altitude = raw->other.parsed_gps.altitude;
			exif->gps_latitude_ref = raw->other.parsed_gps.latref;
			exif->gps_longitude_ref = raw->other.parsed_gps.longref;
			exif->gps_altitude_ref = (char)raw->other.parsed_gps.altref;
		}

		// Store EXIF in image structure
		output->exif = exif;
	}
#endif

	// Cleanup LibRAW resources
	libraw_dcraw_clear_mem(processed);
	libraw_close(raw);

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
 * @brief Main entry point for RAW decoding
 *
 * @param data Raw RAW file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded (always 1 for RAW)
 * @return Array of image_t* pointers, or NULL on error
 */
image_t **decode_raw(const uint8_t *data, size_t len, int *frame_count)
{
	// RAW files are always single images (no animation)
	return decode_raw_static(data, len, frame_count);
}
