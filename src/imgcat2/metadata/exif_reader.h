/**
 * @file exif_reader.h
 * @brief Pure C EXIF/XMP metadata parser (TinyEXIF port)
 *
 * Extracts EXIF and XMP metadata from JPEG images.
 * Read-only implementation with zero external dependencies.
 */

#ifndef EXIF_READER_H
#define EXIF_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief EXIF metadata information
 *
 * Contains comprehensive EXIF data extracted from JPEG files including
 * camera settings, exposure info, GPS coordinates, and lens data.
 */
typedef struct exif_info_t {
	/* Camera make/model */
	char make[64]; /**< Camera manufacturer (e.g., "Canon") */
	char model[64]; /**< Camera model (e.g., "EOS 5D Mark IV") */
	char software[64]; /**< Software used to process image */
	char date_time[32]; /**< Image capture date/time (YYYY:MM:DD HH:MM:SS) */

	/* Image dimensions */
	uint32_t image_width; /**< Image width in pixels */
	uint32_t image_height; /**< Image height in pixels */
	uint16_t orientation; /**< Image orientation (1-8, see EXIF spec) */

	/* Exposure settings */
	double exposure_time; /**< Exposure time in seconds */
	double f_number; /**< F-number (aperture) */
	uint16_t iso_speed; /**< ISO speed rating */
	double shutter_speed; /**< Shutter speed value (APEX) */
	double aperture_value; /**< Aperture value (APEX) */
	double brightness_value; /**< Brightness value (APEX) */
	double exposure_bias; /**< Exposure bias/compensation in EV */

	/* Camera settings */
	uint16_t metering_mode; /**< Metering mode (0-6, see EXIF spec) */
	uint16_t light_source; /**< Light source / white balance */
	uint16_t flash; /**< Flash status (bit field) */
	uint16_t flash_mode; /**< Flash firing mode */

	/* Lens information */
	double focal_length; /**< Focal length in mm */
	double focal_length_35mm; /**< Focal length in 35mm equivalent */
	char lens_make[64]; /**< Lens manufacturer */
	char lens_model[128]; /**< Lens model */
	double min_focal_length; /**< Minimum focal length (zoom lenses) */
	double max_focal_length; /**< Maximum focal length (zoom lenses) */
	double max_aperture; /**< Maximum aperture at min focal length */

	/* GPS coordinates */
	bool has_gps; /**< True if GPS data is present */
	double gps_latitude; /**< Latitude in decimal degrees */
	double gps_longitude; /**< Longitude in decimal degrees */
	double gps_altitude; /**< Altitude in meters */
	char gps_latitude_ref; /**< 'N' or 'S' */
	char gps_longitude_ref; /**< 'E' or 'W' */
	char gps_altitude_ref; /**< 0 = above sea level, 1 = below */
	char gps_datestamp[32]; /**< GPS date stamp (YYYY:MM:DD) */
	char gps_timestamp[32]; /**< GPS time stamp (HH:MM:SS) */

	/* Additional metadata */
	char description[512]; /**< Image description */
	char copyright[512]; /**< Copyright notice */
	char artist[256]; /**< Artist/photographer name */
	char user_comment[512]; /**< User comment (can be Unicode) */

	/* Technical details */
	uint16_t compression; /**< Compression scheme */
	uint16_t bits_per_sample; /**< Bits per sample */
	uint16_t photometric_interpretation; /**< Pixel composition */
	double x_resolution; /**< Horizontal resolution */
	double y_resolution; /**< Vertical resolution */
	uint16_t resolution_unit; /**< Resolution unit (1=none, 2=inches, 3=cm) */

	/* Validity flags (for optional fields) */
	bool has_exposure_time;
	bool has_f_number;
	bool has_iso;
	bool has_focal_length;
	bool has_datetime;
} exif_info_t;

/**
 * @brief XMP metadata information
 *
 * Contains XMP (Extensible Metadata Platform) data including
 * Dublin Core, IPTC, and application-specific metadata.
 */
typedef struct xmp_info_t {
	/* Dublin Core */
	char creator[256]; /**< Creator/author (dc:creator) */
	char copyright[512]; /**< Copyright notice (dc:rights) */
	char description[512]; /**< Description (dc:description) */
	char *keywords; /**< Comma-separated keywords (dc:subject), dynamically allocated */
	char title[256]; /**< Title (dc:title) */

	/* XMP Basic */
	int rating; /**< Star rating 0-5 (xmp:Rating) */
	char create_date[32]; /**< Creation date (xmp:CreateDate) */
	char modify_date[32]; /**< Modification date (xmp:ModifyDate) */
	char label[64]; /**< Color label (xmp:Label) */

	/* IPTC/Photoshop */
	char city[128]; /**< City (photoshop:City) */
	char country[128]; /**< Country (photoshop:Country) */
	char credit[256]; /**< Credit line (photoshop:Credit) */
	char source[256]; /**< Source (photoshop:Source) */

	/* Validity flags */
	bool has_rating;
	bool has_keywords;
} xmp_info_t;

/**
 * @brief Initialize EXIF info structure with defaults
 *
 * @param exif EXIF info structure to initialize
 */
void exif_info_init(exif_info_t *exif);

/**
 * @brief Initialize XMP info structure with defaults
 *
 * @param xmp XMP info structure to initialize
 */
void xmp_info_init(xmp_info_t *xmp);

/**
 * @brief Parse EXIF metadata from JPEG data
 *
 * Extracts all available EXIF metadata from a JPEG file buffer.
 * Searches for APP1 segment with "Exif\0\0" marker and parses
 * TIFF/IFD structure.
 *
 * @param exif Output structure for EXIF data
 * @param jpeg_data JPEG file data buffer
 * @param len Length of jpeg_data in bytes
 * @return 0 on success, -1 on error or no EXIF data found
 */
int parse_exif(exif_info_t *exif, const uint8_t *jpeg_data, size_t len);

/**
 * @brief Parse XMP metadata from JPEG data
 *
 * Extracts XMP metadata from a JPEG file buffer. Searches for APP1
 * segment with "http://ns.adobe.com/xap/1.0/\0" marker and parses
 * the embedded XML using xml_parser.
 *
 * @param xmp Output structure for XMP data
 * @param jpeg_data JPEG file data buffer
 * @param len Length of jpeg_data in bytes
 * @return 0 on success, -1 on error or no XMP data found
 */
int parse_xmp(xmp_info_t *xmp, const uint8_t *jpeg_data, size_t len);

/**
 * @brief Free dynamically allocated fields in EXIF info
 *
 * @param exif EXIF info structure to clean up
 */
void exif_info_free(exif_info_t *exif);

/**
 * @brief Free dynamically allocated fields in XMP info
 *
 * @param xmp XMP info structure to clean up
 */
void xmp_info_free(xmp_info_t *xmp);

#endif /* EXIF_READER_H */
