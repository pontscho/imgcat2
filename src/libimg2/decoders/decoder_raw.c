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
#include <time.h>
#include <libraw/libraw.h>
/* clang-format on */

#include "decoder.h"
#include "../core/log.h"

#ifdef IMG2_HAVE_EXIF_READER
#include "../metadata/exif_reader.h"
#endif

#ifdef IMG2_HAVE_EXIF_READER
/**
 * @brief Check if string has meaningful content (not just whitespace)
 */
static bool has_content(const char *str)
{
	if (!str || str[0] == '\0') {
		return false;
	}
	for (const char *p = str; *p; p++) {
		if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
			return true;
		}
	}
	return false;
}

/**
 * @brief Extract EXIF metadata from LibRaw structure
 *
 * Extracts all available EXIF metadata from a parsed LibRaw structure
 * into an img2_exif_t. Used by both thumbnail and full processing paths.
 *
 * @param raw LibRaw data structure (after open_buffer)
 * @return Allocated img2_exif_t with metadata, or NULL on failure
 */
static img2_exif_t *extract_raw_exif(libraw_data_t *raw)
{
	img2_exif_t *exif = malloc(sizeof(img2_exif_t));
	if (!exif) {
		return NULL;
	}
	img2_exif_init(exif);

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

	// Timestamp -> date_time string (EXIF format: "YYYY:MM:DD HH:MM:SS")
	if (raw->other.timestamp > 0) {
		struct tm tm_info;
		if (localtime_r(&raw->other.timestamp, &tm_info)) {
			strftime(exif->date_time, sizeof(exif->date_time), "%Y:%m:%d %H:%M:%S", &tm_info);
			exif->has_datetime = true;
		}
	}

	// ISO speed (LibRaw's float, e.g. 102400; NaN or <= 0 means none): rounded, and saturated at UINT32_MAX because
	// the uint32_t conversion of a float at or above 2^32 is undefined
	if (raw->other.iso_speed > 0) {
		exif->iso_speed = raw->other.iso_speed >= 4294967296.0f ? UINT32_MAX : (uint32_t)((double)raw->other.iso_speed + 0.5);
		exif->has_iso = true;
	}

	// Exposure time (shutter speed)
	if (raw->other.shutter > 0) {
		exif->exposure_time = raw->other.shutter;
		exif->has_exposure_time = true;
	}

	// Aperture (f-number)
	if (raw->other.aperture > 0) {
		exif->f_number = raw->other.aperture;
		exif->has_f_number = true;
	}

	// Focal length
	if (raw->other.focal_len > 0) {
		exif->focal_length = raw->other.focal_len;
		exif->has_focal_length = true;
	}

	// Image orientation from flip field
	// LibRaw flip: 0=none, 3=180, 5=90CCW, 6=90CW
	// EXIF orientation: 1=normal, 3=180, 6=90CW, 8=90CCW
	switch (raw->sizes.flip) {
		case 0: exif->orientation = 1; break; // Normal
		case 3: exif->orientation = 3; break; // 180 degrees
		case 5: exif->orientation = 8; break; // 90 CCW (270 CW)
		case 6: exif->orientation = 6; break; // 90 CW
		default: exif->orientation = 1; break;
	}

	// Image dimensions (raw sensor size)
	exif->image_width = raw->sizes.width;
	exif->image_height = raw->sizes.height;

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
	if (raw->lens.FocalLengthIn35mmFormat > 0) {
		exif->focal_length_35mm = raw->lens.FocalLengthIn35mmFormat;
	}

	// Image description and artist (skip whitespace-only strings)
	if (has_content(raw->other.desc)) {
		strncpy(exif->description, raw->other.desc, sizeof(exif->description) - 1);
		exif->description[sizeof(exif->description) - 1] = '\0';
	}
	if (has_content(raw->other.artist)) {
		strncpy(exif->artist, raw->other.artist, sizeof(exif->artist) - 1);
		exif->artist[sizeof(exif->artist) - 1] = '\0';
	}

	// GPS coordinates
	if (raw->other.parsed_gps.gpsparsed) {
		exif->has_gps = true;
		// Convert DMS to decimal degrees
		double lat = raw->other.parsed_gps.latitude[0] + raw->other.parsed_gps.latitude[1] / 60.0 + raw->other.parsed_gps.latitude[2] / 3600.0;
		double lon = raw->other.parsed_gps.longitude[0] + raw->other.parsed_gps.longitude[1] / 60.0 + raw->other.parsed_gps.longitude[2] / 3600.0;

		// Apply reference direction
		exif->gps_latitude = (raw->other.parsed_gps.latref == 'S') ? -lat : lat;
		exif->gps_longitude = (raw->other.parsed_gps.longref == 'W') ? -lon : lon;
		exif->gps_altitude = raw->other.parsed_gps.altitude;
		exif->gps_latitude_ref = raw->other.parsed_gps.latref;
		exif->gps_longitude_ref = raw->other.parsed_gps.longref;
		exif->gps_altitude_ref = (char)raw->other.parsed_gps.altref;
	}

	return exif;
}

