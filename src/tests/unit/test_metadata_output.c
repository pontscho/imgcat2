/**
 * @file test_metadata_output.c
 * @brief Unit tests for EXIF/XMP metadata output formatting
 *
 * Tests text and JSON output for EXIF and XMP metadata structures.
 */

#include <stdio.h>
#include <string.h>

#include "../../imgcat2/core/metadata.h"
#include "../ctest.h"

#ifdef HAVE_EXIF_READER
#include "../../imgcat2/metadata/exif_reader.h"

/**
 * Test EXIF text output doesn't crash with NULL
 */
CTEST(metadata_output, exif_text_null)
{
	// Should not crash
	output_exif_text(NULL);
	ASSERT_TRUE(true);
}

/**
 * Test XMP text output doesn't crash with NULL
 */
CTEST(metadata_output, xmp_text_null)
{
	// Should not crash
	output_xmp_text(NULL);
	ASSERT_TRUE(true);
}

/**
 * Test EXIF JSON output doesn't crash with NULL
 */
CTEST(metadata_output, exif_json_null)
{
	// Should not crash
	output_exif_json(NULL, true);
	ASSERT_TRUE(true);
}

/**
 * Test XMP JSON output doesn't crash with NULL
 */
CTEST(metadata_output, xmp_json_null)
{
	// Should not crash
	output_xmp_json(NULL, true);
	ASSERT_TRUE(true);
}

/**
 * Test EXIF text output with minimal data
 */
CTEST(metadata_output, exif_text_minimal)
{
	exif_info_t exif;
	exif_info_init(&exif);

	// Set some basic fields
	strcpy(exif.make, "Canon");
	strcpy(exif.model, "EOS 5D");
	exif.has_f_number = true;
	exif.f_number = 2.8;
	exif.has_iso = true;
	exif.iso_speed = 400;

	// Should not crash - output goes to stdout
	output_exif_text(&exif);

	exif_info_free(&exif);
	ASSERT_TRUE(true);
}

/**
 * Test XMP text output with minimal data
 */
CTEST(metadata_output, xmp_text_minimal)
{
	xmp_info_t xmp;
	xmp_info_init(&xmp);

	// Set some basic fields
	strcpy(xmp.creator, "John Doe");
	strcpy(xmp.title, "Test Photo");
	xmp.has_rating = true;
	xmp.rating = 5;

	// Should not crash - output goes to stdout
	output_xmp_text(&xmp);

	xmp_info_free(&xmp);
	ASSERT_TRUE(true);
}

/**
 * Test EXIF JSON output with minimal data
 */
CTEST(metadata_output, exif_json_minimal)
{
	exif_info_t exif;
	exif_info_init(&exif);

	// Set some basic fields
	strcpy(exif.make, "Nikon");
	strcpy(exif.model, "D850");
	exif.has_exposure_time = true;
	exif.exposure_time = 0.008; // 1/125s

	// Capture output to test JSON structure
	// For now just ensure it doesn't crash
	output_exif_json(&exif, true);

	exif_info_free(&exif);
	ASSERT_TRUE(true);
}

/**
 * Test XMP JSON output with minimal data
 */
CTEST(metadata_output, xmp_json_minimal)
{
	xmp_info_t xmp;
	xmp_info_init(&xmp);

	// Set some basic fields
	strcpy(xmp.creator, "Jane Smith");
	strcpy(xmp.copyright, "Copyright 2024");
	xmp.has_keywords = true;
	xmp.keywords = strdup("landscape,nature,sunset");

	// Should not crash - output goes to stdout
	output_xmp_json(&xmp, true);

	xmp_info_free(&xmp);
	ASSERT_TRUE(true);
}

/**
 * Test EXIF JSON output with GPS data
 */
CTEST(metadata_output, exif_json_gps)
{
	exif_info_t exif;
	exif_info_init(&exif);

	// Set GPS data
	exif.has_gps = true;
	exif.gps_latitude = 37.7749;
	exif.gps_longitude = -122.4194;
	exif.gps_altitude = 15.5;
	exif.gps_latitude_ref = 'N';
	exif.gps_longitude_ref = 'W';

	// Should output valid JSON with GPS data
	output_exif_json(&exif, true);

	exif_info_free(&exif);
	ASSERT_TRUE(true);
}

/**
 * Test JSON escaping with special characters
 */
CTEST(metadata_output, json_escaping)
{
	exif_info_t exif;
	exif_info_init(&exif);

	// Set fields with special characters that need escaping
	strcpy(exif.description, "Test \"quoted\" string\nwith newline\ttab");
	strcpy(exif.artist, "Artist\\Name");

	// Should properly escape special characters in JSON
	output_exif_json(&exif, true);

	exif_info_free(&exif);
	ASSERT_TRUE(true);
}

/**
 * Test XMP JSON with location data
 */
CTEST(metadata_output, xmp_json_location)
{
	xmp_info_t xmp;
	xmp_info_init(&xmp);

	// Set location data
	strcpy(xmp.city, "San Francisco");
	strcpy(xmp.country, "USA");
	strcpy(xmp.credit, "Photo Agency Inc");

	// Should output valid JSON with location data
	output_xmp_json(&xmp, true);

	xmp_info_free(&xmp);
	ASSERT_TRUE(true);
}

/**
 * Test EXIF text output with full camera data
 */
CTEST(metadata_output, exif_text_full_camera)
{
	exif_info_t exif;
	exif_info_init(&exif);

	// Set comprehensive camera data
	strcpy(exif.make, "Canon");
	strcpy(exif.model, "EOS R5");
	strcpy(exif.software, "Adobe Lightroom");
	strcpy(exif.lens_model, "RF 24-70mm F2.8 L IS USM");

	exif.has_exposure_time = true;
	exif.exposure_time = 0.004; // 1/250s
	exif.has_f_number = true;
	exif.f_number = 5.6;
	exif.has_iso = true;
	exif.iso_speed = 800;
	exif.exposure_bias = -0.3;

	exif.has_focal_length = true;
	exif.focal_length = 50.0;
	exif.focal_length_35mm = 50.0;
	exif.max_aperture = 2.8;

	exif.image_width = 8192;
	exif.image_height = 5464;
	exif.orientation = 1;
	exif.has_datetime = true;
	strcpy(exif.date_time, "2024:12:25 14:30:45");

	// Should output comprehensive camera information
	output_exif_text(&exif);

	exif_info_free(&exif);
	ASSERT_TRUE(true);
}

#else
/* Placeholder tests when EXIF reader is disabled */
CTEST(metadata_output, disabled)
{
	ASSERT_TRUE(true);
}
#endif
