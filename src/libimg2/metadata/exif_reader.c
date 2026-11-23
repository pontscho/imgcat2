/**
 * @file exif_reader.c
 * @brief Pure C EXIF/XMP metadata parser implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exif_reader.h"
#include "xml_parser.h"

/* ========== EXIF Tag Constants ========== */

/* IFD0 Tags (main image) */
#define TAG_MAKE                       0x010F
#define TAG_MODEL                      0x0110
#define TAG_ORIENTATION                0x0112
#define TAG_SOFTWARE                   0x0131
#define TAG_DATETIME                   0x0132
#define TAG_ARTIST                     0x013B
#define TAG_COPYRIGHT                  0x8298
#define TAG_EXIF_OFFSET                0x8769
#define TAG_GPS_OFFSET                 0x8825
#define TAG_IMAGE_WIDTH                0x0100
#define TAG_IMAGE_HEIGHT               0x0101
#define TAG_COMPRESSION                0x0103
#define TAG_BITS_PER_SAMPLE            0x0102
#define TAG_PHOTOMETRIC_INTERPRETATION 0x0106
#define TAG_X_RESOLUTION               0x011A
#define TAG_Y_RESOLUTION               0x011B
#define TAG_RESOLUTION_UNIT            0x0128
#define TAG_IMAGE_DESCRIPTION          0x010E

/* ExifIFD Tags (EXIF-specific) */
#define TAG_EXPOSURE_TIME      0x829A
#define TAG_F_NUMBER           0x829D
#define TAG_ISO_SPEED          0x8827
#define TAG_SENSITIVITY_TYPE   0x8830 /* EXIF 2.3: which of the three LONG tags below hold the ISO */
#define TAG_SOS                0x8831 /* StandardOutputSensitivity */
#define TAG_REI                0x8832 /* RecommendedExposureIndex */
#define TAG_ISO_SPEED_LONG     0x8833 /* ISOSpeed */
#define TAG_DATETIME_ORIGINAL  0x9003
#define TAG_DATETIME_DIGITIZED 0x9004
#define TAG_SHUTTER_SPEED      0x9201
#define TAG_APERTURE_VALUE     0x9202
#define TAG_BRIGHTNESS_VALUE   0x9203
#define TAG_EXPOSURE_BIAS      0x9204
#define TAG_FOCAL_LENGTH       0x920A
#define TAG_USER_COMMENT       0x9286
#define TAG_FLASH              0x9209
#define TAG_METERING_MODE      0x9207
#define TAG_LIGHT_SOURCE       0x9208
#define TAG_FOCAL_LENGTH_35MM  0xA405
#define TAG_LENS_MAKE          0xA433
#define TAG_LENS_MODEL         0xA434

/* GPS Tags */
#define TAG_GPS_LATITUDE_REF  0x0001
#define TAG_GPS_LATITUDE      0x0002
#define TAG_GPS_LONGITUDE_REF 0x0003
#define TAG_GPS_LONGITUDE     0x0004
#define TAG_GPS_ALTITUDE_REF  0x0005
#define TAG_GPS_ALTITUDE      0x0006
#define TAG_GPS_TIMESTAMP     0x0007
#define TAG_GPS_DATESTAMP     0x001D

/* TIFF Data Types */
#define TYPE_BYTE      1
#define TYPE_ASCII     2
#define TYPE_SHORT     3
#define TYPE_LONG      4
#define TYPE_RATIONAL  5
#define TYPE_SBYTE     6
#define TYPE_UNDEFINED 7
#define TYPE_SSHORT    8
#define TYPE_SLONG     9
#define TYPE_SRATIONAL 10

/* ========== Byte Order Utilities ========== */

typedef enum {
	BYTE_ORDER_LITTLE_ENDIAN,
	BYTE_ORDER_BIG_ENDIAN
} byte_order_t;

static uint16_t read_u16(const uint8_t *data, byte_order_t order)
{
	if (order == BYTE_ORDER_LITTLE_ENDIAN) {
		return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
	} else {
		return ((uint16_t)data[0] << 8) | (uint16_t)data[1];
	}
}

static uint32_t read_u32(const uint8_t *data, byte_order_t order)
{
	if (order == BYTE_ORDER_LITTLE_ENDIAN) {
		return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
	} else {
		return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | (uint32_t)data[3];
	}
}