/**
 * @brief Extract XMP metadata from LibRaw structure
 *
 * Parses embedded XMP data from the RAW file if available.
 *
 * @param raw LibRaw data structure (after open_buffer)
 * @return Allocated img2_xmp_t with metadata, or NULL if no XMP or failure
 */
static img2_xmp_t *extract_raw_xmp(libraw_data_t *raw)
{
	// Check if XMP data is available
	if (!raw->idata.xmpdata || raw->idata.xmplen == 0) {
		return NULL;
	}

	img2_xmp_t *xmp = malloc(sizeof(img2_xmp_t));
	if (!xmp) {
		return NULL;
	}
	img2_xmp_init(xmp);

	// Parse XMP from raw XML data
	if (img2_xmp_parse_xml(xmp, raw->idata.xmpdata, raw->idata.xmplen) != 0) {
		free(xmp);
		return NULL;
	}

	return xmp;
}

/**
 * @brief Give @p img LibRaw's metadata, replacing any the JPEG decoder found in an embedded preview
 *        (LibRaw prepends its own EXIF block to previews that lack one)
 */
static void raw_attach_metadata(img2_image_t *img, libraw_data_t *raw)
{
	if (img->exif != NULL) {
		img2_exif_clear(img->exif);
		free(img->exif);
	}
	if (img->xmp != NULL) {
		img2_xmp_clear(img->xmp);
		free(img->xmp);
	}
	img->exif = extract_raw_exif(raw);
	img->xmp = extract_raw_xmp(raw);
}
#endif

/**
 * @brief Whether a LibRaw bitmap holds width x height pixels of 8-bit grey (1 colour) or RGB (3 colours)
 *
 * LibRaw's bitmaps are 8-bit here (previews always, processed images by output_bps = 8), but their colour
 * count follows the file: grey previews, monochrome sensors.
 */
static bool raw_bitmap_ok(const libraw_processed_image_t *bmp)
{
	return (bmp->colors == 1 || bmp->colors == 3) && (size_t)bmp->data_size >= (size_t)bmp->width * bmp->height * (size_t)bmp->colors;
}

/** @brief Convert a bitmap accepted by raw_bitmap_ok() to RGBA8888 */
static void raw_bitmap_to_rgba(uint8_t *dst, const libraw_processed_image_t *bmp)
{
	const size_t pixels = (size_t)bmp->width * bmp->height;
	const size_t step = (size_t)bmp->colors;
	const size_t g = bmp->colors == 3 ? 1 : 0;
	const uint8_t *s = bmp->data;
	for (size_t i = 0; i < pixels; i++, s += step) {
		*dst++ = s[0];
		*dst++ = s[g];
		*dst++ = s[g * 2];
		*dst++ = 0xFF;
	}
}

/**
 * @brief Decode static RAW image (single frame)
 *
 * Decodes a RAW camera image to RGBA8888 format.
 * RAW files are always single-frame (no animation support).
 *
 * @param data Raw RAW file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (single frame)
 * @return Array with single img2_image_t*, or NULL on error
 *
 * @note Output format is RGBA8888
 * @note Processing parameters: sRGB color space, camera white balance, AHD demosaicing
 */
