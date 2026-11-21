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

#include "../../libimg2/decoders/magic.h"
#include "../ctest.h"

/**
 * @test Test PNG magic bytes detection
 *
 * Verifies that img2_detect_mime() correctly identifies PNG files
 * from their 8-byte signature: 89 50 4E 47 0D 0A 1A 0A
 */
CTEST(magic, detect_png)
{
	/* Valid PNG signature */
	const uint8_t png_signature[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

	img2_mime_t mime = img2_detect_mime(png_signature, sizeof(png_signature));
	ASSERT_EQUAL(IMG2_MIME_PNG, mime);

	/* PNG signature with extra data */
	const uint8_t png_with_data[] = {
		0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, /* PNG signature */
		0x00, 0x00, 0x00, 0x0D, /* Extra data */
		'I',  'H',  'D',  'R' /* IHDR chunk */
	};

	img2_mime_t mime2 = img2_detect_mime(png_with_data, sizeof(png_with_data));
	ASSERT_EQUAL(IMG2_MIME_PNG, mime2);

	/* Verify img2_mime_name returns correct string */
	const char *name = img2_mime_name(IMG2_MIME_PNG);
	ASSERT_NOT_NULL(name);
	ASSERT_STR("PNG", name);
}

/**
 * @test Test JPEG magic bytes detection
 *
 * Verifies that img2_detect_mime() correctly identifies JPEG files
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

	img2_mime_t mime1 = img2_detect_mime(jpeg_jfif, sizeof(jpeg_jfif));
	ASSERT_EQUAL(IMG2_MIME_JPEG, mime1);

	/* Valid JPEG signature (EXIF) */
	const uint8_t jpeg_exif[] = {
		0xFF, 0xD8, 0xFF, 0xE1, /* SOI + APP1 marker */
		0x00, 0x10, /* Length */
		'E',  'x',  'i',  'f',  0x00 /* Exif identifier */
	};

	img2_mime_t mime2 = img2_detect_mime(jpeg_exif, sizeof(jpeg_exif));
	ASSERT_EQUAL(IMG2_MIME_JPEG, mime2);

	/* Minimal JPEG (just SOI) */
	const uint8_t jpeg_minimal[] = { 0xFF, 0xD8, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00 };

	img2_mime_t mime3 = img2_detect_mime(jpeg_minimal, sizeof(jpeg_minimal));
	ASSERT_EQUAL(IMG2_MIME_JPEG, mime3);

	/* Verify img2_mime_name returns correct string */
	const char *name = img2_mime_name(IMG2_MIME_JPEG);
	ASSERT_NOT_NULL(name);
	ASSERT_STR("JPEG", name);
}

/**
 * @test Test GIF magic bytes detection
 *
 * Verifies that img2_detect_mime() correctly identifies GIF files
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

	img2_mime_t mime1 = img2_detect_mime(gif87a, sizeof(gif87a));
	ASSERT_EQUAL(IMG2_MIME_GIF, mime1);

	/* Valid GIF89a signature */
	const uint8_t gif89a[] = {
		'G',  'I',  'F', '8', '9', 'a', /* Signature */
		0x14, 0x00, /* Width: 20 */
		0x14, 0x00 /* Height: 20 */
	};

	img2_mime_t mime2 = img2_detect_mime(gif89a, sizeof(gif89a));
	ASSERT_EQUAL(IMG2_MIME_GIF, mime2);

	/* Minimal GIF87a (just signature) */
	const uint8_t gif_minimal[] = { 'G', 'I', 'F', '8', '7', 'a', 0x00, 0x00 };

	img2_mime_t mime3 = img2_detect_mime(gif_minimal, sizeof(gif_minimal));
	ASSERT_EQUAL(IMG2_MIME_GIF, mime3);

	/* Verify img2_mime_name returns correct string */
	const char *name = img2_mime_name(IMG2_MIME_GIF);
	ASSERT_NOT_NULL(name);
	ASSERT_STR("GIF", name);
}

/**
 * @test Test BMP magic bytes detection
 *
 * Verifies that img2_detect_mime() correctly identifies BMP files
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

	img2_mime_t mime = img2_detect_mime(bmp_signature, sizeof(bmp_signature));
	ASSERT_EQUAL(IMG2_MIME_BMP, mime);

	/* Minimal BMP (just signature) */
	const uint8_t bmp_minimal[] = { 'B', 'M', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	img2_mime_t mime2 = img2_detect_mime(bmp_minimal, sizeof(bmp_minimal));
	ASSERT_EQUAL(IMG2_MIME_BMP, mime2);

	/* Verify img2_mime_name returns correct string */
	const char *name = img2_mime_name(IMG2_MIME_BMP);
	ASSERT_NOT_NULL(name);
	ASSERT_STR("BMP", name);
}