static double read_rational(const uint8_t *data, byte_order_t order)
{
	uint32_t numerator = read_u32(data, order);
	uint32_t denominator = read_u32(data + 4, order);
	if (denominator == 0) {
		return 0.0;
	}
	return (double)numerator / (double)denominator;
}

/* ========== JPEG Segment Scanner ========== */

/**
 * @brief Find APP1 segment with given marker
 *
 * @param jpeg_data JPEG file data
 * @param len Length of JPEG data
 * @param marker Marker string to search for (e.g., "Exif\0\0")
 * @param marker_len Length of marker
 * @param segment_data Output: pointer to segment data (after marker)
 * @param segment_len Output: length of segment data
 * @return 0 if found, -1 if not found
 */
static int find_app1_segment(const uint8_t *jpeg_data, size_t len, const char *marker, size_t marker_len, const uint8_t **segment_data, size_t *segment_len)
{
	if (!jpeg_data || len < 4) {
		return -1;
	}

	/* Check JPEG SOI marker */
	if (jpeg_data[0] != 0xFF || jpeg_data[1] != 0xD8) {
		return -1;
	}

	size_t pos = 2;
	while (pos + 4 < len) {
		/* Check for marker */
		if (jpeg_data[pos] != 0xFF) {
			return -1; /* Invalid JPEG */
		}

		uint8_t marker_type = jpeg_data[pos + 1];
		pos += 2;

		/* Skip padding bytes */
		while (marker_type == 0xFF && pos < len) {
			marker_type = jpeg_data[pos];
			pos++;
		}

		/* Check if this is an APP1 marker (0xE1) */
		if (marker_type == 0xE1) {
			if (pos + 2 > len) {
				return -1;
			}

			/* Read segment length (big endian) */
			uint16_t seg_len = ((uint16_t)jpeg_data[pos] << 8) | (uint16_t)jpeg_data[pos + 1];
			pos += 2;

			if (pos + seg_len - 2 > len) {
				return -1;
			}

			/* Check if this segment has the marker we're looking for */
			if (seg_len >= marker_len + 2 && memcmp(jpeg_data + pos, marker, marker_len) == 0) {
				*segment_data = jpeg_data + pos + marker_len;
				*segment_len = seg_len - marker_len - 2;
				return 0;
			}

			/* Skip this segment */
			pos += seg_len - 2;

		} else if (marker_type == 0xDA) {
			/* SOS (Start of Scan) - end of metadata */
			return -1;

		} else if (marker_type >= 0xD0 && marker_type <= 0xD9) {
			/* Standalone markers (no length field) */
			continue;

		} else {
			/* Other markers with length field */
			if (pos + 2 > len) {
				return -1;
			}
			uint16_t seg_len = ((uint16_t)jpeg_data[pos] << 8) | (uint16_t)jpeg_data[pos + 1];
			pos += seg_len;
		}
	}

	return -1;
}

img2_status_t img2_jpeg_find_app1(const uint8_t *jpeg, size_t len, const char *id, size_t id_len, const uint8_t **payload, size_t *payload_len)
{
	return find_app1_segment(jpeg, len, id, id_len, payload, payload_len) == 0 ? IMG2_OK : IMG2_E_FORMAT;
}

/* ========== IFD Parser ========== */

typedef struct {
	const uint8_t *tiff_data;
	size_t tiff_len;
	byte_order_t byte_order;
	bool exif_ifd_done; /* IFD0's ExifIFD pointer followed (at most once) */
	bool gps_ifd_done; /* IFD0's GPS IFD pointer followed (at most once) */
	/* ISO sources, combined by resolve_iso() after the walk */
	bool iso_short_seen; /* ISOSpeedRatings (SHORT) read */
	uint16_t iso_short; /* its value; 65535 means "65535 or more, see the LONG tags" */
	uint16_t sensitivity_type; /* SensitivityType; 0 (Unknown) when absent */
	uint32_t sos, rei, iso_long; /* StandardOutputSensitivity, RecommendedExposureIndex, ISOSpeed; 0 = absent */
} tiff_context_t;

/* the LONG ISO tags SensitivityType permits (EXIF 2.3 table) */
#define ISO_SOS  1u
#define ISO_REI  2u
#define ISO_LONG 4u

