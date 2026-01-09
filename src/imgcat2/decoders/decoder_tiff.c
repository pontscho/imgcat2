/**
 * @file decoder_tiff.c
 * @brief TIFF decoder implementation using libtiff
 *
 * Decodes TIFF images (both single-page and multi-page) to RGBA8888 format.
 * Handles all TIFF format variations including CMYK, YCbCr, palette, etc.
 */

/* clang-format off */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tiffio.h>
/* clang-format on */

#include "decoder.h"

/** Maximum number of TIFF frames to decode (prevents DoS) */
#define MAX_TIFF_FRAMES 200

/**
 * @brief Memory-based I/O context for libtiff
 *
 * Used with TIFFClientOpen to read TIFF data from memory buffer
 * instead of files. Tracks read position and buffer bounds.
 */
typedef struct {
	const uint8_t *data; /**< Pointer to TIFF data buffer */
	size_t size; /**< Size of buffer in bytes */
	size_t offset; /**< Current read offset */
} tiff_memory_t;

/**
 * @brief Custom read callback for TIFFClientOpen
 *
 * Reads data from memory buffer into destination buffer.
 * Advances read offset and handles EOF conditions.
 *
 * @param handle Opaque handle (tiff_memory_t*)
 * @param buf Destination buffer
 * @param size Number of bytes to read
 * @return Bytes read, or 0 if EOF
 */
static tmsize_t tiff_read_proc(thandle_t handle, void *buf, tmsize_t size)
{
	tiff_memory_t *mem = (tiff_memory_t *)handle;
	if (mem->offset >= mem->size) {
		return 0;
	}

	size_t available = mem->size - mem->offset;
	size_t to_read = ((size_t)size < available) ? (size_t)size : available;

	memcpy(buf, mem->data + mem->offset, to_read);
	mem->offset += to_read;
	return (tmsize_t)to_read;
}

/**
 * @brief Custom write callback for TIFFClientOpen
 *
 * Write operations are not supported (read-only decoder).
 *
 * @param handle Opaque handle (unused)
 * @param buf Source buffer (unused)
 * @param size Number of bytes (unused)
 * @return -1 (error, write not supported)
 */
static tmsize_t tiff_write_proc(thandle_t handle, void *buf, tmsize_t size)
{
	(void)handle;
	(void)buf;
	(void)size;
	return -1;
}

/**
 * @brief Custom seek callback for TIFFClientOpen
 *
 * Seeks to specified position in memory buffer.
 * Handles SEEK_SET, SEEK_CUR, SEEK_END modes.
 *
 * @param handle Opaque handle (tiff_memory_t*)
 * @param offset Seek offset
 * @param whence Seek mode (SEEK_SET, SEEK_CUR, SEEK_END)
 * @return New offset position
 */
static toff_t tiff_seek_proc(thandle_t handle, toff_t offset, int whence)
{
	tiff_memory_t *mem = (tiff_memory_t *)handle;

	switch (whence) {
		case SEEK_SET: mem->offset = (size_t)offset; break;
		case SEEK_CUR: mem->offset += (size_t)offset; break;
		case SEEK_END: mem->offset = mem->size + (size_t)offset; break;
	}

	if (mem->offset > mem->size) {
		mem->offset = mem->size;
	}

	return (toff_t)mem->offset;
}

/**
 * @brief Custom close callback for TIFFClientOpen
 *
 * No-op since we don't own the memory buffer.
 *
 * @param handle Opaque handle (unused)
 * @return 0 (success)
 */
static int tiff_close_proc(thandle_t handle)
{
	(void)handle;
	return 0;
}

/**
 * @brief Custom size callback for TIFFClientOpen
 *
 * Returns total size of memory buffer.
 *
 * @param handle Opaque handle (tiff_memory_t*)
 * @return Buffer size in bytes
 */
static toff_t tiff_size_proc(thandle_t handle)
{
	tiff_memory_t *mem = (tiff_memory_t *)handle;
	return (toff_t)mem->size;
}

