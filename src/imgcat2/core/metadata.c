/**
 * @file metadata.c
 * @brief Image metadata output formatting implementation
 */

#include <stdio.h>
#include <string.h>

#include "metadata.h"

#ifdef HAVE_EXIF_READER
#include "../metadata/exif_reader.h"
#endif

/**
 * @brief Get MIME type string from enum
 *
 * Returns standard MIME type strings for supported image formats.
 */
const char *get_mime_string(mime_type_t mime)
{
	switch (mime) {
		case MIME_PNG: return "image/png";
		case MIME_JPEG: return "image/jpeg";
		case MIME_GIF: return "image/gif";
		case MIME_BMP: return "image/bmp";
		case MIME_WEBP: return "image/webp";
		case MIME_HEIF: return "image/heif";
		case MIME_TIFF: return "image/tiff";
		case MIME_TGA: return "image/x-tga";
		case MIME_PSD: return "image/vnd.adobe.photoshop";
		case MIME_HDR: return "image/vnd.radiance";
		case MIME_PNM: return "image/x-portable-anymap";
		default: return "application/octet-stream";
	}
}

/**
 * @brief Output metadata in human-readable text format
 *
 * Format: "TYPE mime WxH N frame(s)"
 * Example: "PNG image/png 1920x1080 1 frame"
 */
void output_metadata_text(mime_type_t mime, uint32_t width, uint32_t height, int frame_count)
{
	const char *type = mime_type_name(mime);
	const char *mime_str = get_mime_string(mime);

	printf("%s %s %ux%u %d %s\n", type, mime_str, width, height, frame_count, frame_count == 1 ? "frame" : "frames");
}

/**
 * @brief Output metadata as JSON (single line, JSONL format)
 *
 * Format: {"type":"TYPE","mime":"MIME","width":W,"height":H,"frames":N}
 * Example: {"type":"PNG","mime":"image/png","width":1920,"height":1080,"frames":1}
 */
void output_metadata_json(mime_type_t mime, uint32_t width, uint32_t height, int frame_count)
{
	const char *type = mime_type_name(mime);
	const char *mime_str = get_mime_string(mime);

	printf("{\"type\":\"%s\",\"mime\":\"%s\",\"width\":%u,\"height\":%u,\"frames\":%d}\n", type, mime_str, width, height, frame_count);
}

#ifdef HAVE_EXIF_READER
/**
 * @brief Output EXIF metadata in human-readable format
 */