static const uint8_t k_sensitivity_tags[8] = {
	ISO_LONG | ISO_REI | ISO_SOS, /* 0: Unknown (or absent): any of them */
	ISO_SOS,                      /* 1: SOS */
	ISO_REI,                      /* 2: REI */
	ISO_LONG,                     /* 3: ISO speed */
	ISO_SOS | ISO_REI,            /* 4: SOS and REI */
	ISO_SOS | ISO_LONG,           /* 5: SOS and ISO speed */
	ISO_REI | ISO_LONG,           /* 6: REI and ISO speed */
	ISO_LONG | ISO_REI | ISO_SOS, /* 7: all three */
};

/**
 * @brief Record a LONG ISO tag (a value of 0 is ignored: it never overrides)
 */
static void store_iso_long(tiff_context_t *ctx, uint16_t tag, uint32_t value)
{
	if (value == 0) {
		return;
	}
	if (tag == TAG_SOS) {
		ctx->sos = value;
	} else if (tag == TAG_REI) {
		ctx->rei = value;
	} else {
		ctx->iso_long = value;
	}
}

/**
 * @brief Combine the ISO tags (EXIF 2.3)
 *
 * ISOSpeedRatings is a SHORT: when it is absent or 65535 ("see the other tags"), the LONG tag SensitivityType
 * selects is used, ISOSpeed first, then RecommendedExposureIndex, then StandardOutputSensitivity. Without a valid
 * SensitivityType (absent, 0 = Unknown, or above 7) the first present of the three in that order is taken.
 */
static void resolve_iso(const tiff_context_t *ctx, img2_exif_t *exif)
{
	if (ctx->iso_short_seen && ctx->iso_short != 65535) {
		return;
	}
	unsigned allowed = k_sensitivity_tags[ctx->sensitivity_type <= 7 ? ctx->sensitivity_type : 0];
	uint32_t value = 0;
	if (allowed & ISO_LONG) {
		value = ctx->iso_long;
	}
	if (value == 0 && (allowed & ISO_REI)) {
		value = ctx->rei;
	}
	if (value == 0 && (allowed & ISO_SOS)) {
		value = ctx->sos;
	}
	if (value != 0) {
		exif->iso_speed = value;
		exif->has_iso = true;
	}
}

/**
 * @brief Parse IFD entry and populate EXIF info
 */
