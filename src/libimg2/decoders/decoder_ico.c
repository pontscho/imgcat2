/**
 * @file decoder_ico.c
 * @brief ICO/CUR (Icon/Cursor) image decoder
 *
 * Parses ICO and CUR file formats, extracts the best/largest image,
 * and decodes either PNG or DIB (Device Independent Bitmap) data.
 */

/* clang-format off */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

#include "decoder.h"

/* ICO directory structures */
#pragma pack(push, 1)
typedef struct {
	uint16_t reserved; /**< Always 0 */
	uint16_t type; /**< 1 = ICO, 2 = CUR */
	uint16_t count; /**< Number of images */
} ico_header_t;

typedef struct {
	uint8_t width; /**< Width (0 = 256) */
	uint8_t height; /**< Height (0 = 256) */
	uint8_t color_count; /**< 0 if >= 8bpp */
	uint8_t reserved; /**< Always 0 */
	uint16_t planes; /**< Color planes (ICO) or hotspot X (CUR) */
	uint16_t bit_count; /**< Bits per pixel (ICO) or hotspot Y (CUR) */
	uint32_t bytes_in_res; /**< Size of image data */
	uint32_t image_offset; /**< Offset to image data */
} ico_dir_entry_t;

typedef struct {
	uint32_t size; /**< Header size (40) */
	int32_t width; /**< Image width */
	int32_t height; /**< Image height (2x if AND mask) */
	uint16_t planes; /**< Always 1 */
	uint16_t bit_count; /**< Bits per pixel */
	uint32_t compression; /**< 0=BI_RGB, 1=BI_RLE8, 2=BI_RLE4 */
	uint32_t size_image; /**< Image data size */
	int32_t x_pels; /**< Horizontal resolution */
	int32_t y_pels; /**< Vertical resolution */
	uint32_t clr_used; /**< Colors in palette */
	uint32_t clr_important; /**< Important colors */
} bmp_info_header_t;
#pragma pack(pop)

/* Forward declarations for PNG decoder (external) */
extern image_t **decode_png(const uint8_t *data, size_t len, int *frame_count);

/* Static helper functions */
static int find_best_ico_entry(const ico_dir_entry_t *entries, int count);
static image_t *decode_ico_entry(const uint8_t *data, size_t len, bool is_cursor);
static image_t *decode_dib(const uint8_t *data, size_t len);
static bool parse_bmp_header(const uint8_t *data, size_t len, bmp_info_header_t *header);
static void apply_and_mask(image_t *img, const uint8_t *and_mask, uint32_t width, uint32_t height);
static void decode_dib_32bit(image_t *img, const uint8_t *pixels, uint32_t width, uint32_t height, uint32_t row_stride);
static void decode_dib_24bit(image_t *img, const uint8_t *pixels, uint32_t width, uint32_t height, uint32_t row_stride);
static void decode_dib_8bit(image_t *img, const uint8_t *pixels, const uint8_t *palette, uint32_t palette_size, uint32_t width, uint32_t height, uint32_t row_stride);
static void decode_dib_4bit(image_t *img, const uint8_t *pixels, const uint8_t *palette, uint32_t palette_size, uint32_t width, uint32_t height, uint32_t row_stride);
static void decode_dib_1bit(image_t *img, const uint8_t *pixels, const uint8_t *palette, uint32_t palette_size, uint32_t width, uint32_t height, uint32_t row_stride);

/**
 * @brief Find best ICO entry (largest area, highest bit depth)
 *
 * Selects the best image from ICO directory entries based on:
 * 1. Largest area (width × height)
 * 2. Highest bit depth (as tiebreaker)
 *
 * @param entries Array of ICO directory entries
 * @param count Number of entries
 * @return Index of best entry (0-based)
 */
