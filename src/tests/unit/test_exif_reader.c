/**
 * @file test_exif_reader.c
 * @brief Unit tests for EXIF/XMP metadata parser
 *
 * Tests EXIF and XMP parsing from JPEG data using minimal mock data.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../imgcat2/metadata/exif_reader.h"
#include "../ctest.h"

/**
 * Minimal valid JPEG with EXIF APP1 marker
 *
 * Structure:
 * - SOI marker (0xFFD8)
 * - APP1 marker (0xFFE1) with EXIF data
 * - TIFF header (little-endian)
 * - IFD0 with Make="Canon" and Model="Test"
 * - EOI marker (0xFFD9)
 */
static const uint8_t minimal_jpeg_with_exif[] = {
	/* JPEG SOI */
	0xFF,
	0xD8,

	/* APP1 marker */
	0xFF,
	0xE1,

	/* APP1 length (2 bytes, big-endian) = 0x0048 (72 bytes, includes this length field) */
	0x00,
	0x48,

	/* EXIF identifier */
	'E',
	'x',
	'i',
	'f',
	0x00,
	0x00,

	/* TIFF header - little endian */
	'I',
	'I', /* Byte order (little-endian) */
	0x2A,
	0x00, /* TIFF magic number (0x002A) */
	0x08,
	0x00,
	0x00,
	0x00, /* Offset to IFD0 */

	/* IFD0 - 2 entries */
	0x02,
	0x00, /* Number of entries */

	/* Entry 1: Make (0x010F) */
	0x0F,
	0x01, /* Tag: Make */
	0x02,
	0x00, /* Type: ASCII */
	0x06,
	0x00,
	0x00,
	0x00, /* Count: 6 bytes */
	0x26,
	0x00,
	0x00,
	0x00, /* Offset to data */

	/* Entry 2: Model (0x0110) */
	0x10,
	0x01, /* Tag: Model */
	0x02,
	0x00, /* Type: ASCII */
	0x05,
	0x00,
	0x00,
	0x00, /* Count: 5 bytes */
	0x2C,
	0x00,
	0x00,
	0x00, /* Offset to data */

	/* Next IFD offset (0 = no more IFDs) */
	0x00,
	0x00,
	0x00,
	0x00,

	/* Data for Make tag at offset 0x26 */
	'C',
	'a',
	'n',
	'o',
	'n',
	0x00,

	/* Data for Model tag at offset 0x2C */
	'T',
	'e',
	's',
	't',
	0x00,

	/* Padding to fill APP1 segment */
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,

	/* JPEG EOI */
	0xFF,
	0xD9
};

/**
 * Minimal valid JPEG with XMP APP1 marker
 */
static const char *minimal_xmp_xml = "<?xml version=\"1.0\"?>"
                                     "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\" "
                                     "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
                                     "xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\">"
                                     "<rdf:Description>"
                                     "<dc:creator>Test Author</dc:creator>"
                                     "<dc:rights>Test Copyright</dc:rights>"
                                     "<xmp:Rating>5</xmp:Rating>"
                                     "</rdf:Description>"
                                     "</rdf:RDF>";

/**
 * @test Test EXIF info initialization
 */
CTEST(exif_reader, init_exif_info)
{
	exif_info_t exif;
	exif_info_init(&exif);

	ASSERT_STR("", exif.make);
	ASSERT_STR("", exif.model);
	ASSERT_EQUAL(0, exif.image_width);
	ASSERT_EQUAL(0, exif.image_height);
	ASSERT_FALSE(exif.has_gps);
	ASSERT_FALSE(exif.has_exposure_time);
	ASSERT_FALSE(exif.has_f_number);
	ASSERT_FALSE(exif.has_iso);
}

/**
 * @test Test XMP info initialization
 */
CTEST(exif_reader, init_xmp_info)
{
	xmp_info_t xmp;
	xmp_info_init(&xmp);

	ASSERT_STR("", xmp.creator);
	ASSERT_STR("", xmp.copyright);
	ASSERT_STR("", xmp.description);
	ASSERT_NULL(xmp.keywords);
	ASSERT_FALSE(xmp.has_rating);
	ASSERT_FALSE(xmp.has_keywords);
}