static void parse_ifd_entry(tiff_context_t *ctx, img2_exif_t *exif, uint16_t tag, uint16_t type, uint32_t count, uint32_t value_offset)
{
	const uint8_t *tiff = ctx->tiff_data;
	size_t tiff_len = ctx->tiff_len;
	byte_order_t order = ctx->byte_order;

	/* Calculate actual data pointer */
	const uint8_t *data_ptr = NULL;
	size_t type_size = 0;

	/* Determine type size */
	switch (type) {
		case TYPE_BYTE:
		case TYPE_SBYTE:
		case TYPE_UNDEFINED: type_size = 1; break;
		case TYPE_ASCII: type_size = 1; break;
		case TYPE_SHORT:
		case TYPE_SSHORT: type_size = 2; break;
		case TYPE_LONG:
		case TYPE_SLONG: type_size = 4; break;
		case TYPE_RATIONAL:
		case TYPE_SRATIONAL: type_size = 8; break;
		default: return;
	}

	size_t data_size = type_size * count;

	/* If data fits in 4 bytes, it's stored in value_offset itself */
	if (data_size <= 4) {
		/* Data is inline in the value_offset field */
		uint8_t inline_data[4];
		if (order == BYTE_ORDER_LITTLE_ENDIAN) {
			inline_data[0] = value_offset & 0xFF;
			inline_data[1] = (value_offset >> 8) & 0xFF;
			inline_data[2] = (value_offset >> 16) & 0xFF;
			inline_data[3] = (value_offset >> 24) & 0xFF;
		} else {
			inline_data[0] = (value_offset >> 24) & 0xFF;
			inline_data[1] = (value_offset >> 16) & 0xFF;
			inline_data[2] = (value_offset >> 8) & 0xFF;
			inline_data[3] = value_offset & 0xFF;
		}
		data_ptr = inline_data;

		/* Process inline data immediately */
		switch (tag) {
			case TAG_ORIENTATION:
				if (type == TYPE_SHORT) {
					exif->orientation = read_u16(data_ptr, order);
				}
				break;

			case TAG_COMPRESSION:
				if (type == TYPE_SHORT) {
					exif->compression = read_u16(data_ptr, order);
				}
				break;

			case TAG_BITS_PER_SAMPLE:
				if (type == TYPE_SHORT) {
					exif->bits_per_sample = read_u16(data_ptr, order);
				}
				break;
			case TAG_PHOTOMETRIC_INTERPRETATION:
				if (type == TYPE_SHORT) {
					exif->photometric_interpretation = read_u16(data_ptr, order);
				}
				break;

			case TAG_RESOLUTION_UNIT:
				if (type == TYPE_SHORT) {
					exif->resolution_unit = read_u16(data_ptr, order);
				}
				break;

			case TAG_ISO_SPEED:
				if (type == TYPE_SHORT) {
					exif->iso_speed = read_u16(data_ptr, order);
					exif->has_iso = true;
					ctx->iso_short_seen = true;
					ctx->iso_short = read_u16(data_ptr, order);
				}
				break;

			case TAG_SENSITIVITY_TYPE:
				if (type == TYPE_SHORT && count >= 1) {
					ctx->sensitivity_type = read_u16(data_ptr, order);
				}
				break;

			case TAG_SOS:
			case TAG_REI:
			case TAG_ISO_SPEED_LONG:
				if (type == TYPE_LONG && count == 1) {
					store_iso_long(ctx, tag, read_u32(data_ptr, order));
				}
				break;

			case TAG_FLASH:
				if (type == TYPE_SHORT) {
					exif->flash = read_u16(data_ptr, order);
				}
				break;

			case TAG_METERING_MODE:
				if (type == TYPE_SHORT) {
					exif->metering_mode = read_u16(data_ptr, order);
				}
				break;

			case TAG_LIGHT_SOURCE:
				if (type == TYPE_SHORT) {
					exif->light_source = read_u16(data_ptr, order);
				}
				break;

			case TAG_IMAGE_WIDTH:
				if (type == TYPE_SHORT) {
					exif->image_width = read_u16(data_ptr, order);
				} else if (type == TYPE_LONG) {
					exif->image_width = value_offset;
				}
				break;

			case TAG_IMAGE_HEIGHT:
				if (type == TYPE_SHORT) {
					exif->image_height = read_u16(data_ptr, order);
				} else if (type == TYPE_LONG) {
					exif->image_height = value_offset;
				}
				break;
		}
		return;
	}

	/* Data is referenced by offset */
	if (value_offset + data_size > tiff_len) {
		return; /* Out of bounds */
	}
	data_ptr = tiff + value_offset;

	/* Parse tag-specific data */
	switch (tag) {
		case TAG_MAKE:
			if (type == TYPE_ASCII && count < sizeof(exif->make)) {
				memcpy(exif->make, data_ptr, count);
				exif->make[count] = '\0';
				/* Trim trailing spaces/nulls */
				for (int i = count - 1; i >= 0 && (exif->make[i] == ' ' || exif->make[i] == '\0'); i--) {
					exif->make[i] = '\0';
				}
			}
			break;

		case TAG_MODEL:
			if (type == TYPE_ASCII && count < sizeof(exif->model)) {
				memcpy(exif->model, data_ptr, count);
				exif->model[count] = '\0';
				for (int i = count - 1; i >= 0 && (exif->model[i] == ' ' || exif->model[i] == '\0'); i--) {
					exif->model[i] = '\0';
				}
			}
			break;

		case TAG_SOFTWARE:
			if (type == TYPE_ASCII && count < sizeof(exif->software)) {
				memcpy(exif->software, data_ptr, count);
				exif->software[count] = '\0';
			}
			break;

		case TAG_DATETIME:
		case TAG_DATETIME_ORIGINAL:
			if (type == TYPE_ASCII && count < sizeof(exif->date_time)) {
				memcpy(exif->date_time, data_ptr, count);
				exif->date_time[count] = '\0';
				exif->has_datetime = true;
			}
			break;

		case TAG_ARTIST:
			if (type == TYPE_ASCII && count < sizeof(exif->artist)) {
				memcpy(exif->artist, data_ptr, count);
				exif->artist[count] = '\0';
			}
			break;

		case TAG_COPYRIGHT:
			if (type == TYPE_ASCII && count < sizeof(exif->copyright)) {
				memcpy(exif->copyright, data_ptr, count);
				exif->copyright[count] = '\0';
			}
			break;

		case TAG_IMAGE_DESCRIPTION:
			if (type == TYPE_ASCII && count < sizeof(exif->description)) {
				memcpy(exif->description, data_ptr, count);
				exif->description[count] = '\0';
			}
			break;

		case TAG_USER_COMMENT:
			if (count < sizeof(exif->user_comment)) {
				/* User comment may have encoding prefix, skip it */
				const char *comment_start = (const char *)data_ptr;
				if (count > 8) {
					comment_start += 8; /* Skip encoding identifier */
					count -= 8;
				}
				memcpy(exif->user_comment, comment_start, count);
				exif->user_comment[count] = '\0';
			}
			break;

		case TAG_EXPOSURE_TIME:
			if (type == TYPE_RATIONAL) {
				exif->exposure_time = read_rational(data_ptr, order);
				exif->has_exposure_time = true;
			}
			break;

		case TAG_F_NUMBER:
			if (type == TYPE_RATIONAL) {
				exif->f_number = read_rational(data_ptr, order);
				exif->has_f_number = true;
			}
			break;

		case TAG_SHUTTER_SPEED:
			if (type == TYPE_SRATIONAL) {
				exif->shutter_speed = read_rational(data_ptr, order);
			}
			break;

		case TAG_APERTURE_VALUE:
			if (type == TYPE_RATIONAL) {
				exif->aperture_value = read_rational(data_ptr, order);
			}
			break;

		case TAG_BRIGHTNESS_VALUE:
			if (type == TYPE_SRATIONAL) {
				exif->brightness_value = read_rational(data_ptr, order);
			}
			break;

		case TAG_EXPOSURE_BIAS:
			if (type == TYPE_SRATIONAL) {
				exif->exposure_bias = read_rational(data_ptr, order);
			}
			break;

		case TAG_FOCAL_LENGTH:
			if (type == TYPE_RATIONAL) {
				exif->focal_length = read_rational(data_ptr, order);
				exif->has_focal_length = true;
			}
			break;

		case TAG_FOCAL_LENGTH_35MM:
			if (type == TYPE_SHORT) {
				exif->focal_length_35mm = read_u16(data_ptr, order);
			}
			break;

		case TAG_SOS:
		case TAG_REI:
		case TAG_ISO_SPEED_LONG:
			/* count > 1 (a single LONG is inline): the first value, inside the bounds checked above */
			if (type == TYPE_LONG) {
				store_iso_long(ctx, tag, read_u32(data_ptr, order));
			}
			break;

		case TAG_LENS_MAKE:
			if (type == TYPE_ASCII && count < sizeof(exif->lens_make)) {
				memcpy(exif->lens_make, data_ptr, count);
				exif->lens_make[count] = '\0';
			}
			break;

		case TAG_LENS_MODEL:
			if (type == TYPE_ASCII && count < sizeof(exif->lens_model)) {
				memcpy(exif->lens_model, data_ptr, count);
				exif->lens_model[count] = '\0';
			}
			break;

		case TAG_X_RESOLUTION:
			if (type == TYPE_RATIONAL) {
				exif->x_resolution = read_rational(data_ptr, order);
			}
			break;

		case TAG_Y_RESOLUTION:
			if (type == TYPE_RATIONAL) {
				exif->y_resolution = read_rational(data_ptr, order);
			}
			break;
	}
}