/**
 * @brief Check if TIFF has multiple pages
 *
 * Quickly checks if a TIFF file contains multiple directories (pages)
 * without fully decoding the image data. Useful for determining whether
 * to use static or multi-page decoder.
 *
 * @param data Raw TIFF file data
 * @param len Size of data in bytes
 * @return true if TIFF has multiple pages, false otherwise or on error
 *
 * @note Returns false if data is invalid or not a TIFF
 * @note Does not validate that pages are decodable, only checks count
 */
static bool tiff_is_multipage(const uint8_t *data, size_t len)
{
	if (data == NULL || len == 0) {
		return false;
	}

	tiff_memory_t mem = { data, len, 0 };

	TIFF *tif = TIFFClientOpen("memory", "r", (thandle_t)&mem, tiff_read_proc, tiff_write_proc, tiff_seek_proc, tiff_close_proc, tiff_size_proc, NULL, NULL);
	if (tif == NULL) {
		return false;
	}

	tdir_t dir_count = TIFFNumberOfDirectories(tif);

	TIFFClose(tif);

	return dir_count > 1;
}

/**
 * @brief Decode static TIFF image (single page)
 *
 * Decodes a single-page TIFF image to RGBA8888 format.
 * For multi-page TIFF files, use decode_tiff_multipage().
 *
 * @param data Raw TIFF file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (single frame)
 * @return Array with single image_t*, or NULL on error
 *
 * @note Output format is RGBA8888
 * @note Handles all TIFF format variations (CMYK, YCbCr, palette, etc.)
 */
static image_t **decode_tiff_static(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_tiff_static\n");
		return NULL;
	}

	*frame_count = 0;

	tiff_memory_t mem = { data, len, 0 };

	TIFF *tif = TIFFClientOpen("memory", "r", (thandle_t)&mem, tiff_read_proc, tiff_write_proc, tiff_seek_proc, tiff_close_proc, tiff_size_proc, NULL, NULL);
	if (tif == NULL) {
		fprintf(stderr, "Error: Failed to open TIFF\n");
		return NULL;
	}

	uint32_t width, height;
	TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
	TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);

	// Allocate raster buffer (ABGR format)
	uint32_t *raster = (uint32_t *)_TIFFmalloc(width * height * sizeof(uint32_t));
	if (raster == NULL) {
		fprintf(stderr, "Error: Failed to allocate raster buffer\n");
		TIFFClose(tif);
		return NULL;
	}

	// Read RGBA image (libtiff handles all format conversions)
	if (!TIFFReadRGBAImageOriented(tif, width, height, raster, ORIENTATION_TOPLEFT, 0)) {
		fprintf(stderr, "Error: Failed to read TIFF image\n");
		_TIFFfree(raster);
		TIFFClose(tif);
		return NULL;
	}

	// Create image_t
	image_t *img = image_create(width, height);
	if (img == NULL) {
		fprintf(stderr, "Error: Failed to create image_t\n");
		_TIFFfree(raster);
		TIFFClose(tif);
		return NULL;
	}

	// Convert ABGR to RGBA (libtiff returns ABGR packed in uint32_t)
	for (uint32_t i = 0; i < width * height; i++) {
		uint32_t abgr = raster[i];
		img->pixels[i * 4 + 0] = TIFFGetR(abgr);
		img->pixels[i * 4 + 1] = TIFFGetG(abgr);
		img->pixels[i * 4 + 2] = TIFFGetB(abgr);
		img->pixels[i * 4 + 3] = TIFFGetA(abgr);
	}

	_TIFFfree(raster);
	TIFFClose(tif);

	// Allocate frames array
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

/**
 * @brief Decode multi-page TIFF with all pages
 *
 * Decodes all pages in a multi-page TIFF file.
 * Each page is decoded independently.
 *
 * @param data Raw TIFF file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded
 * @return Array of image_t* frames, or NULL on error
 *
 * @note Maximum MAX_TIFF_FRAMES frames (200) to prevent DoS
 * @note TIFF pages have no timing info - uniform frame rate
 * @note Output format is RGBA8888
 */