/**
 * @test Test parsing EXIF from minimal JPEG
 */
CTEST(exif_reader, parse_exif_basic)
{
	exif_info_t exif;
	exif_info_init(&exif);

	int result = parse_exif(&exif, minimal_jpeg_with_exif, sizeof(minimal_jpeg_with_exif));

	ASSERT_EQUAL(0, result);
	ASSERT_STR("Canon", exif.make);
	ASSERT_STR("Test", exif.model);

	exif_info_free(&exif);
}

/**
 * @test Test parsing EXIF from invalid data
 */
CTEST(exif_reader, parse_exif_invalid)
{
	exif_info_t exif;
	exif_info_init(&exif);

	/* Invalid JPEG (no SOI marker) */
	uint8_t invalid_data[] = { 0x00, 0x01, 0x02, 0x03 };
	int result = parse_exif(&exif, invalid_data, sizeof(invalid_data));

	ASSERT_NOT_EQUAL(0, result);

	exif_info_free(&exif);
}

/**
 * @test Test parsing EXIF with NULL parameters
 */
CTEST(exif_reader, parse_exif_null_params)
{
	exif_info_t exif;
	exif_info_init(&exif);

	uint8_t data[] = { 0xFF, 0xD8, 0xFF, 0xD9 };

	/* NULL exif */
	int result1 = parse_exif(NULL, data, sizeof(data));
	ASSERT_NOT_EQUAL(0, result1);

	/* NULL data */
	int result2 = parse_exif(&exif, NULL, sizeof(data));
	ASSERT_NOT_EQUAL(0, result2);

	/* Zero length */
	int result3 = parse_exif(&exif, data, 0);
	ASSERT_NOT_EQUAL(0, result3);

	exif_info_free(&exif);
}

/**
 * @test Test parsing EXIF from JPEG without EXIF segment
 */
CTEST(exif_reader, parse_exif_no_segment)
{
	exif_info_t exif;
	exif_info_init(&exif);

	/* Minimal JPEG with no APP1 marker */
	uint8_t jpeg_no_exif[] = {
		0xFF,
		0xD8, /* SOI */
		0xFF,
		0xD9 /* EOI */
	};

	int result = parse_exif(&exif, jpeg_no_exif, sizeof(jpeg_no_exif));

	ASSERT_NOT_EQUAL(0, result);

	exif_info_free(&exif);
}

/**
 * @test Test XMP parsing with mock data
 */
CTEST(exif_reader, parse_xmp_mock)
{
	xmp_info_t xmp;
	xmp_info_init(&xmp);

	/* Build minimal JPEG with XMP APP1 marker */
	size_t xmp_xml_len = strlen(minimal_xmp_xml);
	size_t xmp_marker_len = 29; /* "http://ns.adobe.com/xap/1.0/\0" */
	size_t app1_len = 2 + xmp_marker_len + xmp_xml_len; /* length field + marker + XML */
	size_t total_len = 2 + 4 + app1_len + 2; /* SOI + APP1 header + APP1 data + EOI */

	uint8_t *jpeg_with_xmp = malloc(total_len);
	ASSERT_NOT_NULL(jpeg_with_xmp);

	size_t pos = 0;

	/* SOI */
	jpeg_with_xmp[pos++] = 0xFF;
	jpeg_with_xmp[pos++] = 0xD8;

	/* APP1 marker */
	jpeg_with_xmp[pos++] = 0xFF;
	jpeg_with_xmp[pos++] = 0xE1;

	/* APP1 length (big-endian, includes length field itself) */
	uint16_t segment_len = app1_len;
	jpeg_with_xmp[pos++] = (segment_len >> 8) & 0xFF;
	jpeg_with_xmp[pos++] = segment_len & 0xFF;

	/* XMP marker */
	memcpy(jpeg_with_xmp + pos, "http://ns.adobe.com/xap/1.0/", 29);
	pos += 29;

	/* XMP XML data */
	memcpy(jpeg_with_xmp + pos, minimal_xmp_xml, xmp_xml_len);
	pos += xmp_xml_len;

	/* EOI */
	jpeg_with_xmp[pos++] = 0xFF;
	jpeg_with_xmp[pos++] = 0xD9;

	/* Parse XMP */
	int result = parse_xmp(&xmp, jpeg_with_xmp, total_len);

	ASSERT_EQUAL(0, result);
	ASSERT_STR("Test Author", xmp.creator);
	ASSERT_STR("Test Copyright", xmp.copyright);
	ASSERT_TRUE(xmp.has_rating);
	ASSERT_EQUAL(5, xmp.rating);

	free(jpeg_with_xmp);
	xmp_info_free(&xmp);
}