/**
 * @brief Parse GPS IFD entry
 */
static void parse_gps_entry(tiff_context_t *ctx, img2_exif_t *exif, uint16_t tag, uint16_t type, uint32_t count, uint32_t value_offset)
{
	const uint8_t *tiff = ctx->tiff_data;
	size_t tiff_len = ctx->tiff_len;
	byte_order_t order = ctx->byte_order;

	exif->has_gps = true;

	switch (tag) {
		case TAG_GPS_LATITUDE_REF:
			if (type == TYPE_ASCII && count >= 1) {
				/* ASCII data <=4 bytes is stored inline in value_offset */
				if (count <= 4) {
					if (order == BYTE_ORDER_LITTLE_ENDIAN) {
						exif->gps_latitude_ref = value_offset & 0xFF;
					} else {
						exif->gps_latitude_ref = (value_offset >> 24) & 0xFF;
					}

				} else if ((size_t)value_offset + 1 <= tiff_len) { /* size_t: no uint32 wrap-around */
					exif->gps_latitude_ref = tiff[value_offset];
				}
			}
			break;

		case TAG_GPS_LONGITUDE_REF:
			if (type == TYPE_ASCII && count >= 1) {
				/* ASCII data <=4 bytes is stored inline in value_offset */
				if (count <= 4) {
					if (order == BYTE_ORDER_LITTLE_ENDIAN) {
						exif->gps_longitude_ref = value_offset & 0xFF;
					} else {
						exif->gps_longitude_ref = (value_offset >> 24) & 0xFF;
					}

				} else if ((size_t)value_offset + 1 <= tiff_len) {
					exif->gps_longitude_ref = tiff[value_offset];
				}
			}
			break;

		case TAG_GPS_LATITUDE:
			if (type == TYPE_RATIONAL && count == 3 && (size_t)value_offset + 24 <= tiff_len) {
				const uint8_t *data = tiff + value_offset;
				double degrees = read_rational(data, order);
				double minutes = read_rational(data + 8, order);
				double seconds = read_rational(data + 16, order);
				exif->gps_latitude = degrees + minutes / 60.0 + seconds / 3600.0;
				if (exif->gps_latitude_ref == 'S') {
					exif->gps_latitude = -exif->gps_latitude;
				}
			}
			break;

		case TAG_GPS_LONGITUDE:
			if (type == TYPE_RATIONAL && count == 3 && (size_t)value_offset + 24 <= tiff_len) {
				const uint8_t *data = tiff + value_offset;
				double degrees = read_rational(data, order);
				double minutes = read_rational(data + 8, order);
				double seconds = read_rational(data + 16, order);
				exif->gps_longitude = degrees + minutes / 60.0 + seconds / 3600.0;
				if (exif->gps_longitude_ref == 'W') {
					exif->gps_longitude = -exif->gps_longitude;
				}
			}
			break;

		case TAG_GPS_ALTITUDE_REF:
			if (count >= 1) {
				exif->gps_altitude_ref = value_offset & 0xFF;
			}
			break;

		case TAG_GPS_ALTITUDE:
			if (type == TYPE_RATIONAL && (size_t)value_offset + 8 <= tiff_len) {
				exif->gps_altitude = read_rational(tiff + value_offset, order);
				if (exif->gps_altitude_ref == 1) {
					exif->gps_altitude = -exif->gps_altitude;
				}
			}
			break;

		case TAG_GPS_DATESTAMP:
			if (type == TYPE_ASCII && count < sizeof(exif->gps_datestamp) && (size_t)value_offset + count <= tiff_len) {
				memcpy(exif->gps_datestamp, tiff + value_offset, count);
				exif->gps_datestamp[count] = '\0';
			}
			break;
	}
}