static int find_best_ico_entry(const ico_dir_entry_t *entries, int count)
{
	int best_index = 0;
	uint32_t best_score = 0;

	for (int i = 0; i < count; i++) {
		// Handle width/height 0 = 256
		uint32_t width = (entries[i].width == 0) ? 256 : entries[i].width;
		uint32_t height = (entries[i].height == 0) ? 256 : entries[i].height;

		// Calculate score: (area << 16) | bit_count
		uint32_t area = width * height;
		uint32_t score = (area << 16) | entries[i].bit_count;

		if (score > best_score) {
			best_score = score;
			best_index = i;
		}
	}

	return best_index;
}

/**
 * @brief Parse BITMAPINFOHEADER from DIB data
 *
 * @param data DIB data buffer
 * @param len Length of data buffer
 * @param header Output: parsed header structure
 * @return true if valid header, false otherwise
 */
static bool parse_bmp_header(const uint8_t *data, size_t len, bmp_info_header_t *header)
{
	if (len < 40) {
		return false;
	}

	// Copy header (40 bytes)
	memcpy(header, data, 40);

	// Validate header
	if (header->size != 40) {
		return false;
	}
	if (header->planes != 1) {
		return false;
	}
	if (header->bit_count != 1 && header->bit_count != 4 && header->bit_count != 8 && header->bit_count != 24 && header->bit_count != 32) {
		return false;
	}
	if (header->compression != 0) { // Only BI_RGB supported
		return false;
	}

	return true;
}

/**
 * @brief Apply 1-bit AND mask for transparency
 *
 * @param img Image to modify
 * @param and_mask AND mask data (1 bit per pixel, 4-byte aligned rows)
 * @param width Image width
 * @param height Image height
 */
static void apply_and_mask(image_t *img, const uint8_t *and_mask, uint32_t width, uint32_t height)
{
	// AND mask row stride (1 bit per pixel, 4-byte aligned)
	uint32_t mask_stride = ((width + 31) / 32) * 4;

	for (uint32_t y = 0; y < height; y++) {
		// AND mask is bottom-up
		const uint8_t *mask_row = and_mask + ((height - 1 - y) * mask_stride);
		uint8_t *pixel_row = img->pixels + (y * width * 4);

		for (uint32_t x = 0; x < width; x++) {
			uint8_t byte = mask_row[x / 8];
			uint8_t bit = (byte >> (7 - (x % 8))) & 0x01;

			if (bit) {
				// Transparent pixel
				pixel_row[x * 4 + 3] = 0;
			} else {
				// Opaque pixel
				pixel_row[x * 4 + 3] = 255;
			}
		}
	}
}

/**
 * @brief Decode 32-bit DIB (BGRA to RGBA)
 *
 * @param img Output image
 * @param pixels Pixel data (BGRA format, bottom-up)
 * @param width Image width
 * @param height Image height
 * @param row_stride Bytes per row (4-byte aligned)
 */
static void decode_dib_32bit(image_t *img, const uint8_t *pixels, uint32_t width, uint32_t height, uint32_t row_stride)
{
	for (uint32_t y = 0; y < height; y++) {
		// Bottom-up to top-down
		const uint8_t *src = pixels + ((height - 1 - y) * row_stride);
		uint8_t *dst = img->pixels + (y * width * 4);

		for (uint32_t x = 0; x < width; x++) {
			uint8_t b = *src++;
			uint8_t g = *src++;
			uint8_t r = *src++;
			uint8_t a = *src++;

			*dst++ = r;
			*dst++ = g;
			*dst++ = b;
			*dst++ = a;
		}
	}
}

/**
 * @brief Decode 24-bit DIB (BGR to RGBA)
 *
 * @param img Output image
 * @param pixels Pixel data (BGR format, bottom-up)
 * @param width Image width
 * @param height Image height
 * @param row_stride Bytes per row (4-byte aligned)
 */