void output_exif_text(const struct exif_info_t *exif)
{
	if (exif == NULL) {
		return;
	}

	printf("\n");

	// Camera information
	if (exif->make[0] != '\0' || exif->model[0] != '\0') {
		printf("Camera:\n");
		if (exif->make[0] != '\0' && exif->model[0] != '\0') {
			printf("  Make/Model: %s %s\n", exif->make, exif->model);
		} else if (exif->make[0] != '\0') {
			printf("  Make: %s\n", exif->make);
		} else {
			printf("  Model: %s\n", exif->model);
		}
		if (exif->software[0] != '\0') {
			printf("  Software: %s\n", exif->software);
		}
	}

	// Exposure settings
	if (exif->has_exposure_time || exif->has_f_number || exif->has_iso) {
		printf("\nExposure:\n");
		if (exif->has_exposure_time) {
			if (exif->exposure_time < 1.0) {
				printf("  Shutter Speed: 1/%.0f s\n", 1.0 / exif->exposure_time);
			} else {
				printf("  Shutter Speed: %.2f s\n", exif->exposure_time);
			}
		}
		if (exif->has_f_number) {
			printf("  Aperture: f/%.1f\n", exif->f_number);
		}
		if (exif->has_iso) {
			printf("  ISO: %u\n", exif->iso_speed);
		}
		if (exif->exposure_bias != 0.0) {
			printf("  Exposure Compensation: %+.1f EV\n", exif->exposure_bias);
		}
	}

	// Lens information
	if (exif->lens_model[0] != '\0' || exif->has_focal_length) {
		printf("\nLens:\n");
		if (exif->lens_model[0] != '\0') {
			printf("  Model: %s\n", exif->lens_model);
		}
		if (exif->has_focal_length) {
			printf("  Focal Length: %.0f mm", exif->focal_length);
			if (exif->focal_length_35mm > 0) {
				printf(" (35mm equiv: %.0f mm)", exif->focal_length_35mm);
			}
			printf("\n");
		}
		if (exif->max_aperture > 0) {
			printf("  Max Aperture: f/%.1f\n", exif->max_aperture);
		}
	}

	// Image information
	if (exif->image_width > 0 || exif->orientation > 0 || exif->has_datetime) {
		printf("\nImage:\n");
		if (exif->image_width > 0 && exif->image_height > 0) {
			printf("  Dimensions: %ux%u\n", exif->image_width, exif->image_height);
		}
		if (exif->orientation > 0 && exif->orientation <= 8) {
			const char *orientations[] = { "Normal", "Horizontal flip", "Rotate 180", "Vertical flip", "Transpose", "Rotate 90 CW", "Transverse", "Rotate 270 CW" };
			printf("  Orientation: %s\n", orientations[exif->orientation - 1]);
		}
		if (exif->has_datetime && exif->date_time[0] != '\0') {
			printf("  Date/Time: %s\n", exif->date_time);
		}
	}

	// GPS information
	if (exif->has_gps) {
		printf("\nGPS:\n");
		printf("  Latitude: %.6f%c\n", exif->gps_latitude, exif->gps_latitude_ref);
		printf("  Longitude: %.6f%c\n", exif->gps_longitude, exif->gps_longitude_ref);
		if (exif->gps_altitude != 0.0) {
			printf("  Altitude: %.1f m\n", exif->gps_altitude);
		}
		if (exif->gps_datestamp[0] != '\0') {
			printf("  Date: %s", exif->gps_datestamp);
			if (exif->gps_timestamp[0] != '\0') {
				printf(" %s", exif->gps_timestamp);
			}
			printf("\n");
		}
	}

	// Copyright and artist
	if (exif->artist[0] != '\0' || exif->copyright[0] != '\0') {
		printf("\nRights:\n");
		if (exif->artist[0] != '\0') {
			printf("  Artist: %s\n", exif->artist);
		}
		if (exif->copyright[0] != '\0') {
			printf("  Copyright: %s\n", exif->copyright);
		}
	}

	// Description and comment
	if (exif->description[0] != '\0' || exif->user_comment[0] != '\0') {
		printf("\nAnnotations:\n");
		if (exif->description[0] != '\0') {
			printf("  Description: %s\n", exif->description);
		}
		if (exif->user_comment[0] != '\0') {
			printf("  Comment: %s\n", exif->user_comment);
		}
	}
}

/**
 * @brief Output XMP metadata in human-readable format
 */
void output_xmp_text(const struct xmp_info_t *xmp)
{
	if (xmp == NULL) {
		return;
	}

	// Check if there's any XMP content before printing header
	bool has_content =
	    (xmp->creator[0] != '\0' || xmp->title[0] != '\0' || xmp->description[0] != '\0' || (xmp->has_keywords && xmp->keywords != NULL) || xmp->copyright[0] != '\0' || xmp->has_rating || xmp->label[0] != '\0' || xmp->create_date[0] != '\0' ||
	     xmp->modify_date[0] != '\0' || xmp->city[0] != '\0' || xmp->country[0] != '\0' || xmp->credit[0] != '\0' || xmp->source[0] != '\0');

	if (!has_content) {
		return;
	}

	printf("\nXMP Metadata:\n");

	// Dublin Core
	if (xmp->creator[0] != '\0') {
		printf("  Creator: %s\n", xmp->creator);
	}
	if (xmp->title[0] != '\0') {
		printf("  Title: %s\n", xmp->title);
	}
	if (xmp->description[0] != '\0') {
		printf("  Description: %s\n", xmp->description);
	}
	if (xmp->has_keywords && xmp->keywords != NULL) {
		printf("  Keywords: %s\n", xmp->keywords);
	}
	if (xmp->copyright[0] != '\0') {
		printf("  Copyright: %s\n", xmp->copyright);
	}

	// XMP Basic
	if (xmp->has_rating) {
		printf("  Rating: %d star%s\n", xmp->rating, xmp->rating == 1 ? "" : "s");
	}
	if (xmp->label[0] != '\0') {
		printf("  Label: %s\n", xmp->label);
	}
	if (xmp->create_date[0] != '\0') {
		printf("  Created: %s\n", xmp->create_date);
	}
	if (xmp->modify_date[0] != '\0') {
		printf("  Modified: %s\n", xmp->modify_date);
	}

	// IPTC/Photoshop
	if (xmp->city[0] != '\0' || xmp->country[0] != '\0') {
		printf("  Location: ");
		if (xmp->city[0] != '\0') {
			printf("%s", xmp->city);
			if (xmp->country[0] != '\0') {
				printf(", %s", xmp->country);
			}
		} else {
			printf("%s", xmp->country);
		}
		printf("\n");
	}
	if (xmp->credit[0] != '\0') {
		printf("  Credit: %s\n", xmp->credit);
	}
	if (xmp->source[0] != '\0') {
		printf("  Source: %s\n", xmp->source);
	}
}

