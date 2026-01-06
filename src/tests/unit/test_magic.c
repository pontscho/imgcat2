/**
 * @file test_magic.c
 * @brief Unit tests for MIME type detection via magic bytes
 *
 * Tests magic byte signature detection for PNG, JPEG, GIF, BMP,
 * and other image formats. Validates correct format identification
 * and unknown format handling.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../imgcat2/decoders/magic.h"
#include "../ctest.h"

/**
 * @test Test PNG magic bytes detection
 *
 * Verifies that detect_mime_type() correctly identifies PNG files
 * from their 8-byte signature: 89 50 4E 47 0D 0A 1A 0A
 */
CTEST(magic, detect_png)
{
	/* Valid PNG signature */
	const uint8_t png_signature[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

	mime_type_t mime = detect_mime_type(png_signature, sizeof(png_signature));
	ASSERT_EQUAL(MIME_PNG, mime);

	/* PNG signature with extra data */
	const uint8_t png_with_data[] = {
		0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, /* PNG signature */
		0x00, 0x00, 0x00, 0x0D, /* Extra data */
		'I',  'H',  'D',  'R' /* IHDR chunk */
	};

	mime_type_t mime2 = detect_mime_type(png_with_data, sizeof(png_with_data));
	ASSERT_EQUAL(MIME_PNG, mime2);

	/* Verify mime_type_name returns correct string */
	const char *name = mime_type_name(MIME_PNG);
	ASSERT_NOT_NULL(name);
	ASSERT_STR("PNG", name);
}

/**
 * @test Test JPEG magic bytes detection
 *
 * Verifies that detect_mime_type() correctly identifies JPEG files
 * from their 3-byte signature: FF D8 FF (SOI marker)
 */
CTEST(magic, detect_jpeg)
{
	/* Valid JPEG signature (JFIF) */
	const uint8_t jpeg_jfif[] = {
		0xFF, 0xD8, 0xFF, 0xE0, /* SOI + APP0 marker */
		0x00, 0x10, /* Length */
		'J',  'F',  'I',  'F',  0x00 /* JFIF identifier */
	};

	mime_type_t mime1 = detect_mime_type(jpeg_jfif, sizeof(jpeg_jfif));
	ASSERT_EQUAL(MIME_JPEG, mime1);

	/* Valid JPEG signature (EXIF) */
	const uint8_t jpeg_exif[] = {
		0xFF, 0xD8, 0xFF, 0xE1, /* SOI + APP1 marker */
		0x00, 0x10, /* Length */
		'E',  'x',  'i',  'f',  0x00 /* Exif identifier */
	};

	mime_type_t mime2 = detect_mime_type(jpeg_exif, sizeof(jpeg_exif));
	ASSERT_EQUAL(MIME_JPEG, mime2);

	/* Minimal JPEG (just SOI) */
	const uint8_t jpeg_minimal[] = { 0xFF, 0xD8, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00 };

	mime_type_t mime3 = detect_mime_type(jpeg_minimal, sizeof(jpeg_minimal));
	ASSERT_EQUAL(MIME_JPEG, mime3);

	/* Verify mime_type_name returns correct string */
	const char *name = mime_type_name(MIME_JPEG);
	ASSERT_NOT_NULL(name);
	ASSERT_STR("JPEG", name);
}

/**
 * @test Test GIF magic bytes detection
 *
 * Verifies that detect_mime_type() correctly identifies GIF files
 * from their signatures: "GIF87a" or "GIF89a"
 */
CTEST(magic, detect_gif)
{
	/* Valid GIF87a signature */
	const uint8_t gif87a[] = {
		'G',  'I',  'F', '8', '7', 'a', /* Signature */
		0x0A, 0x00, /* Width: 10 */
		0x0A, 0x00 /* Height: 10 */
	};

	mime_type_t mime1 = detect_mime_type(gif87a, sizeof(gif87a));
	ASSERT_EQUAL(MIME_GIF, mime1);

	/* Valid GIF89a signature */
	const uint8_t gif89a[] = {
		'G',  'I',  'F', '8', '9', 'a', /* Signature */
		0x14, 0x00, /* Width: 20 */
		0x14, 0x00 /* Height: 20 */
	};

	mime_type_t mime2 = detect_mime_type(gif89a, sizeof(gif89a));
	ASSERT_EQUAL(MIME_GIF, mime2);

	/* Minimal GIF87a (just signature) */
	const uint8_t gif_minimal[] = { 'G', 'I', 'F', '8', '7', 'a', 0x00, 0x00 };

	mime_type_t mime3 = detect_mime_type(gif_minimal, sizeof(gif_minimal));
	ASSERT_EQUAL(MIME_GIF, mime3);

	/* Verify mime_type_name returns correct string */
	const char *name = mime_type_name(MIME_GIF);
	ASSERT_NOT_NULL(name);
	ASSERT_STR("GIF", name);
}

/**
 * @test Test BMP magic bytes detection
 *
 * Verifies that detect_mime_type() correctly identifies BMP files
 * from their 2-byte signature: "BM"
 */
CTEST(magic, detect_bmp)
{
	/* Valid BMP signature */
	const uint8_t bmp_signature[] = {
		'B',  'M', /* BM signature */
		0x36, 0x04, 0x00, 0x00, /* File size */
		0x00, 0x00, /* Reserved */
		0x00, 0x00, /* Reserved */
		0x36, 0x00, 0x00, 0x00 /* Offset to pixel data */
	};

	mime_type_t mime = detect_mime_type(bmp_signature, sizeof(bmp_signature));
	ASSERT_EQUAL(MIME_BMP, mime);

	/* Minimal BMP (just signature) */
	const uint8_t bmp_minimal[] = { 'B', 'M', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	mime_type_t mime2 = detect_mime_type(bmp_minimal, sizeof(bmp_minimal));
	ASSERT_EQUAL(MIME_BMP, mime2);

	/* Verify mime_type_name returns correct string */
	const char *name = mime_type_name(MIME_BMP);
	ASSERT_NOT_NULL(name);
	ASSERT_STR("BMP", name);
}

/**
 * @test Test unknown format detection
 *
 * Verifies that detect_mime_type() returns MIME_UNKNOWN for
 * unrecognized file signatures.
 */
CTEST(magic, detect_unknown)
{
	/* Random data that doesn't match any signature */
	const uint8_t random_data[] = { 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0 };

	mime_type_t mime1 = detect_mime_type(random_data, sizeof(random_data));
	ASSERT_EQUAL(MIME_UNKNOWN, mime1);

	/* Text file (not an image) */
	const uint8_t text_data[] = { 'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd' };

	mime_type_t mime2 = detect_mime_type(text_data, sizeof(text_data));
	ASSERT_EQUAL(MIME_UNKNOWN, mime2);

	/* Almost PNG (but not quite) */
	const uint8_t almost_png[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0B };

	mime_type_t mime3 = detect_mime_type(almost_png, sizeof(almost_png));
	ASSERT_EQUAL(MIME_UNKNOWN, mime3);

	/* Almost JPEG (but not quite) */
	const uint8_t almost_jpeg[] = { 0xFF, 0xD8, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00 };

	mime_type_t mime4 = detect_mime_type(almost_jpeg, sizeof(almost_jpeg));
	ASSERT_EQUAL(MIME_UNKNOWN, mime4);

	/* Almost GIF (but not quite - "GIF88a" doesn't exist) */
	const uint8_t almost_gif[] = { 'G', 'I', 'F', '8', '8', 'a', 0x00, 0x00 };

	mime_type_t mime5 = detect_mime_type(almost_gif, sizeof(almost_gif));
	ASSERT_EQUAL(MIME_UNKNOWN, mime5);

	/* Almost BMP (but not quite) */
	const uint8_t almost_bmp[] = { 'B', 'N', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	mime_type_t mime6 = detect_mime_type(almost_bmp, sizeof(almost_bmp));
	ASSERT_EQUAL(MIME_UNKNOWN, mime6);

	/* Verify mime_type_name returns correct string */
	const char *name = mime_type_name(MIME_UNKNOWN);
	ASSERT_NOT_NULL(name);
	ASSERT_STR("UNKNOWN", name);
}

/**
 * @test Test detection with insufficient data
 *
 * Verifies that detect_mime_type() returns MIME_UNKNOWN when
 * the data buffer is too small for reliable detection.
 */
CTEST(magic, detect_insufficient_data)
{
	/* Empty buffer */
	const uint8_t empty[] = {};
	mime_type_t mime1 = detect_mime_type(empty, 0);
	ASSERT_EQUAL(MIME_UNKNOWN, mime1);

	/* Only 1 byte (too short) */
	const uint8_t one_byte[] = { 0x89 };
	mime_type_t mime2 = detect_mime_type(one_byte, 1);
	ASSERT_EQUAL(MIME_UNKNOWN, mime2);

	/* 7 bytes (just under the 8 byte requirement) */
	const uint8_t seven_bytes[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A };
	mime_type_t mime3 = detect_mime_type(seven_bytes, 7);
	ASSERT_EQUAL(MIME_UNKNOWN, mime3);
}

/**
 * @test Test PSD magic bytes detection
 *
 * Verifies that detect_mime_type() correctly identifies PSD files
 * from their signature: "8BPS"
 */
CTEST(magic, detect_psd)
{
	/* Valid PSD signature */
	const uint8_t psd_signature[] = {
		'8',  'B',  'P', 'S', /* Signature */
		0x00, 0x01, /* Version: 1 */
		0x00, 0x00, /* Reserved */
		0x00, 0x00 /* Reserved */
	};

	mime_type_t mime = detect_mime_type(psd_signature, sizeof(psd_signature));
	ASSERT_EQUAL(MIME_PSD, mime);

	/* Verify mime_type_name returns correct string */
	const char *name = mime_type_name(MIME_PSD);
	ASSERT_NOT_NULL(name);
	ASSERT_STR("PSD", name);
}

/**
 * @test Test HDR magic bytes detection
 *
 * Verifies that detect_mime_type() correctly identifies HDR/Radiance files.
 */
CTEST(magic, detect_hdr)
{
	/* Valid HDR Radiance signature */
	const uint8_t hdr_radiance[] = { '#',  '?', 'R', 'A', 'D', 'I', 'A', 'N', 'C', 'E', /* #?RADIANCE */
		                             '\n', 0x00 };

	mime_type_t mime1 = detect_mime_type(hdr_radiance, sizeof(hdr_radiance));
	ASSERT_EQUAL(MIME_HDR, mime1);

	/* Valid HDR RGBE signature */
	const uint8_t hdr_rgbe[] = { '#',  '?',  'R',  'G', 'B', 'E', /* #?RGBE */
		                         '\n', 0x00, 0x00, 0x00 };

	mime_type_t mime2 = detect_mime_type(hdr_rgbe, sizeof(hdr_rgbe));
	ASSERT_EQUAL(MIME_HDR, mime2);

	/* Verify mime_type_name returns correct string */
	const char *name = mime_type_name(MIME_HDR);
	ASSERT_NOT_NULL(name);
	ASSERT_STR("HDR", name);
}

/**
 * @test Test PNM magic bytes detection
 *
 * Verifies that detect_mime_type() correctly identifies PNM files
 * (PGM P5 and PPM P6 formats).
 */
CTEST(magic, detect_pnm)
{
	/* Valid PGM P5 (grayscale) signature */
	const uint8_t pnm_p5[] = { 'P',  '5', /* P5 signature */
		                       '\n', '1', '0', ' ', '1', '0' };

	mime_type_t mime1 = detect_mime_type(pnm_p5, sizeof(pnm_p5));
	ASSERT_EQUAL(MIME_PNM, mime1);

	/* Valid PPM P6 (RGB) signature */
	const uint8_t pnm_p6[] = { 'P',  '6', /* P6 signature */
		                       '\n', '2', '0', ' ', '2', '0' };

	mime_type_t mime2 = detect_mime_type(pnm_p6, sizeof(pnm_p6));
	ASSERT_EQUAL(MIME_PNM, mime2);

	/* Verify mime_type_name returns correct string */
	const char *name = mime_type_name(MIME_PNM);
	ASSERT_NOT_NULL(name);
	ASSERT_STR("PNM", name);
}

/**
 * @test Test TGA detection
 *
 * Verifies that detect_mime_type() correctly identifies TGA files
 * (which have a header-based detection rather than magic bytes).
 */
CTEST(magic, detect_tga)
{
	/* Valid TGA header (uncompressed true-color image) */
	const uint8_t tga_header[] = {
		0x00, /* ID length */
		0x00, /* Color map type: 0 (no color map) */
		0x02, /* Image type: 2 (uncompressed true-color) */
		0x00, 0x00, /* Color map specification (5 bytes) */
		0x00, 0x00, 0x00, 0x00, 0x00, /* X-origin */
		0x00, 0x00, /* Y-origin */
		0x0A, 0x00, /* Width: 10 */
		0x0A, 0x00, /* Height: 10 */
		0x18, /* Pixel depth: 24 bits */
		0x00 /* Image descriptor */
	};

	mime_type_t mime = detect_mime_type(tga_header, sizeof(tga_header));
	ASSERT_EQUAL(MIME_TGA, mime);

	/* Verify mime_type_name returns correct string */
	const char *name = mime_type_name(MIME_TGA);
	ASSERT_NOT_NULL(name);
	ASSERT_STR("TGA", name);
}
