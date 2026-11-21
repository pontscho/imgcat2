/**
 * @file decoder_pnm.c
 * @brief Native Netpbm (PBM/PGM/PPM/PAM) image decoder
 *
 * Decodes all Netpbm formats to RGBA8888:
 * - P1 / P4: PBM bitmap (ASCII / binary packed bits), 1 = black, 0 = white
 * - P2 / P5: PGM graymap (ASCII / binary)
 * - P3 / P6: PPM pixmap (ASCII / binary)
 * - P7: PAM arbitrary map (binary), depth 1-4 (GRAYSCALE, BLACKANDWHITE,
 *   GRAYSCALE_ALPHA, BLACKANDWHITE_ALPHA, RGB, RGB_ALPHA)
 *
 * Samples with maxval >= 256 are 2-byte big-endian in binary formats.
 * All samples are scaled to 8 bit with rounding. Samples exceeding maxval
 * are clamped to maxval (robustness over strictness). Only the first image
 * of a multi-image file is decoded; trailing data is ignored.
 *
 * The input buffer is NOT assumed to be NUL-terminated; every read is
 * bounded by the buffer length.
 */

/* clang-format off */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

#include "decoder.h"
#include "../core/log.h"

/** Maximum valid maxval for Netpbm formats */
#define PNM_MAXVAL_LIMIT 65535U

/**
 * @brief Bounded byte reader over the input buffer
 */
typedef struct {
	const uint8_t *data; /**< Input buffer */
	size_t len; /**< Buffer length in bytes */
	size_t pos; /**< Current read offset */
} pnm_reader_t;

/**
 * @brief Parsed Netpbm header
 */
typedef struct {
	char format; /**< Format digit '1'..'7' */
	uint32_t width; /**< Image width in pixels */
	uint32_t height; /**< Image height in pixels */
	uint32_t maxval; /**< Maximum sample value (1 for PBM) */
	uint32_t channels; /**< Samples per pixel (1-4) */
} pnm_header_t;

/**
 * @brief Check whether a byte is Netpbm whitespace
 *
 * @param c Byte to test
 * @return true for space, \\t, \\n, \\r, \\v, \\f
 */