static img2_image_t **decode_raw_static(const uint8_t *data, size_t len, int *frame_count, img2_status_t *st)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_raw_static");
		return img2_decode_fail(st, IMG2_E_ARG);
	}

	// Initialize output
	*frame_count = 0;

	// Create LibRAW processor
	libraw_data_t *raw = libraw_init(0);
	if (raw == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to initialize LibRAW");
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Open buffer
	// Cast away const - libraw API doesn't accept const but won't modify data
	int ret = libraw_open_buffer(raw, (void *)data, len);
	if (ret != LIBRAW_SUCCESS) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to open RAW buffer: %s", libraw_strerror(ret));
		libraw_close(raw);
		return img2_decode_fail(st, IMG2_E_CODEC);
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

			img2_image_t *output = NULL;
			if (thumb->type == LIBRAW_IMAGE_JPEG) {
				// Decode JPEG thumbnail
				img2_frames_t tf = { 0 };
				if (img2_decode_jpeg(thumb->data, thumb->data_size, NULL, &tf) == IMG2_OK && tf.count > 0) {
					output = tf.frames[0];
					tf.frames[0] = NULL;
				}
				img2_frames_free(&tf);
			} else if (thumb->type == LIBRAW_IMAGE_BITMAP && raw_bitmap_ok(thumb)) {
				// Convert the grey or RGB bitmap thumbnail
				output = img2_image_alloc(thumb->width, thumb->height);
				if (output) {
					raw_bitmap_to_rgba(output->pixels, thumb);
				}
			}

			if (output) {
				// Add EXIF and XMP metadata using helper functions
#ifdef IMG2_HAVE_EXIF_READER
				raw_attach_metadata(output, raw);
#endif
				libraw_dcraw_clear_mem(thumb);
				libraw_close(raw);

				img2_image_t **frames = malloc(sizeof(img2_image_t *));
				if (frames) {
					frames[0] = output;
					*frame_count = 1;
					return frames;
				}
				img2_image_destroy(output);
				return img2_decode_fail(st, IMG2_E_NOMEM);
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
		return img2_decode_fail(st, IMG2_E_CODEC);
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
	// No rotation: the orientation is reported in the EXIF data (as for the embedded preview, and as every
	// other decoder does), the caller applies it. dcraw_process() sets sizes.flip to user_flip: keep the
	// file's value for the metadata.
	const ushort file_flip = raw->sizes.flip;
	raw->params.user_flip = 0;

	// Process RAW data (demosaicing, color correction)
	ret = libraw_dcraw_process(raw);
	if (ret != LIBRAW_SUCCESS) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to process RAW data: %s", libraw_strerror(ret));
		libraw_close(raw);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Get processed image
	libraw_processed_image_t *processed = libraw_dcraw_make_mem_image(raw, &ret);
	if (processed == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create memory image: %s", libraw_strerror(ret));
		libraw_close(raw);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}
	raw->sizes.flip = file_flip;

	// 8-bit RGB, or grey for monochrome sensors
	if (!raw_bitmap_ok(processed)) {
		img2_log(IMG2_LOG_ERROR, "Error: Unexpected RAW output format (%d colours)", processed->colors);
		libraw_dcraw_clear_mem(processed);
		libraw_close(raw);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Create img2_image_t structure
	img2_image_t *output = NULL;
	img2_status_t alloc_st = img2_decode_image_alloc(processed->width, processed->height, &output);
	if (alloc_st != IMG2_OK) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create image_t structure");
		libraw_dcraw_clear_mem(processed);
		libraw_close(raw);
		return img2_decode_fail(st, alloc_st);
	}

	// Convert RGB (or grey) to RGBA8888
	raw_bitmap_to_rgba(output->pixels, processed);

	// Extract EXIF and XMP metadata using helper functions
#ifdef IMG2_HAVE_EXIF_READER
	raw_attach_metadata(output, raw);
#endif

	// Cleanup LibRAW resources
	libraw_dcraw_clear_mem(processed);
	libraw_close(raw);

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
 * @brief Main entry point for RAW decoding
 *
 * @param data Raw RAW file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded (always 1 for RAW)
 * @return Array of img2_image_t* pointers, or NULL on error
 */
static img2_image_t **decode_raw_frames(const uint8_t *data, size_t len, int *frame_count, img2_status_t *st)
{
	// RAW files are always single images (no animation)
	return decode_raw_static(data, len, frame_count, st);
}

img2_status_t img2_decode_raw(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out)
{
	(void)opts;
	if (out == NULL) {
		return IMG2_E_ARG;
	}
	img2_status_t st = IMG2_E_INTERNAL;
	int count = 0;
	img2_image_t **frames = decode_raw_frames(data, len, &count, &st);
	return img2_frames_adopt(out, frames, count, st);
}