/**
 * @brief Parse IFD (Image File Directory)
 *
 * The ExifIFD and GPS IFD pointers are followed only from IFD0 (is_ifd0), and each at most once: offsets come
 * from the file, so a pointer back to an IFD (directly or through another sub-IFD) must not recurse forever,
 * and repeated pointers must not multiply the work.
 */
static void parse_ifd(tiff_context_t *ctx, img2_exif_t *exif, uint32_t ifd_offset, bool is_gps, bool is_ifd0)
{
	const uint8_t *tiff = ctx->tiff_data;
	size_t tiff_len = ctx->tiff_len;
	byte_order_t order = ctx->byte_order;
	size_t pos = ifd_offset; /* size_t: offsets near 4 GiB must not wrap around the bounds checks */

	if (pos + 2 > tiff_len) {
		return;
	}

	uint16_t num_entries = read_u16(tiff + pos, order);
	pos += 2;

	for (uint16_t i = 0; i < num_entries; i++) {
		if (pos + 12 > tiff_len) {
			return;
		}

		uint16_t tag = read_u16(tiff + pos, order);
		uint16_t type = read_u16(tiff + pos + 2, order);
		uint32_t count = read_u32(tiff + pos + 4, order);
		uint32_t value_offset = read_u32(tiff + pos + 8, order);

		if (is_gps) {
			parse_gps_entry(ctx, exif, tag, type, count, value_offset);

		} else {
			parse_ifd_entry(ctx, exif, tag, type, count, value_offset);

			/* Sub-IFD offsets (IFD0 only, once each) */
			if (is_ifd0 && tag == TAG_EXIF_OFFSET && !ctx->exif_ifd_done) {
				ctx->exif_ifd_done = true;
				parse_ifd(ctx, exif, value_offset, false, false);
			} else if (is_ifd0 && tag == TAG_GPS_OFFSET && !ctx->gps_ifd_done) {
				ctx->gps_ifd_done = true;
				parse_ifd(ctx, exif, value_offset, true, false);
			}
		}

		pos += 12;
	}
}