/**
 * @brief Helper to escape JSON strings
 */
static void print_json_string(const char *str)
{
	if (str == NULL || str[0] == '\0') {
		printf("null");
		return;
	}

	printf("\"");
	for (const char *p = str; *p != '\0'; p++) {
		switch (*p) {
			case '"': printf("\\\""); break;
			case '\\': printf("\\\\"); break;
			case '\n': printf("\\n"); break;
			case '\r': printf("\\r"); break;
			case '\t': printf("\\t"); break;
			default:
				if (*p < 32) {
					printf("\\u%04x", (unsigned char)*p);
				} else {
					putchar(*p);
				}
				break;
		}
	}
	printf("\"");
}

/**
 * @brief Output EXIF metadata as JSON object
 */
void output_exif_json(const struct exif_info_t *exif, bool first)
{
	if (exif == NULL) {
		return;
	}

	if (!first) {
		printf(",");
	}
	printf("\"exif\":{");

	bool has_field = false;

	// Camera
	if (exif->make[0] != '\0' || exif->model[0] != '\0' || exif->software[0] != '\0') {
		printf("\"camera\":{");
		bool first_cam = true;
		if (exif->make[0] != '\0') {
			printf("\"make\":");
			print_json_string(exif->make);
			first_cam = false;
		}
		if (exif->model[0] != '\0') {
			if (!first_cam) {
				printf(",");
			}
			printf("\"model\":");
			print_json_string(exif->model);
			first_cam = false;
		}
		if (exif->software[0] != '\0') {
			if (!first_cam) {
				printf(",");
			}
			printf("\"software\":");
			print_json_string(exif->software);
		}
		printf("}");
		has_field = true;
	}

	// Exposure
	if (exif->has_exposure_time || exif->has_f_number || exif->has_iso) {
		if (has_field) {
			printf(",");
		}
		printf("\"exposure\":{");
		bool first_exp = true;
		if (exif->has_exposure_time) {
			printf("\"time\":%.6f", exif->exposure_time);
			first_exp = false;
		}
		if (exif->has_f_number) {
			if (!first_exp) {
				printf(",");
			}
			printf("\"aperture\":%.1f", exif->f_number);
			first_exp = false;
		}
		if (exif->has_iso) {
			if (!first_exp) {
				printf(",");
			}
			printf("\"iso\":%u", exif->iso_speed);
			first_exp = false;
		}
		if (exif->exposure_bias != 0.0) {
			if (!first_exp) {
				printf(",");
			}
			printf("\"compensation\":%.1f", exif->exposure_bias);
		}
		printf("}");
		has_field = true;
	}

	// Lens
	if (exif->lens_model[0] != '\0' || exif->has_focal_length) {
		if (has_field) {
			printf(",");
		}
		printf("\"lens\":{");
		bool first_lens = true;
		if (exif->lens_model[0] != '\0') {
			printf("\"model\":");
			print_json_string(exif->lens_model);
			first_lens = false;
		}
		if (exif->has_focal_length) {
			if (!first_lens) {
				printf(",");
			}
			printf("\"focalLength\":%.0f", exif->focal_length);
			if (exif->focal_length_35mm > 0) {
				printf(",\"focalLength35mm\":%.0f", exif->focal_length_35mm);
			}
			first_lens = false;
		}
		if (exif->max_aperture > 0) {
			if (!first_lens) {
				printf(",");
			}
			printf("\"maxAperture\":%.1f", exif->max_aperture);
		}
		printf("}");
		has_field = true;
	}

	// Image
	if (exif->image_width > 0 || exif->orientation > 0 || exif->has_datetime) {
		if (has_field) {
			printf(",");
		}
		printf("\"image\":{");
		bool first_img = true;
		if (exif->image_width > 0) {
			printf("\"width\":%u,\"height\":%u", exif->image_width, exif->image_height);
			first_img = false;
		}
		if (exif->orientation > 0) {
			if (!first_img) {
				printf(",");
			}
			printf("\"orientation\":%u", exif->orientation);
			first_img = false;
		}
		if (exif->has_datetime && exif->date_time[0] != '\0') {
			if (!first_img) {
				printf(",");
			}
			printf("\"dateTime\":");
			print_json_string(exif->date_time);
		}
		printf("}");
		has_field = true;
	}

	// GPS
	if (exif->has_gps) {
		if (has_field) {
			printf(",");
		}
		printf("\"gps\":{");
		printf("\"latitude\":%.6f,\"longitude\":%.6f", exif->gps_latitude, exif->gps_longitude);
		if (exif->gps_altitude != 0.0) {
			printf(",\"altitude\":%.1f", exif->gps_altitude);
		}
		printf("}");
		has_field = true;
	}

	// Copyright
	if (exif->artist[0] != '\0' || exif->copyright[0] != '\0') {
		if (has_field) {
			printf(",");
		}
		printf("\"rights\":{");
		bool first_rights = true;
		if (exif->artist[0] != '\0') {
			printf("\"artist\":");
			print_json_string(exif->artist);
			first_rights = false;
		}
		if (exif->copyright[0] != '\0') {
			if (!first_rights) {
				printf(",");
			}
			printf("\"copyright\":");
			print_json_string(exif->copyright);
		}
		printf("}");
		has_field = true;
	}

	// Description
	if (exif->description[0] != '\0' || exif->user_comment[0] != '\0') {
		if (has_field) {
			printf(",");
		}
		printf("\"annotations\":{");
		bool first_ann = true;
		if (exif->description[0] != '\0') {
			printf("\"description\":");
			print_json_string(exif->description);
			first_ann = false;
		}
		if (exif->user_comment[0] != '\0') {
			if (!first_ann) {
				printf(",");
			}
			printf("\"comment\":");
			print_json_string(exif->user_comment);
		}
		printf("}");
	}

	printf("}");
}