/**
 * @test Test unknown format detection
 *
 * Verifies that img2_detect_mime() returns IMG2_MIME_UNKNOWN for
 * unrecognized file signatures.
 */
CTEST(magic, detect_unknown)
{
	/* Random data that doesn't match any signature */
	const uint8_t random_data[] = { 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0 };

	img2_mime_t mime1 = img2_detect_mime(random_data, sizeof(random_data));
	ASSERT_EQUAL(IMG2_MIME_UNKNOWN, mime1);

	/* Text file (not an image) */
	const uint8_t text_data[] = { 'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd' };

	img2_mime_t mime2 = img2_detect_mime(text_data, sizeof(text_data));
	ASSERT_EQUAL(IMG2_MIME_UNKNOWN, mime2);

	/* Almost PNG (but not quite) */
	const uint8_t almost_png[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0B };

	img2_mime_t mime3 = img2_detect_mime(almost_png, sizeof(almost_png));
	ASSERT_EQUAL(IMG2_MIME_UNKNOWN, mime3);

	/* Almost JPEG (but not quite) */
	const uint8_t almost_jpeg[] = { 0xFF, 0xD8, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00 };

	img2_mime_t mime4 = img2_detect_mime(almost_jpeg, sizeof(almost_jpeg));
	ASSERT_EQUAL(IMG2_MIME_UNKNOWN, mime4);

	/* Almost GIF (but not quite - "GIF88a" doesn't exist) */
	const uint8_t almost_gif[] = { 'G', 'I', 'F', '8', '8', 'a', 0x00, 0x00 };

	img2_mime_t mime5 = img2_detect_mime(almost_gif, sizeof(almost_gif));
	ASSERT_EQUAL(IMG2_MIME_UNKNOWN, mime5);

	/* Almost BMP (but not quite) */
	const uint8_t almost_bmp[] = { 'B', 'N', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	img2_mime_t mime6 = img2_detect_mime(almost_bmp, sizeof(almost_bmp));
	ASSERT_EQUAL(IMG2_MIME_UNKNOWN, mime6);

	/* Verify img2_mime_name returns correct string */
	const char *name = img2_mime_name(IMG2_MIME_UNKNOWN);
	ASSERT_NOT_NULL(name);
	ASSERT_STR("UNKNOWN", name);
}

/**
 * @test Test detection with insufficient data
 *
 * Verifies that img2_detect_mime() returns IMG2_MIME_UNKNOWN when
 * the data buffer is too small for reliable detection.
 */
CTEST(magic, detect_insufficient_data)
{
	/* Empty buffer */
	const uint8_t empty[] = {};
	img2_mime_t mime1 = img2_detect_mime(empty, 0);
	ASSERT_EQUAL(IMG2_MIME_UNKNOWN, mime1);

	/* Only 1 byte (too short) */
	const uint8_t one_byte[] = { 0x89 };
	img2_mime_t mime2 = img2_detect_mime(one_byte, 1);
	ASSERT_EQUAL(IMG2_MIME_UNKNOWN, mime2);

	/* 7 bytes (just under the 8 byte requirement) */
	const uint8_t seven_bytes[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A };
	img2_mime_t mime3 = img2_detect_mime(seven_bytes, 7);
	ASSERT_EQUAL(IMG2_MIME_UNKNOWN, mime3);
}

/**
 * @test Test PSD magic bytes detection
 *
 * Verifies that img2_detect_mime() correctly identifies PSD files
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

	img2_mime_t mime = img2_detect_mime(psd_signature, sizeof(psd_signature));
	ASSERT_EQUAL(IMG2_MIME_PSD, mime);

	/* Verify img2_mime_name returns correct string */
	const char *name = img2_mime_name(IMG2_MIME_PSD);
	ASSERT_NOT_NULL(name);
	ASSERT_STR("PSD", name);
}

/**
 * @test Test HDR magic bytes detection
 *
 * Verifies that img2_detect_mime() correctly identifies HDR/Radiance files.
 */