static void decode_dib_24bit(image_t *img, const uint8_t *pixels, uint32_t width, uint32_t height, uint32_t row_stride)
{
	for (uint32_t y = 0; y < height; y++) {
		// Bottom-up to top-down
		const uint8_t *src = pixels + ((height - 1 - y) * row_stride);
		uint8_t *dst = img->pixels + (y * width * 4);

		for (uint32_t x = 0; x < width; x++) {
			uint8_t b = *src++;
			uint8_t g = *src++;
			uint8_t r = *src++;

			*dst++ = r;
			*dst++ = g;
			*dst++ = b;
			*dst++ = 255; // Opaque (AND mask applies later)
		}
	}
}

/**
 * @brief Decode 8-bit indexed DIB
 *
 * @param img Output image
 * @param pixels Pixel data (8-bit indices, bottom-up)
 * @param palette Palette data (BGRA format)
 * @param palette_size Number of palette entries
 * @param width Image width
 * @param height Image height
 * @param row_stride Bytes per row (4-byte aligned)
 */
static void decode_dib_8bit(image_t *img, const uint8_t *pixels, const uint8_t *palette, uint32_t palette_size, uint32_t width, uint32_t height, uint32_t row_stride)
{
	for (uint32_t y = 0; y < height; y++) {
		// Bottom-up to top-down
		const uint8_t *src = pixels + ((height - 1 - y) * row_stride);
		uint8_t *dst = img->pixels + (y * width * 4);

		for (uint32_t x = 0; x < width; x++) {
			uint8_t index = *src++;

			if (index < palette_size) {
				const uint8_t *color = palette + (index * 4);
				*dst++ = color[2]; // R
				*dst++ = color[1]; // G
				*dst++ = color[0]; // B
				*dst++ = 255; // A
			} else {
				// Invalid index - black transparent
				*dst++ = 0;
				*dst++ = 0;
				*dst++ = 0;
				*dst++ = 0;
			}
		}
	}
}

/**
 * @brief Decode 4-bit indexed DIB (2 pixels per byte)
 *
 * @param img Output image
 * @param pixels Pixel data (4-bit indices, bottom-up)
 * @param palette Palette data (BGRA format)
 * @param palette_size Number of palette entries
 * @param width Image width
 * @param height Image height
 * @param row_stride Bytes per row (4-byte aligned)
 */
static void decode_dib_4bit(image_t *img, const uint8_t *pixels, const uint8_t *palette, uint32_t palette_size, uint32_t width, uint32_t height, uint32_t row_stride)
{
	for (uint32_t y = 0; y < height; y++) {
		// Bottom-up to top-down
		const uint8_t *src = pixels + ((height - 1 - y) * row_stride);
		uint8_t *dst = img->pixels + (y * width * 4);

		for (uint32_t x = 0; x < width; x += 2) {
			uint8_t byte = *src++;

			// High nibble (first pixel)
			uint8_t index1 = (byte >> 4) & 0x0f;
			// Low nibble (second pixel)
			uint8_t index2 = byte & 0x0f;

			// Decode first pixel
			if (index1 < palette_size) {
				const uint8_t *color = palette + (index1 * 4);
				*dst++ = color[2]; // R
				*dst++ = color[1]; // G
				*dst++ = color[0]; // B
				*dst++ = 255; // A
			} else {
				*dst++ = 0;
				*dst++ = 0;
				*dst++ = 0;
				*dst++ = 0;
			}

			// Decode second pixel (if not past width)
			if (x + 1 < width) {
				if (index2 < palette_size) {
					const uint8_t *color = palette + (index2 * 4);
					*dst++ = color[2]; // R
					*dst++ = color[1]; // G
					*dst++ = color[0]; // B
					*dst++ = 255; // A
				} else {
					*dst++ = 0;
					*dst++ = 0;
					*dst++ = 0;
					*dst++ = 0;
				}
			}
		}
	}
}

/**
 * @brief Decode 1-bit monochrome DIB (8 pixels per byte)
 *
 * @param img Output image
 * @param pixels Pixel data (1-bit indices, bottom-up)
 * @param palette Palette data (BGRA format)
 * @param palette_size Number of palette entries
 * @param width Image width
 * @param height Image height
 * @param row_stride Bytes per row (4-byte aligned)
 */