/* ========== Public API Implementation ========== */

void img2_exif_init(img2_exif_t *exif)
{
	if (!exif) {
		return;
	}
	memset(exif, 0, sizeof(img2_exif_t));
}

void img2_xmp_init(img2_xmp_t *xmp)
{
	if (!xmp) {
		return;
	}
	memset(xmp, 0, sizeof(img2_xmp_t));
}

img2_status_t img2_exif_parse_tiff(img2_exif_t *exif, const uint8_t *tiff_data, size_t len)
{
	if (!exif || !tiff_data) {
		return IMG2_E_ARG;
	}

	if (len < 8) {
		return IMG2_E_FORMAT;
	}

	/* Validate TIFF magic ('II' or 'MM' + 0x002A) */
	byte_order_t byte_order;
	if (tiff_data[0] == 'I' && tiff_data[1] == 'I') {
		byte_order = BYTE_ORDER_LITTLE_ENDIAN;
	} else if (tiff_data[0] == 'M' && tiff_data[1] == 'M') {
		byte_order = BYTE_ORDER_BIG_ENDIAN;
	} else {
		return IMG2_E_FORMAT;
	}

	/* Verify TIFF magic number (0x002A) */
	uint16_t magic = read_u16(tiff_data + 2, byte_order);
	if (magic != 0x002A) {
		return IMG2_E_FORMAT;
	}

	/* Read IFD0 offset */
	uint32_t ifd0_offset = read_u32(tiff_data + 4, byte_order);

	/* Initialize tiff_context_t */
	tiff_context_t ctx;
	ctx.tiff_data = tiff_data;
	ctx.tiff_len = len;
	ctx.byte_order = byte_order;
	ctx.exif_ifd_done = false;
	ctx.gps_ifd_done = false;
	ctx.iso_short_seen = false;
	ctx.iso_short = 0;
	ctx.sensitivity_type = 0;
	ctx.sos = ctx.rei = ctx.iso_long = 0;

	/* Call parse_ifd() for IFD0 */
	parse_ifd(&ctx, exif, ifd0_offset, false, true);
	resolve_iso(&ctx, exif);

	return IMG2_OK;
}

img2_status_t img2_exif_parse(img2_exif_t *exif, const uint8_t *jpeg_data, size_t len)
{
	return img2_exif_parse_jpeg(exif, jpeg_data, len);
}

img2_status_t img2_exif_parse_jpeg(img2_exif_t *exif, const uint8_t *jpeg_data, size_t len)
{
	if (!exif || !jpeg_data) {
		return IMG2_E_ARG;
	}

	/* Find EXIF APP1 segment with "Exif\0\0" marker */
	const uint8_t *tiff_data = NULL;
	size_t tiff_len = 0;
	if (find_app1_segment(jpeg_data, len, "Exif\0\0", 6, &tiff_data, &tiff_len) != 0) {
		return IMG2_E_FORMAT;
	}

	/* Call img2_exif_parse_tiff() with extracted TIFF data */
	return img2_exif_parse_tiff(exif, tiff_data, tiff_len);
}