CTEST(magic, detect_hdr)
{
	/* Valid HDR Radiance signature */
	const uint8_t hdr_radiance[] = { '#',  '?', 'R', 'A', 'D', 'I', 'A', 'N', 'C', 'E', /* #?RADIANCE */
		                             '\n', 0x00 };

	img2_mime_t mime1 = img2_detect_mime(hdr_radiance, sizeof(hdr_radiance));
	ASSERT_EQUAL(IMG2_MIME_HDR, mime1);

	/* Valid HDR RGBE signature */
	const uint8_t hdr_rgbe[] = { '#',  '?',  'R',  'G', 'B', 'E', /* #?RGBE */
		                         '\n', 0x00, 0x00, 0x00 };

	img2_mime_t mime2 = img2_detect_mime(hdr_rgbe, sizeof(hdr_rgbe));
	ASSERT_EQUAL(IMG2_MIME_HDR, mime2);

	/* Verify img2_mime_name returns correct string */
	const char *name = img2_mime_name(IMG2_MIME_HDR);
	ASSERT_NOT_NULL(name);
	ASSERT_STR("HDR", name);
}

/**
 * @test Test PNM magic bytes detection
 *
 * Verifies that img2_detect_mime() correctly identifies PNM files
 * (PGM P5 and PPM P6 formats).
 */
CTEST(magic, detect_pnm)
{
	/* Valid PGM P5 (grayscale) signature */
	const uint8_t pnm_p5[] = { 'P',  '5', /* P5 signature */
		                       '\n', '1', '0', ' ', '1', '0' };

	img2_mime_t mime1 = img2_detect_mime(pnm_p5, sizeof(pnm_p5));
	ASSERT_EQUAL(IMG2_MIME_PNM, mime1);

	/* Valid PPM P6 (RGB) signature */
	const uint8_t pnm_p6[] = { 'P',  '6', /* P6 signature */
		                       '\n', '2', '0', ' ', '2', '0' };

	img2_mime_t mime2 = img2_detect_mime(pnm_p6, sizeof(pnm_p6));
	ASSERT_EQUAL(IMG2_MIME_PNM, mime2);

	/* All Netpbm variants P1..P7, with each accepted whitespace separator */
	const char separators[] = { ' ', '\t', '\n', '\r', '\v', '\f' };
	for (char fmt = '1'; fmt <= '7'; fmt++) {
		for (size_t i = 0; i < sizeof(separators); i++) {
			uint8_t hdr[] = { 'P', (uint8_t)fmt, (uint8_t)separators[i], '1', ' ', '1', '\n', '1' };
			ASSERT_EQUAL(IMG2_MIME_PNM, img2_detect_mime(hdr, sizeof(hdr)));
		}
	}

	/* P8 / P0 are not Netpbm */
	const uint8_t pnm_p8[] = { 'P', '8', '\n', '1', ' ', '1', '\n', '1' };
	ASSERT_NOT_EQUAL(IMG2_MIME_PNM, img2_detect_mime(pnm_p8, sizeof(pnm_p8)));
	const uint8_t pnm_p0[] = { 'P', '0', '\n', '1', ' ', '1', '\n', '1' };
	ASSERT_NOT_EQUAL(IMG2_MIME_PNM, img2_detect_mime(pnm_p0, sizeof(pnm_p0)));

	/* P6 not followed by whitespace is not Netpbm */
	const uint8_t pnm_p6_nows[] = { 'P', '6', 'x', '1', ' ', '1', '\n', '1' };
	ASSERT_NOT_EQUAL(IMG2_MIME_PNM, img2_detect_mime(pnm_p6_nows, sizeof(pnm_p6_nows)));

	/* Regression: '\n' (0x0A, TGA RLE type) at offset 2 and ' ' (0x20 = 32,
	 * valid TGA depth) at offset 16 must still be detected as PNM, not TGA */
	const char p6_tga_like[] = "P6\n# comment xxx yyy\n2 2\n255\n";
	ASSERT_EQUAL('\n', p6_tga_like[2]);
	ASSERT_EQUAL(' ', p6_tga_like[16]);
	ASSERT_EQUAL(IMG2_MIME_PNM, img2_detect_mime((const uint8_t *)p6_tga_like, sizeof(p6_tga_like) - 1));

	/* Verify img2_mime_name returns correct string */
	const char *name = img2_mime_name(IMG2_MIME_PNM);
	ASSERT_NOT_NULL(name);
	ASSERT_STR("PNM", name);
}

/**
 * @test Test TGA detection
 *
 * Verifies that img2_detect_mime() correctly identifies TGA files
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

	img2_mime_t mime = img2_detect_mime(tga_header, sizeof(tga_header));
	ASSERT_EQUAL(IMG2_MIME_TGA, mime);

	/* Verify img2_mime_name returns correct string */
	const char *name = img2_mime_name(IMG2_MIME_TGA);
	ASSERT_NOT_NULL(name);
	ASSERT_STR("TGA", name);
}