static void decode_dib_1bit(image_t *img, const uint8_t *pixels, const uint8_t *palette, uint32_t palette_size, uint32_t width, uint32_t height, uint32_t row_stride)
{
	for (uint32_t y = 0; y < height; y++) {
		// Bottom-up to top-down
		const uint8_t *src = pixels + ((height - 1 - y) * row_stride);
		uint8_t *dst = img->pixels + (y * width * 4);

		for (uint32_t x = 0; x < width; x += 8) {
			uint8_t byte = *src++;

			for (int bit = 7; bit >= 0 && (x + (7 - bit)) < width; bit--) {
				uint8_t index = (byte >> bit) & 0x01;

				if (index < palette_size) {
					const uint8_t *color = palette + (index * 4);
					*dst++ = color[2]; // R
					*dst++ = color[1]; // G
					*dst++ = color[0]; // B
					*dst++ = 255; // A
				} else {
					*dst++ = 0;
					*dst++ = 0;
					*dst++ = 0;
					*dst++ = 0;
				}
			}
		}
	}
}

/**
 * @brief Decode DIB (Device Independent Bitmap) entry
 *
 * @param data DIB data buffer
 * @param len Length of data buffer
 * @return Decoded image, or NULL on error
 */
static image_t *decode_dib(const uint8_t *data, size_t len)
{
	// Parse BMP header
	bmp_info_header_t header;
	if (!parse_bmp_header(data, len, &header)) {
		fprintf(stderr, "Error: Failed to parse BMP header\n");
		return NULL;
	}

	// Calculate actual dimensions
	uint32_t width = (uint32_t)header.width;
	uint32_t height = (uint32_t)header.height;

	// Check if AND mask is present (height is 2x actual height)
	bool has_and_mask = false;
	if (height >= width * 2) {
		has_and_mask = true;
		height = height / 2;
	}

	// Determine palette size
	uint32_t palette_size = 0;
	if (header.bit_count <= 8) {
		if (header.clr_used > 0) {
			palette_size = header.clr_used;
		} else {
			palette_size = 1 << header.bit_count; // 2^bit_count
		}
	}

	// Validate palette bounds
	size_t palette_offset = 40;
	size_t palette_bytes = palette_size * 4;
	if (palette_offset + palette_bytes > len) {
		fprintf(stderr, "Error: Invalid palette size\n");
		return NULL;
	}

	const uint8_t *palette = data + palette_offset;

	// Calculate row stride (4-byte aligned)
	uint32_t row_stride = ((width * header.bit_count + 31) / 32) * 4;

	// Calculate pixel data offset
	size_t pixel_offset = palette_offset + palette_bytes;
	size_t pixel_bytes = (size_t)row_stride * height;

	if (pixel_offset + pixel_bytes > len) {
		fprintf(stderr, "Error: Invalid pixel data size\n");
		return NULL;
	}

	const uint8_t *pixels = data + pixel_offset;

	// Create output image
	image_t *img = image_create(width, height);
	if (img == NULL) {
		fprintf(stderr, "Error: Failed to create image\n");
		return NULL;
	}

	// Decode based on bit depth
	switch (header.bit_count) {
		case 32: decode_dib_32bit(img, pixels, width, height, row_stride); break;
		case 24: decode_dib_24bit(img, pixels, width, height, row_stride); break;
		case 8: decode_dib_8bit(img, pixels, palette, palette_size, width, height, row_stride); break;
		case 4: decode_dib_4bit(img, pixels, palette, palette_size, width, height, row_stride); break;
		case 1: decode_dib_1bit(img, pixels, palette, palette_size, width, height, row_stride); break;
		default:
			fprintf(stderr, "Error: Unsupported bit depth: %u\n", header.bit_count);
			image_destroy(img);
			return NULL;
	}

	// Apply AND mask if present
	if (has_and_mask) {
		uint32_t and_mask_stride = ((width + 31) / 32) * 4;
		size_t and_mask_offset = pixel_offset + pixel_bytes;
		size_t and_mask_bytes = (size_t)and_mask_stride * height;

		if (and_mask_offset + and_mask_bytes <= len) {
			const uint8_t *and_mask = data + and_mask_offset;
			apply_and_mask(img, and_mask, width, height);
		}
	}

	return img;
}