/**
 * @brief Output XMP metadata as JSON object
 */
void output_xmp_json(const struct xmp_info_t *xmp, bool first)
{
	if (xmp == NULL) {
		return;
	}

	if (!first) {
		printf(",");
	}
	printf("\"xmp\":{");

	bool has_field = false;

	// Dublin Core
	if (xmp->creator[0] != '\0') {
		printf("\"creator\":");
		print_json_string(xmp->creator);
		has_field = true;
	}
	if (xmp->title[0] != '\0') {
		if (has_field) {
			printf(",");
		}
		printf("\"title\":");
		print_json_string(xmp->title);
		has_field = true;
	}
	if (xmp->description[0] != '\0') {
		if (has_field) {
			printf(",");
		}
		printf("\"description\":");
		print_json_string(xmp->description);
		has_field = true;
	}
	if (xmp->has_keywords && xmp->keywords != NULL) {
		if (has_field) {
			printf(",");
		}
		printf("\"keywords\":");
		print_json_string(xmp->keywords);
		has_field = true;
	}
	if (xmp->copyright[0] != '\0') {
		if (has_field) {
			printf(",");
		}
		printf("\"copyright\":");
		print_json_string(xmp->copyright);
		has_field = true;
	}

	// XMP Basic
	if (xmp->has_rating) {
		if (has_field) {
			printf(",");
		}
		printf("\"rating\":%d", xmp->rating);
		has_field = true;
	}
	if (xmp->label[0] != '\0') {
		if (has_field) {
			printf(",");
		}
		printf("\"label\":");
		print_json_string(xmp->label);
		has_field = true;
	}
	if (xmp->create_date[0] != '\0') {
		if (has_field) {
			printf(",");
		}
		printf("\"createDate\":");
		print_json_string(xmp->create_date);
		has_field = true;
	}
	if (xmp->modify_date[0] != '\0') {
		if (has_field) {
			printf(",");
		}
		printf("\"modifyDate\":");
		print_json_string(xmp->modify_date);
		has_field = true;
	}

	// Location
	if (xmp->city[0] != '\0' || xmp->country[0] != '\0') {
		if (has_field) {
			printf(",");
		}
		printf("\"location\":{");
		bool first_loc = true;
		if (xmp->city[0] != '\0') {
			printf("\"city\":");
			print_json_string(xmp->city);
			first_loc = false;
		}
		if (xmp->country[0] != '\0') {
			if (!first_loc) {
				printf(",");
			}
			printf("\"country\":");
			print_json_string(xmp->country);
		}
		printf("}");
		has_field = true;
	}

	// IPTC
	if (xmp->credit[0] != '\0') {
		if (has_field) {
			printf(",");
		}
		printf("\"credit\":");
		print_json_string(xmp->credit);
		has_field = true;
	}
	if (xmp->source[0] != '\0') {
		if (has_field) {
			printf(",");
		}
		printf("\"source\":");
		print_json_string(xmp->source);
		has_field = true;
	}

	printf("}");
}
#endif /* HAVE_EXIF_READER */