/**
 * @test Test SVG magic bytes detection
 *
 * Verifies that img2_detect_mime() correctly identifies SVG files
 * in various formats: direct <svg tag, XML declaration + <svg tag,
 * and BOM + XML declaration.
 */
CTEST(magic, detect_svg)
{
	/* SVG with direct <svg tag (most common in standalone SVG files) */
	const uint8_t svg_direct[] = {
		'<', 's', 'v', 'g', ' ', /* <svg  */
		'w', 'i', 'd', 't', 'h', '=', '"', '8', '0', '0', '"', /* width="800" */
		0x00
	};

	img2_mime_t mime1 = img2_detect_mime(svg_direct, sizeof(svg_direct));
	ASSERT_EQUAL(IMG2_MIME_SVG, mime1);

	/* SVG with XML declaration */
	const uint8_t svg_xml[] = {
		'<', '?', 'x', 'm', 'l', ' ', /* <?xml  */
		'v', 'e', 'r', 's', 'i', 'o', 'n', '=', '"', '1', '.', '0', '"', '?', '>', '\n', /* version="1.0"?> */
		'<', 's', 'v', 'g', ' ', /* <svg  */
		0x00
	};

	img2_mime_t mime2 = img2_detect_mime(svg_xml, sizeof(svg_xml));
	ASSERT_EQUAL(IMG2_MIME_SVG, mime2);

	/* SVG with BOM + XML declaration */
	const uint8_t svg_bom_xml[] = {
		0xEF, 0xBB, 0xBF, /* UTF-8 BOM */
		'<', '?', 'x', 'm', 'l', ' ', /* <?xml  */
		'v', 'e', 'r', 's', 'i', 'o', 'n', '=', '"', '1', '.', '0', '"', '?', '>', '\n', /* version="1.0"?> */
		'<', 's', 'v', 'g', ' ', /* <svg  */
		0x00
	};

	img2_mime_t mime3 = img2_detect_mime(svg_bom_xml, sizeof(svg_bom_xml));
	ASSERT_EQUAL(IMG2_MIME_SVG, mime3);

	/* Minimal SVG with just <svg tag */
	const uint8_t svg_minimal[] = { '<', 's', 'v', 'g', '>', 0x00, 0x00, 0x00 };

	img2_mime_t mime4 = img2_detect_mime(svg_minimal, sizeof(svg_minimal));
	ASSERT_EQUAL(IMG2_MIME_SVG, mime4);

	/* Verify img2_mime_name returns correct string */
	const char *name = img2_mime_name(IMG2_MIME_SVG);
	ASSERT_NOT_NULL(name);
	ASSERT_STR("SVG", name);
}

/**
 * @test Test HEIF / AVIF ftyp brand detection
 *
 * Verifies that img2_detect_mime() routes the HEVC HEIF brands to HEIF
 * and the AV1 brands to AVIF.
 */
CTEST(magic, detect_heif_avif_brands)
{
	/* HEIC: major brand 'heic' */
	const uint8_t heic[] = { 0x00, 0x00, 0x00, 0x18, 'f', 't', 'y', 'p', 'h', 'e', 'i', 'c', 0x00, 0x00, 0x00, 0x00, 'm', 'i', 'f', '1', 'h', 'e', 'i', 'c' };
	ASSERT_EQUAL(IMG2_MIME_HEIF, img2_detect_mime(heic, sizeof(heic)));

	/* AVIF: major brand 'avif' (compatible 'mif1' must not make it HEIF) */
	const uint8_t avif[] = { 0x00, 0x00, 0x00, 0x18, 'f', 't', 'y', 'p', 'a', 'v', 'i', 'f', 0x00, 0x00, 0x00, 0x00, 'm', 'i', 'f', '1', 'a', 'v', 'i', 'f' };
	ASSERT_EQUAL(IMG2_MIME_AVIF, img2_detect_mime(avif, sizeof(avif)));

	ASSERT_STR("HEIF", img2_mime_name(IMG2_MIME_HEIF));
	ASSERT_STR("AVIF", img2_mime_name(IMG2_MIME_AVIF));
}

/**
 * @test Test JPEG 2000 in HEIF detection
 *
 * Verifies that img2_detect_mime() identifies HEIF files carrying JPEG 2000
 * (ISO/IEC 15444-16 'j2ki' / 'j2is' brands) as HEIF, whether the brand is the
 * major brand or only a compatible brand.
 */