/**
 * @brief Decode single ICO entry (PNG or DIB)
 *
 * @param data Image data buffer
 * @param len Length of data buffer
 * @param is_cursor true if CUR format, false if ICO (currently unused)
 * @return Decoded image, or NULL on error
 */
static image_t *decode_ico_entry(const uint8_t *data, size_t len, bool is_cursor)
{
	(void)is_cursor; // Currently unused - both ICO and CUR decode the same way

	// Check if PNG format
	const uint8_t png_sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
	if (len >= 8 && memcmp(data, png_sig, 8) == 0) {
		// PNG format - use existing decoder
		int frame_count = 0;
		image_t **frames = decode_png(data, len, &frame_count);
		if (frames == NULL || frame_count < 1) {
			return NULL;
		}

		// Extract first frame
		image_t *img = frames[0];
		free(frames);
		return img;
	}

	// DIB format - custom parsing
	return decode_dib(data, len);
}

/**
 * @brief Main entry point for ICO/CUR decoding
 *
 * @param data Raw ICO/CUR file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (single frame)
 * @return Array with single image_t*, or NULL on error
 */
image_t **decode_ico(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_ico\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Validate minimum size (6 byte header + 16 byte entry)
	if (len < 22) {
		fprintf(stderr, "Error: ICO file too small\n");
		return NULL;
	}

	// Parse ICO header
	ico_header_t header;
	memcpy(&header, data, 6);

	// Validate header
	if (header.reserved != 0) {
		fprintf(stderr, "Error: Invalid ICO reserved field\n");
		return NULL;
	}
	if (header.type != 1 && header.type != 2) {
		fprintf(stderr, "Error: Invalid ICO type field\n");
		return NULL;
	}
	if (header.count == 0 || header.count > 255) {
		fprintf(stderr, "Error: Invalid ICO image count\n");
		return NULL;
	}

	// Validate entries array size
	size_t entries_size = (size_t)header.count * sizeof(ico_dir_entry_t);
	if (len < 6 + entries_size) {
		fprintf(stderr, "Error: ICO file too small for entries\n");
		return NULL;
	}

	// Parse entries array
	const ico_dir_entry_t *entries = (const ico_dir_entry_t *)(data + 6);

	// Find best entry
	int best_idx = find_best_ico_entry(entries, header.count);

	// Validate selected entry
	const ico_dir_entry_t *entry = &entries[best_idx];
	if (entry->image_offset >= len || entry->bytes_in_res == 0) {
		fprintf(stderr, "Error: Invalid ICO entry offset or size\n");
		return NULL;
	}
	if (entry->image_offset + entry->bytes_in_res > len) {
		fprintf(stderr, "Error: ICO entry extends beyond file\n");
		return NULL;
	}

	// Decode selected entry
	const uint8_t *entry_data = data + entry->image_offset;
	size_t entry_size = entry->bytes_in_res;
	bool is_cursor = (header.type == 2);

	image_t *img = decode_ico_entry(entry_data, entry_size, is_cursor);
	if (img == NULL) {
		fprintf(stderr, "Error: Failed to decode ICO entry\n");
		return NULL;
	}

	// Allocate frames array (single frame)
	image_t **frames = (image_t **)malloc(sizeof(image_t *));
	if (frames == NULL) {
		fprintf(stderr, "Error: Failed to allocate frames array\n");
		image_destroy(img);
		return NULL;
	}

	frames[0] = img;
	*frame_count = 1;

	return frames;
}