img2_status_t img2_xmp_parse_xml(img2_xmp_t *xmp, const char *xml_data, size_t len)
{
	if (!xmp || !xml_data) {
		return IMG2_E_ARG;
	}

	/* Parse XML */
	img2_xml_document_t *doc = img2_xml_document_create();
	if (!doc) {
		return IMG2_E_NOMEM;
	}

	if (img2_xml_document_parse(doc, xml_data, len) != 0) {
		img2_xml_document_destroy(doc);
		return IMG2_E_FORMAT;
	}

	/* Navigate XMP structure: rdf:RDF -> rdf:Description */
	img2_xml_node_t *root = img2_xml_document_root_element(doc);
	if (!root) {
		img2_xml_document_destroy(doc);
		return IMG2_E_FORMAT;
	}

	img2_xml_node_t *desc = img2_xml_element_first_child(root, NULL);
	if (!desc) {
		img2_xml_document_destroy(doc);
		return IMG2_E_FORMAT;
	}

	/* Extract Dublin Core, XMP Basic, and IPTC/Photoshop fields */
	img2_xml_node_t *child = desc->first_child;
	while (child) {
		const char *name = img2_xml_element_name(child);
		if (name) {
			const char *text = img2_xml_element_text(child);

			if (strstr(name, "creator") && text) {
				snprintf(xmp->creator, sizeof(xmp->creator), "%s", text);
			} else if (strstr(name, "rights") && text) {
				snprintf(xmp->copyright, sizeof(xmp->copyright), "%s", text);
			} else if (strstr(name, "description") && text) {
				snprintf(xmp->description, sizeof(xmp->description), "%s", text);
			} else if (strstr(name, "title") && text) {
				snprintf(xmp->title, sizeof(xmp->title), "%s", text);
			} else if (strstr(name, "Rating") && text) {
				xmp->rating = atoi(text);
				xmp->has_rating = true;
			} else if (strstr(name, "CreateDate") && text) {
				snprintf(xmp->create_date, sizeof(xmp->create_date), "%s", text);
			} else if (strstr(name, "ModifyDate") && text) {
				snprintf(xmp->modify_date, sizeof(xmp->modify_date), "%s", text);
			} else if (strstr(name, "Label") && text) {
				snprintf(xmp->label, sizeof(xmp->label), "%s", text);
			} else if (strstr(name, "City") && text) {
				snprintf(xmp->city, sizeof(xmp->city), "%s", text);
			} else if (strstr(name, "Country") && text) {
				snprintf(xmp->country, sizeof(xmp->country), "%s", text);
			} else if (strstr(name, "Credit") && text) {
				snprintf(xmp->credit, sizeof(xmp->credit), "%s", text);
			} else if (strstr(name, "Source") && text) {
				snprintf(xmp->source, sizeof(xmp->source), "%s", text);
			} else if (strstr(name, "subject")) {
				/* Keywords - may be in a Bag/Seq structure */
				img2_xml_node_t *bag = img2_xml_element_first_child(child, NULL);
				if (bag) {
					/* Collect all keywords */
					char keywords_buf[1024] = { 0 };
					size_t keywords_len = 0;

					img2_xml_node_t *item = bag->first_child;
					while (item) {
						const char *keyword = img2_xml_element_text(item);
						if (keyword && keywords_len + strlen(keyword) + 2 < sizeof(keywords_buf)) {
							if (keywords_len > 0) {
								keywords_buf[keywords_len++] = ',';
								keywords_buf[keywords_len++] = ' ';
							}
							strcpy(keywords_buf + keywords_len, keyword);
							keywords_len += strlen(keyword);
						}
						item = img2_xml_element_next_sibling(item, NULL);
					}

					if (keywords_len > 0) {
						/* a later dc:subject (same packet, or another XMP item parsed into the same struct)
						 * replaces the earlier keywords */
						char *copy = malloc(keywords_len + 1);
						if (copy) {
							strcpy(copy, keywords_buf);
							free(xmp->keywords);
							xmp->keywords = copy;
							xmp->has_keywords = true;
						}
					}
				}
			}
		}

		child = child->next_sibling;
	}

	img2_xml_document_destroy(doc);
	return IMG2_OK;
}

img2_status_t img2_xmp_parse(img2_xmp_t *xmp, const uint8_t *jpeg_data, size_t len)
{
	return img2_xmp_parse_jpeg(xmp, jpeg_data, len);
}

img2_status_t img2_xmp_parse_jpeg(img2_xmp_t *xmp, const uint8_t *jpeg_data, size_t len)
{
	if (!xmp || !jpeg_data) {
		return IMG2_E_ARG;
	}

	/* Find XMP APP1 segment with "http://ns.adobe.com/xap/1.0/\0" marker */
	const uint8_t *xml_data = NULL;
	size_t xml_len = 0;
	if (find_app1_segment(jpeg_data, len, "http://ns.adobe.com/xap/1.0/\0", 29, &xml_data, &xml_len) != 0) {
		return IMG2_E_FORMAT;
	}

	/* Call img2_xmp_parse_xml() with extracted XML data */
	return img2_xmp_parse_xml(xmp, (const char *)xml_data, xml_len);
}

void img2_exif_clear(img2_exif_t *exif)
{
	/* No dynamic allocations in img2_exif_t currently */
	(void)exif;
}

void img2_xmp_clear(img2_xmp_t *xmp)
{
	if (!xmp) {
		return;
	}

	if (xmp->keywords) {
		free(xmp->keywords);
		xmp->keywords = NULL;
	}
}