CTEST(magic, detect_heif_j2k)
{
	/* Header of src/tests/data/heif/j2k_64x48.heif (vendored libheif + OpenJPEG):
	 * major 'j2ki', compatible 'mif1' 'j2ki' 'miaf' */
	const uint8_t j2ki_major[] = { 0x00, 0x00, 0x00, 0x1c, 'f', 't', 'y', 'p', 'j', '2', 'k', 'i', 0x00, 0x00, 0x00, 0x00, 'm', 'i', 'f', '1', 'j', '2', 'k', 'i', 'm', 'i', 'a', 'f', 0x00, 0x00, 0x00, 0xee };
	ASSERT_EQUAL(IMG2_MIME_HEIF, img2_detect_mime(j2ki_major, sizeof(j2ki_major)));

	/* Image sequence: major 'j2is' */
	const uint8_t j2is_major[] = { 0x00, 0x00, 0x00, 0x14, 'f', 't', 'y', 'p', 'j', '2', 'i', 's', 0x00, 0x00, 0x00, 0x00, 'm', 's', 'f', '1' };
	ASSERT_EQUAL(IMG2_MIME_HEIF, img2_detect_mime(j2is_major, sizeof(j2is_major)));

	/* 'j2ki' only as a compatible brand behind a non-HEIF major brand */
	const uint8_t j2ki_compat[] = { 0x00, 0x00, 0x00, 0x18, 'f', 't', 'y', 'p', 'm', 'i', 'a', 'f', 0x00, 0x00, 0x00, 0x00, 'm', 'i', 'a', 'f', 'j', '2', 'k', 'i' };
	ASSERT_EQUAL(IMG2_MIME_HEIF, img2_detect_mime(j2ki_compat, sizeof(j2ki_compat)));

	/* 'j2is' only as a compatible brand */
	const uint8_t j2is_compat[] = { 0x00, 0x00, 0x00, 0x14, 'f', 't', 'y', 'p', 'm', 's', 'f', '1', 0x00, 0x00, 0x00, 0x00, 'j', '2', 'i', 's' };
	ASSERT_EQUAL(IMG2_MIME_HEIF, img2_detect_mime(j2is_compat, sizeof(j2is_compat)));

	/* 'j2ki' past the end of the ftyp box (box size 0x14) is not a brand */
	const uint8_t j2ki_outside[] = { 0x00, 0x00, 0x00, 0x14, 'f', 't', 'y', 'p', 'm', 'i', 'a', 'f', 0x00, 0x00, 0x00, 0x00, 'm', 'i', 'a', 'f', 'j', '2', 'k', 'i' };
	ASSERT_EQUAL(IMG2_MIME_UNKNOWN, img2_detect_mime(j2ki_outside, sizeof(j2ki_outside)));

	/* Truncated buffer: compatible brand cut off by len, not read past it */
	ASSERT_EQUAL(IMG2_MIME_UNKNOWN, img2_detect_mime(j2ki_compat, sizeof(j2ki_compat) - 1));

	/* Plain JPEG 2000 codestream / JP2 box is not HEIF */
	const uint8_t jp2[] = { 0x00, 0x00, 0x00, 0x0c, 'j', 'P', ' ', ' ', 0x0d, 0x0a, 0x87, 0x0a, 0x00, 0x00, 0x00, 0x14, 'f', 't', 'y', 'p', 'j', 'p', '2', ' ' };
	ASSERT_NOT_EQUAL(IMG2_MIME_HEIF, img2_detect_mime(jp2, sizeof(jp2)));
}

/**
 * @test Test SVG false positive prevention
 *
 * Verifies that img2_detect_mime() does NOT identify files as SVG
 * when they have XML declaration but no <svg tag.
 */
CTEST(magic, detect_svg_false_positive)
{
	/* XML file without <svg tag (should not be detected as SVG) */
	const uint8_t xml_no_svg[] = {
		'<', '?', 'x', 'm', 'l', ' ', /* <?xml  */
		'v', 'e', 'r', 's', 'i', 'o', 'n', '=', '"', '1', '.', '0', '"', '?', '>', '\n', /* version="1.0"?> */
		'<', 'r', 'o', 'o', 't', '>', /* <root> */
		0x00
	};

	img2_mime_t mime = img2_detect_mime(xml_no_svg, sizeof(xml_no_svg));
	ASSERT_EQUAL(IMG2_MIME_UNKNOWN, mime);
}