/**
 * @test Test XMP parsing with NULL parameters
 */
CTEST(exif_reader, parse_xmp_null_params)
{
	xmp_info_t xmp;
	xmp_info_init(&xmp);

	uint8_t data[] = { 0xFF, 0xD8, 0xFF, 0xD9 };

	/* NULL xmp */
	int result1 = parse_xmp(NULL, data, sizeof(data));
	ASSERT_NOT_EQUAL(0, result1);

	/* NULL data */
	int result2 = parse_xmp(&xmp, NULL, sizeof(data));
	ASSERT_NOT_EQUAL(0, result2);

	xmp_info_free(&xmp);
}

/**
 * @test Test XMP parsing from JPEG without XMP segment
 */
CTEST(exif_reader, parse_xmp_no_segment)
{
	xmp_info_t xmp;
	xmp_info_init(&xmp);

	/* Minimal JPEG with no XMP APP1 marker */
	uint8_t jpeg_no_xmp[] = {
		0xFF,
		0xD8, /* SOI */
		0xFF,
		0xD9 /* EOI */
	};

	int result = parse_xmp(&xmp, jpeg_no_xmp, sizeof(jpeg_no_xmp));

	ASSERT_NOT_EQUAL(0, result);

	xmp_info_free(&xmp);
}

/**
 * @test Test EXIF/XMP memory cleanup
 */
CTEST(exif_reader, memory_cleanup)
{
	exif_info_t exif;
	xmp_info_t xmp;

	exif_info_init(&exif);
	xmp_info_init(&xmp);

	/* Allocate XMP keywords */
	xmp.keywords = malloc(100);
	ASSERT_NOT_NULL(xmp.keywords);
	strcpy(xmp.keywords, "test, keywords");
	xmp.has_keywords = true;

	/* Test cleanup (should not crash) */
	exif_info_free(&exif);
	xmp_info_free(&xmp);

	/* Verify keywords freed */
	ASSERT_NULL(xmp.keywords);
}

/**
 * @test Test EXIF parsing with big-endian TIFF header
 */
CTEST(exif_reader, parse_exif_big_endian)
{
	exif_info_t exif;
	exif_info_init(&exif);

	/* Minimal JPEG with big-endian EXIF */
	uint8_t jpeg_big_endian[] = { /* JPEG SOI */
		                          0xFF,
		                          0xD8,

		                          /* APP1 marker */
		                          0xFF,
		                          0xE1,

		                          /* APP1 length (48 bytes) */
		                          0x00,
		                          0x30,

		                          /* EXIF identifier */
		                          'E',
		                          'x',
		                          'i',
		                          'f',
		                          0x00,
		                          0x00,

		                          /* TIFF header - BIG endian */
		                          'M',
		                          'M', /* Byte order (big-endian) */
		                          0x00,
		                          0x2A, /* TIFF magic number */
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x08, /* Offset to IFD0 */

		                          /* IFD0 - 1 entry */
		                          0x00,
		                          0x01, /* Number of entries */

		                          /* Entry 1: Make */
		                          0x01,
		                          0x0F, /* Tag: Make (big-endian) */
		                          0x00,
		                          0x02, /* Type: ASCII */
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x06, /* Count: 6 bytes */
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x1A, /* Offset to data */

		                          /* Next IFD offset */
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,

		                          /* Data for Make tag */
		                          'N',
		                          'i',
		                          'k',
		                          'o',
		                          'n',
		                          0x00,

		                          /* Padding */
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,
		                          0x00,

		                          /* JPEG EOI */
		                          0xFF,
		                          0xD9
	};

	int result = parse_exif(&exif, jpeg_big_endian, sizeof(jpeg_big_endian));

	ASSERT_EQUAL(0, result);
	ASSERT_STR("Nikon", exif.make);

	exif_info_free(&exif);
}