static bool pnm_is_space(uint8_t c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

/**
 * @brief Skip whitespace and '#' comments (comment runs to end of line)
 *
 * @param r Reader
 */
static void pnm_skip_space(pnm_reader_t *r)
{
	while (r->pos < r->len) {
		uint8_t c = r->data[r->pos];
		if (pnm_is_space(c)) {
			r->pos++;
		} else if (c == '#') {
			while (r->pos < r->len && r->data[r->pos] != '\n' && r->data[r->pos] != '\r') {
				r->pos++;
			}
		} else {
			break;
		}
	}
}

/**
 * @brief Read an unsigned decimal integer, skipping leading whitespace/comments
 *
 * Accumulates in 64-bit arithmetic with an early cutoff so that overlong
 * digit strings cannot overflow.
 *
 * @param r Reader
 * @param out Output value
 * @return true on success, false on EOF, non-digit or value > UINT32_MAX
 */
static bool pnm_read_uint(pnm_reader_t *r, uint32_t *out)
{
	pnm_skip_space(r);

	if (r->pos >= r->len || r->data[r->pos] < '0' || r->data[r->pos] > '9') {
		return false;
	}

	uint64_t value = 0;
	while (r->pos < r->len && r->data[r->pos] >= '0' && r->data[r->pos] <= '9') {
		value = value * 10 + (uint64_t)(r->data[r->pos] - '0');
		if (value > UINT32_MAX) {
			return false;
		}
		r->pos++;
	}

	*out = (uint32_t)value;
	return true;
}

/**
 * @brief Multiply two sizes with overflow detection
 *
 * @param a First factor
 * @param b Second factor
 * @param out Output product
 * @return true on success, false on overflow
 */
static bool pnm_mul_size(size_t a, size_t b, size_t *out)
{
	if (a != 0 && b > SIZE_MAX / a) {
		return false;
	}
	*out = a * b;
	return true;
}

/**
 * @brief Scale a sample to 8 bit with rounding, clamping to maxval
 *
 * @param v Sample value
 * @param maxval Maximum sample value (1..65535)
 * @return 8-bit value
 */
static uint8_t pnm_scale(uint32_t v, uint32_t maxval)
{
	if (v > maxval) {
		v = maxval;
	}
	if (maxval == 255) {
		return (uint8_t)v;
	}
	return (uint8_t)((v * 255U + maxval / 2U) / maxval);
}

/**
 * @brief Store scaled samples of one pixel as RGBA
 *
 * @param dst Destination RGBA pixel (4 bytes)
 * @param s Scaled samples
 * @param channels Number of samples (1 = gray, 2 = gray+alpha, 3 = RGB, 4 = RGBA)
 */
static void pnm_store_pixel(uint8_t *dst, const uint8_t *s, uint32_t channels)
{
	switch (channels) {
		case 1:
			dst[0] = dst[1] = dst[2] = s[0];
			dst[3] = 255;
			break;
		case 2:
			dst[0] = dst[1] = dst[2] = s[0];
			dst[3] = s[1];
			break;
		case 3:
			dst[0] = s[0];
			dst[1] = s[1];
			dst[2] = s[2];
			dst[3] = 255;
			break;
		default:
			dst[0] = s[0];
			dst[1] = s[1];
			dst[2] = s[2];
			dst[3] = s[3];
			break;
	}
}

/**
 * @brief Parse a P1-P6 header (magic already consumed)
 *
 * For binary formats (P4, P5, P6) consumes exactly one whitespace byte after
 * the last header token, leaving the reader at the start of the raster.
 *
 * @param r Reader positioned after the 2-byte magic
 * @param hdr Header (format already set)
 * @return true on success, false on malformed header
 */
static bool pnm_parse_header(pnm_reader_t *r, pnm_header_t *hdr)
{
	bool is_bitmap = (hdr->format == '1' || hdr->format == '4');
	bool is_binary = (hdr->format >= '4');

	if (!pnm_read_uint(r, &hdr->width) || !pnm_read_uint(r, &hdr->height)) {
		img2_log(IMG2_LOG_ERROR, "Error: PNM: invalid width/height in header");
		return false;
	}

	if (is_bitmap) {
		hdr->maxval = 1;
	} else if (!pnm_read_uint(r, &hdr->maxval)) {
		img2_log(IMG2_LOG_ERROR, "Error: PNM: invalid maxval in header");
		return false;
	}

	hdr->channels = (hdr->format == '3' || hdr->format == '6') ? 3 : 1;

	if (is_binary) {
		// Exactly one whitespace byte separates the header from the raster
		if (r->pos >= r->len || !pnm_is_space(r->data[r->pos])) {
			img2_log(IMG2_LOG_ERROR, "Error: PNM: missing whitespace before raster data");
			return false;
		}
		r->pos++;
	}

	return true;
}

/**
 * @brief Compare a token against a NUL-terminated keyword
 *
 * @param tok Token start
 * @param tok_len Token length
 * @param kw Keyword
 * @return true if equal
 */
static bool pnm_token_eq(const uint8_t *tok, size_t tok_len, const char *kw)
{
	size_t kw_len = strlen(kw);
	return tok_len == kw_len && memcmp(tok, kw, kw_len) == 0;
}

/**
 * @brief Parse a P7 (PAM) header (magic already consumed)
 *
 * Recognises WIDTH, HEIGHT, DEPTH, MAXVAL, TUPLTYPE and ENDHDR lines plus
 * '#' comments. Leaves the reader positioned at the start of the raster.
 *
 * @param r Reader positioned after "P7"
 * @param hdr Output header
 * @return true on success, false on malformed or unsupported header
 */
static bool pnm_parse_pam_header(pnm_reader_t *r, pnm_header_t *hdr)
{
	bool have_width = false, have_height = false, have_depth = false, have_maxval = false;
	uint32_t depth = 0;
	const uint8_t *tupltype = NULL;
	size_t tupltype_len = 0;

	for (;;) {
		pnm_skip_space(r);
		if (r->pos >= r->len) {
			img2_log(IMG2_LOG_ERROR, "Error: PAM: header ended without ENDHDR");
			return false;
		}

		// Read keyword token
		const uint8_t *tok = r->data + r->pos;
		size_t tok_len = 0;
		while (r->pos < r->len && !pnm_is_space(r->data[r->pos])) {
			r->pos++;
			tok_len++;
		}

		if (pnm_token_eq(tok, tok_len, "ENDHDR")) {
			// Rest of the ENDHDR line (normally empty) up to and including '\n'
			while (r->pos < r->len && r->data[r->pos] != '\n') {
				if (!pnm_is_space(r->data[r->pos])) {
					img2_log(IMG2_LOG_ERROR, "Error: PAM: garbage after ENDHDR");
					return false;
				}
				r->pos++;
			}
			if (r->pos >= r->len) {
				img2_log(IMG2_LOG_ERROR, "Error: PAM: missing newline after ENDHDR");
				return false;
			}
			r->pos++;
			break;
		} else if (pnm_token_eq(tok, tok_len, "WIDTH")) {
			have_width = pnm_read_uint(r, &hdr->width);
			if (!have_width) {
				img2_log(IMG2_LOG_ERROR, "Error: PAM: invalid WIDTH");
				return false;
			}
		} else if (pnm_token_eq(tok, tok_len, "HEIGHT")) {
			have_height = pnm_read_uint(r, &hdr->height);
			if (!have_height) {
				img2_log(IMG2_LOG_ERROR, "Error: PAM: invalid HEIGHT");
				return false;
			}
		} else if (pnm_token_eq(tok, tok_len, "DEPTH")) {
			have_depth = pnm_read_uint(r, &depth);
			if (!have_depth) {
				img2_log(IMG2_LOG_ERROR, "Error: PAM: invalid DEPTH");
				return false;
			}
		} else if (pnm_token_eq(tok, tok_len, "MAXVAL")) {
			have_maxval = pnm_read_uint(r, &hdr->maxval);
			if (!have_maxval) {
				img2_log(IMG2_LOG_ERROR, "Error: PAM: invalid MAXVAL");
				return false;
			}
		} else if (pnm_token_eq(tok, tok_len, "TUPLTYPE")) {
			// Value is the rest of the line, trimmed
			while (r->pos < r->len && (r->data[r->pos] == ' ' || r->data[r->pos] == '\t')) {
				r->pos++;
			}
			tupltype = r->data + r->pos;
			while (r->pos < r->len && r->data[r->pos] != '\n' && r->data[r->pos] != '\r') {
				r->pos++;
			}
			tupltype_len = (size_t)(r->data + r->pos - tupltype);
			while (tupltype_len > 0 && pnm_is_space(tupltype[tupltype_len - 1])) {
				tupltype_len--;
			}
		} else {
			img2_log(IMG2_LOG_ERROR, "Error: PAM: unrecognized header line");
			return false;
		}
	}

	if (!have_width || !have_height || !have_depth || !have_maxval) {
		img2_log(IMG2_LOG_ERROR, "Error: PAM: header missing WIDTH, HEIGHT, DEPTH or MAXVAL");
		return false;
	}

	if (depth < 1 || depth > 4) {
		img2_log(IMG2_LOG_ERROR, "Error: PAM: unsupported DEPTH %u", depth);
		return false;
	}

	// Validate known tuple types against depth; unknown/missing types are
	// interpreted from depth alone (1=gray, 2=gray+alpha, 3=RGB, 4=RGBA)
	if (tupltype != NULL) {
		uint32_t expected = 0;
		if (pnm_token_eq(tupltype, tupltype_len, "GRAYSCALE") || pnm_token_eq(tupltype, tupltype_len, "BLACKANDWHITE")) {
			expected = 1;
		} else if (pnm_token_eq(tupltype, tupltype_len, "GRAYSCALE_ALPHA") || pnm_token_eq(tupltype, tupltype_len, "BLACKANDWHITE_ALPHA")) {
			expected = 2;
		} else if (pnm_token_eq(tupltype, tupltype_len, "RGB")) {
			expected = 3;
		} else if (pnm_token_eq(tupltype, tupltype_len, "RGB_ALPHA")) {
			expected = 4;
		}
		if (expected != 0 && expected != depth) {
			img2_log(IMG2_LOG_ERROR, "Error: PAM: TUPLTYPE does not match DEPTH %u", depth);
			return false;
		}
	}

	hdr->channels = depth;
	return true;
}

/**
 * @brief Decode binary raster (P5, P6, P7) into image
 *
 * @param r Reader positioned at raster start
 * @param hdr Parsed header
 * @param img Destination image (dimensions match header)
 *
 * @note Caller must have verified that the full raster is available
 */
static void pnm_decode_binary(pnm_reader_t *r, const pnm_header_t *hdr, image_t *img)
{
	size_t bps = (hdr->maxval < 256) ? 1 : 2;
	size_t pixel_count = (size_t)hdr->width * (size_t)hdr->height;
	const uint8_t *src = r->data + r->pos;
	uint8_t *dst = img->pixels;
	uint8_t s[4];

	for (size_t i = 0; i < pixel_count; i++) {
		for (uint32_t c = 0; c < hdr->channels; c++) {
			uint32_t v;
			if (bps == 1) {
				v = src[0];
			} else {
				v = ((uint32_t)src[0] << 8) | (uint32_t)src[1];
			}
			src += bps;
			s[c] = pnm_scale(v, hdr->maxval);
		}
		pnm_store_pixel(dst, s, hdr->channels);
		dst += 4;
	}
}

/**
 * @brief Decode P4 packed bitmap raster into image
 *
 * Rows are padded to a whole byte; bits are MSB first; 1 = black.
 *
 * @param r Reader positioned at raster start
 * @param hdr Parsed header
 * @param img Destination image
 */
static void pnm_decode_p4(pnm_reader_t *r, const pnm_header_t *hdr, image_t *img)
{
	size_t row_bytes = ((size_t)hdr->width + 7) / 8;
	const uint8_t *row = r->data + r->pos;
	uint8_t *dst = img->pixels;

	for (uint32_t y = 0; y < hdr->height; y++) {
		for (uint32_t x = 0; x < hdr->width; x++) {
			uint8_t bit = (uint8_t)((row[x >> 3] >> (7 - (x & 7))) & 1U);
			uint8_t v = bit ? 0 : 255;
			dst[0] = dst[1] = dst[2] = v;
			dst[3] = 255;
			dst += 4;
		}
		row += row_bytes;
	}
}

/**
 * @brief Decode ASCII raster (P1, P2, P3) into image
 *
 * @param r Reader positioned after header
 * @param hdr Parsed header
 * @param img Destination image
 * @return true on success, false on truncated or malformed raster
 */
static bool pnm_decode_ascii(pnm_reader_t *r, const pnm_header_t *hdr, image_t *img)
{
	size_t pixel_count = (size_t)hdr->width * (size_t)hdr->height;
	uint8_t *dst = img->pixels;
	uint8_t s[4];

	for (size_t i = 0; i < pixel_count; i++) {
		if (hdr->format == '1') {
			// P1: single '0'/'1' characters, whitespace optional between them
			pnm_skip_space(r);
			if (r->pos >= r->len || (r->data[r->pos] != '0' && r->data[r->pos] != '1')) {
				return false;
			}
			uint8_t v = (r->data[r->pos] == '1') ? 0 : 255;
			r->pos++;
			dst[0] = dst[1] = dst[2] = v;
			dst[3] = 255;
		} else {
			for (uint32_t c = 0; c < hdr->channels; c++) {
				uint32_t v;
				if (!pnm_read_uint(r, &v)) {
					return false;
				}
				s[c] = pnm_scale(v, hdr->maxval);
			}
			pnm_store_pixel(dst, s, hdr->channels);
		}
		dst += 4;
	}

	return true;
}

/**
 * @brief Decode Netpbm image (PBM/PGM/PPM P1-P6, PAM P7)
 *
 * @param data Raw Netpbm file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded (always 1)
 * @return Array with single image_t*, or NULL on error
 *
 * @note Output format is RGBA8888
 * @note Only the first image of a multi-image stream is decoded
 */
image_t **decode_netpbm(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		return NULL;
	}

	*frame_count = 0;

	if (len < 3 || data[0] != 'P' || data[1] < '1' || data[1] > '7') {
		img2_log(IMG2_LOG_ERROR, "Error: Not a Netpbm (PNM/PAM) image");
		return NULL;
	}

	pnm_reader_t r = { .data = data, .len = len, .pos = 2 };
	pnm_header_t hdr = { .format = (char)data[1] };

	// Magic must be followed by whitespace (or a comment for P1-P6)
	if (!pnm_is_space(data[2]) && !(hdr.format != '7' && data[2] == '#')) {
		img2_log(IMG2_LOG_ERROR, "Error: Not a Netpbm (PNM/PAM) image");
		return NULL;
	}

	bool ok = (hdr.format == '7') ? pnm_parse_pam_header(&r, &hdr) : pnm_parse_header(&r, &hdr);
	if (!ok) {
		return NULL;
	}

	// Validate header values before any size arithmetic
	if (hdr.width == 0 || hdr.height == 0) {
		img2_log(IMG2_LOG_ERROR, "Error: PNM: invalid dimensions %ux%u", hdr.width, hdr.height);
		return NULL;
	}
	if (hdr.width > IMAGE_MAX_DIMENSION || hdr.height > IMAGE_MAX_DIMENSION) {
		img2_log(IMG2_LOG_ERROR, "Error: PNM: dimensions %ux%u exceed limit", hdr.width, hdr.height);
		return NULL;
	}
	if (hdr.maxval < 1 || hdr.maxval > PNM_MAXVAL_LIMIT) {
		img2_log(IMG2_LOG_ERROR, "Error: PNM: invalid maxval %u", hdr.maxval);
		return NULL;
	}

	// Minimum number of raster bytes required (exact for binary formats,
	// lower bound of one byte per sample for ASCII formats). Checked before
	// allocating so tiny truncated files cannot trigger huge allocations.
	size_t pixel_count, needed;
	size_t remaining = len - r.pos;
	if (!pnm_mul_size(hdr.width, hdr.height, &pixel_count)) {
		img2_log(IMG2_LOG_ERROR, "Error: PNM: image size overflow");
		return NULL;
	}

	bool size_ok;
	if (hdr.format == '4') {
		size_t row_bytes = ((size_t)hdr.width + 7) / 8;
		size_ok = pnm_mul_size(row_bytes, hdr.height, &needed);
	} else {
		size_t bps = (hdr.format >= '5' && hdr.maxval >= 256) ? 2 : 1;
		size_ok = pnm_mul_size(pixel_count, hdr.channels, &needed) && pnm_mul_size(needed, bps, &needed);
	}
	if (!size_ok) {
		img2_log(IMG2_LOG_ERROR, "Error: PNM: image size overflow");
		return NULL;
	}
	if (remaining < needed) {
		img2_log(IMG2_LOG_ERROR, "Error: PNM: truncated raster data (need %zu bytes, have %zu)", needed, remaining);
		return NULL;
	}

	image_t *img = image_create(hdr.width, hdr.height);
	if (img == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create image_t structure");
		return NULL;
	}

	switch (hdr.format) {
		case '4':
			pnm_decode_p4(&r, &hdr, img);
			ok = true;
			break;
		case '5':
		case '6':
		case '7':
			pnm_decode_binary(&r, &hdr, img);
			ok = true;
			break;
		default:
			ok = pnm_decode_ascii(&r, &hdr, img);
			break;
	}

	if (!ok) {
		img2_log(IMG2_LOG_ERROR, "Error: PNM: truncated or malformed raster data");
		image_destroy(img);
		return NULL;
	}

	image_t **frames = (image_t **)malloc(sizeof(image_t *));
	if (frames == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate frames array");
		image_destroy(img);
		return NULL;
	}

	frames[0] = img;
	*frame_count = 1;

	return frames;
}