static image_t **decode_tiff_multipage(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_tiff_multipage\n");
		return NULL;
	}

	*frame_count = 0;

	tiff_memory_t mem = { data, len, 0 };

	TIFF *tif = TIFFClientOpen("memory", "r", (thandle_t)&mem, tiff_read_proc, tiff_write_proc, tiff_seek_proc, tiff_close_proc, tiff_size_proc, NULL, NULL);
	if (tif == NULL) {
		fprintf(stderr, "Error: Failed to open TIFF\n");
		return NULL;
	}

	tdir_t num_dirs = TIFFNumberOfDirectories(tif);
	if (num_dirs == 0) {
		fprintf(stderr, "Error: TIFF has no pages\n");
		TIFFClose(tif);
		return NULL;
	}

	// Enforce MAX_TIFF_FRAMES limit
	int num_frames = (int)num_dirs;
	if (num_frames > MAX_TIFF_FRAMES) {
		fprintf(stderr, "Warning: TIFF has %d pages, limiting to %d\n", num_frames, MAX_TIFF_FRAMES);
		num_frames = MAX_TIFF_FRAMES;
	}

	// Allocate frames array
	image_t **frames = (image_t **)malloc(sizeof(image_t *) * num_frames);
	if (frames == NULL) {
		fprintf(stderr, "Error: Failed to allocate frames array\n");
		TIFFClose(tif);
		return NULL;
	}

	// Initialize to NULL for cleanup safety
	for (int i = 0; i < num_frames; i++) {
		frames[i] = NULL;
	}

	// Decode each directory
	for (int i = 0; i < num_frames; i++) {
		if (!TIFFSetDirectory(tif, (tdir_t)i)) {
			fprintf(stderr, "Error: Failed to set TIFF directory %d\n", i);
			goto cleanup_error;
		}

		uint32_t width, height;
		TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
		TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);

		uint32_t *raster = (uint32_t *)_TIFFmalloc(width * height * sizeof(uint32_t));
		if (raster == NULL) {
			fprintf(stderr, "Error: Failed to allocate raster for page %d\n", i);
			goto cleanup_error;
		}

		if (!TIFFReadRGBAImageOriented(tif, width, height, raster, ORIENTATION_TOPLEFT, 0)) {
			fprintf(stderr, "Error: Failed to read TIFF page %d\n", i);
			_TIFFfree(raster);
			goto cleanup_error;
		}

		frames[i] = image_create(width, height);
		if (frames[i] == NULL) {
			fprintf(stderr, "Error: Failed to create image_t for page %d\n", i);
			_TIFFfree(raster);
			goto cleanup_error;
		}

		// Convert ABGR to RGBA
		for (uint32_t j = 0; j < width * height; j++) {
			uint32_t abgr = raster[j];
			frames[i]->pixels[j * 4 + 0] = TIFFGetR(abgr);
			frames[i]->pixels[j * 4 + 1] = TIFFGetG(abgr);
			frames[i]->pixels[j * 4 + 2] = TIFFGetB(abgr);
			frames[i]->pixels[j * 4 + 3] = TIFFGetA(abgr);
		}

		_TIFFfree(raster);
	}

	TIFFClose(tif);
	*frame_count = num_frames;
	return frames;

cleanup_error:
	for (int i = 0; i < num_frames; i++) {
		if (frames[i] != NULL) {
			image_destroy(frames[i]);
		}
	}
	free(frames);
	TIFFClose(tif);
	return NULL;
}

/**
 * @brief Decode TIFF image (static or multi-page)
 *
 * Main entry point for TIFF decoding. Automatically detects if the image
 * is multi-page and routes to the appropriate decoder function.
 *
 * @param data Raw TIFF file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded (1 for static, N for multi-page)
 * @return Array of image_t* frames, or NULL on error
 *
 * @note Caller must free returned array with decoder_free_frames()
 * @note For static images, frame_count = 1
 * @note For multi-page images, frame_count = N (max MAX_TIFF_FRAMES)
 * @note Output format is RGBA8888
 */
image_t **decode_tiff(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_tiff\n");
		return NULL;
	}

	*frame_count = 0;

	if (tiff_is_multipage(data, len)) {
		return decode_tiff_multipage(data, len, frame_count);
	} else {
		return decode_tiff_static(data, len, frame_count);
	}
}
