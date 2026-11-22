/**
 * @file metadata.h
 * @brief libimg2 EXIF / XMP metadata (attached to decoded images, or parsed directly)
 */

#ifndef IMG2_METADATA_H
#define IMG2_METADATA_H

#include <img2/common.h>
#include <img2/image.h>

IMG2_BEGIN_DECLS

/**
 * @brief EXIF metadata
 *
 * Camera settings, exposure info, GPS coordinates and lens data.
 */
struct img2_exif {
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
	uint32_t iso_speed; /**< ISO speed (ISOSpeedRatings, or the EXIF 2.3 LONG sensitivity tags above 65535) */
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
	bool has_exposure_time; /**< exposure_time is valid */
	bool has_f_number; /**< f_number is valid */
	bool has_iso; /**< iso_speed is valid */
	bool has_focal_length; /**< focal_length is valid */
	bool has_datetime; /**< date_time is valid */
};

/**
 * @brief XMP metadata
 *
 * Dublin Core, XMP Basic and IPTC/Photoshop fields.
 */
struct img2_xmp {
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
	bool has_rating; /**< rating is valid */
	bool has_keywords; /**< keywords is valid */
};

/**
 * @brief Initialise an EXIF struct with defaults
 *
 * @param exif EXIF struct
 */
void img2_exif_init(img2_exif_t *exif);

/**
 * @brief Free the contents of an EXIF struct (not the struct itself)
 *
 * @param exif EXIF struct
 */
void img2_exif_clear(img2_exif_t *exif);

/**
 * @brief Initialise an XMP struct with defaults
 *
 * @param xmp XMP struct
 */
void img2_xmp_init(img2_xmp_t *xmp);

/**
 * @brief Free the contents of an XMP struct (not the struct itself)
 *
 * @param xmp XMP struct
 */
void img2_xmp_clear(img2_xmp_t *xmp);

/**
 * @brief Parse EXIF metadata from a JPEG file buffer (APP1 "Exif" segment)
 *
 * @param exif Output EXIF struct
 * @param jpeg JPEG data
 * @param len  Length of jpeg in bytes
 * @return IMG2_OK; IMG2_E_ARG (NULL argument); IMG2_E_NOMEM; IMG2_E_FORMAT (malformed or no EXIF)
 */
img2_status_t img2_exif_parse_jpeg(img2_exif_t *exif, const uint8_t *jpeg, size_t len);

/**
 * @brief Parse EXIF metadata from raw TIFF/IFD data (starting with the TIFF header)
 *
 * @param exif Output EXIF struct
 * @param tiff TIFF data
 * @param len  Length of tiff in bytes
 * @return IMG2_OK; IMG2_E_ARG (NULL argument); IMG2_E_NOMEM; IMG2_E_FORMAT (invalid TIFF structure)
 */
img2_status_t img2_exif_parse_tiff(img2_exif_t *exif, const uint8_t *tiff, size_t len);

/**
 * @brief Parse XMP metadata from a JPEG file buffer (APP1 XMP segment)
 *
 * @param xmp  Output XMP struct
 * @param jpeg JPEG data
 * @param len  Length of jpeg in bytes
 * @return IMG2_OK; IMG2_E_ARG (NULL argument); IMG2_E_NOMEM; IMG2_E_FORMAT (malformed or no XMP)
 */
img2_status_t img2_xmp_parse_jpeg(img2_xmp_t *xmp, const uint8_t *jpeg, size_t len);

/**
 * @brief Parse XMP metadata from a raw XML packet
 *
 * @param xmp Output XMP struct
 * @param xml XML text
 * @param len Length of xml in bytes
 * @return IMG2_OK; IMG2_E_ARG (NULL argument); IMG2_E_NOMEM; IMG2_E_FORMAT (invalid XML structure)
 */
img2_status_t img2_xmp_parse_xml(img2_xmp_t *xmp, const char *xml, size_t len);

IMG2_END_DECLS

#endif /* IMG2_METADATA_H */
