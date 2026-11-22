/**
 * @file decoder_tiff.c
 * @brief TIFF decoder implementation using libtiff
 *
 * Decodes TIFF images (both single-page and multi-page) to RGBA8888 format.
 * Handles all TIFF format variations including CMYK, YCbCr, palette, etc.
 */

/* clang-format off */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tiffio.h>
/* clang-format on */

#include "decoder.h"
#include "../core/log.h"

#ifdef IMG2_HAVE_EXIF_READER
#include "../metadata/exif_reader.h"
#endif

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

#if defined(TIFFLIB_AT_LEAST)
#if TIFFLIB_AT_LEAST(4, 5, 0)
#define IMG2_TIFF_HANDLE_LOG 1
#endif
#endif

#ifdef IMG2_TIFF_HANDLE_LOG
/* [M1b] Reproduces unixErrorHandler/unixWarningHandler (vendor/libtiff/libtiff/tif_unix.c:367-385) byte for byte:
 * "<module>: " (if module) + [ "Warning, " ] + vformatted + "." ; the default sink adds "\n". */
static void tiff_log(img2_log_level_t lvl, const char *module, const char *prefix, const char *fmt, va_list ap)
	__attribute__((format(printf, 4, 0)));
static int log_tiff_error(TIFF *tif, void *user, const char *module, const char *fmt, va_list ap)
	__attribute__((format(printf, 4, 0)));
static int log_tiff_warning(TIFF *tif, void *user, const char *module, const char *fmt, va_list ap)
	__attribute__((format(printf, 4, 0)));

static void tiff_log(img2_log_level_t lvl, const char *module, const char *prefix, const char *fmt, va_list ap)
{
	char buf[512];
	char *heap = NULL;
	const char *text = buf;

	va_list ap2;
	va_copy(ap2, ap);
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	if (n < 0) {
		buf[0] = '\0';
	} else if ((size_t)n >= sizeof(buf)) {
		heap = malloc((size_t)n + 1);
		if (heap != NULL) {
			vsnprintf(heap, (size_t)n + 1, fmt, ap2);
			text = heap;
		}
		/* on malloc failure the truncated buf is delivered */
	}
	va_end(ap2);

	if (module != NULL) {
		img2_log(lvl, "%s: %s%s.", module, prefix, text);
	} else {
		img2_log(lvl, "%s%s.", prefix, text);
	}
	free(heap);
}

/* tif may be NULL (_TIFFErrorEarly during TIFFClientOpenExt): never dereferenced. Returning 1 stops libtiff
 * from also calling the process-global default handler. */
static int log_tiff_error(TIFF *tif, void *user, const char *module, const char *fmt, va_list ap)
{
	(void)tif;
	(void)user;
	tiff_log(IMG2_LOG_ERROR, module, "", fmt, ap);
	return 1;
}

static int log_tiff_warning(TIFF *tif, void *user, const char *module, const char *fmt, va_list ap)
{
	(void)tif;
	(void)user;
	tiff_log(IMG2_LOG_WARN, module, "Warning, ", fmt, ap);
	return 1;
}
#endif

/**
 * @brief Open an in-memory TIFF with per-handle error/warning handlers routed through img2_log
 */
static TIFF *tiff_open_memory(tiff_memory_t *mem)
{
#ifdef IMG2_TIFF_HANDLE_LOG
	TIFFOpenOptions *o = TIFFOpenOptionsAlloc();
	if (o == NULL) {
		return NULL;
	}
	TIFFOpenOptionsSetErrorHandlerExtR(o, log_tiff_error, NULL);
	TIFFOpenOptionsSetWarningHandlerExtR(o, log_tiff_warning, NULL);
	TIFF *tif = TIFFClientOpenExt("memory", "r", (thandle_t)mem, tiff_read_proc, tiff_write_proc, tiff_seek_proc,
	                              tiff_close_proc, tiff_size_proc, NULL, NULL, o);
	TIFFOpenOptionsFree(o);
	return tif;
#else
	return TIFFClientOpen("memory", "r", (thandle_t)mem, tiff_read_proc, tiff_write_proc, tiff_seek_proc,
	                      tiff_close_proc, tiff_size_proc, NULL, NULL);   /* host libtiff < 4.5: default handlers */
#endif
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

	TIFF *tif = tiff_open_memory(&mem);
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
 * @return Array with single img2_image_t*, or NULL on error
 *
 * @note Output format is RGBA8888
 * @note Handles all TIFF format variations (CMYK, YCbCr, palette, etc.)
 */
static img2_image_t **decode_tiff_static(const uint8_t *data, size_t len, int *frame_count, img2_status_t *st)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_tiff_static");
		return img2_decode_fail(st, IMG2_E_ARG);
	}

	*frame_count = 0;

	tiff_memory_t mem = { data, len, 0 };

	TIFF *tif = tiff_open_memory(&mem);
	if (tif == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to open TIFF");
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	uint32_t width = 0, height = 0;
	TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
	TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);

	// Reject oversized images before the raster is sized: width * height must not wrap (32 bits)
	size_t byte_count;
	if (!img2_image_calc_size(width, height, &byte_count)) {
		img2_log(IMG2_LOG_ERROR, "Error: TIFF dimensions %ux%u exceed the limits", width, height);
		TIFFClose(tif);
		return img2_decode_fail(st, IMG2_E_LIMIT);
	}

	// Allocate raster buffer (ABGR format)
	uint32_t *raster = (uint32_t *)_TIFFmalloc((tmsize_t)byte_count);
	if (raster == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate raster buffer");
		TIFFClose(tif);
		return img2_decode_fail(st, IMG2_E_NOMEM);
	}

	// Read RGBA image (libtiff handles all format conversions)
	if (!TIFFReadRGBAImageOriented(tif, width, height, raster, ORIENTATION_TOPLEFT, 0)) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to read TIFF image");
		_TIFFfree(raster);
		TIFFClose(tif);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Create img2_image_t
	img2_image_t *img = NULL;
	img2_status_t alloc_st = img2_decode_image_alloc(width, height, &img);
	if (alloc_st != IMG2_OK) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create image_t");
		_TIFFfree(raster);
		TIFFClose(tif);
		return img2_decode_fail(st, alloc_st);
	}

	// Convert ABGR to RGBA (libtiff returns ABGR packed in uint32_t)
	for (size_t i = 0; i < byte_count / 4; i++) {
		uint32_t abgr = raster[i];
		img->pixels[i * 4 + 0] = TIFFGetR(abgr);
		img->pixels[i * 4 + 1] = TIFFGetG(abgr);
		img->pixels[i * 4 + 2] = TIFFGetB(abgr);
		img->pixels[i * 4 + 3] = TIFFGetA(abgr);
	}

	_TIFFfree(raster);
	TIFFClose(tif);

	// Parse EXIF metadata - TIFF IS EXIF format (direct pass-through)
#ifdef IMG2_HAVE_EXIF_READER
	img2_exif_t *exif = malloc(sizeof(img2_exif_t));
	if (exif) {
		img2_exif_init(exif);

		// Pass entire TIFF file to parser (TIFF file = valid IFD structure)
		if (img2_exif_parse_tiff(exif, data, len) != 0) {
			// No EXIF data or parse failed
			free(exif);
			exif = NULL;
		}

		// Store metadata in image structure
		img->exif = exif;
	}
#endif

	// Allocate frames array
	img2_image_t **frames = (img2_image_t **)malloc(sizeof(img2_image_t *));
	if (frames == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate frames array");
		img2_image_destroy(img);
		return img2_decode_fail(st, IMG2_E_NOMEM);
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
 * @return Array of img2_image_t* frames, or NULL on error
 *
 * @note Maximum MAX_TIFF_FRAMES frames (200) to prevent DoS
 * @note TIFF pages have no timing info - uniform frame rate
 * @note Output format is RGBA8888
 */
static img2_image_t **decode_tiff_multipage(const uint8_t *data, size_t len, int *frame_count, img2_status_t *st)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_tiff_multipage");
		return img2_decode_fail(st, IMG2_E_ARG);
	}

	*frame_count = 0;

	tiff_memory_t mem = { data, len, 0 };

	TIFF *tif = tiff_open_memory(&mem);
	if (tif == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to open TIFF");
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	tdir_t num_dirs = TIFFNumberOfDirectories(tif);
	if (num_dirs == 0) {
		img2_log(IMG2_LOG_ERROR, "Error: TIFF has no pages");
		TIFFClose(tif);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Enforce MAX_TIFF_FRAMES limit
	int num_frames = (int)num_dirs;
	if (num_frames > MAX_TIFF_FRAMES) {
		img2_log(IMG2_LOG_WARN, "Warning: TIFF has %d pages, limiting to %d", num_frames, MAX_TIFF_FRAMES);
		num_frames = MAX_TIFF_FRAMES;
	}

	// Allocate frames array
	img2_image_t **frames = (img2_image_t **)malloc(sizeof(img2_image_t *) * num_frames);
	if (frames == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate frames array");
		TIFFClose(tif);
		return img2_decode_fail(st, IMG2_E_NOMEM);
	}

	// Initialize to NULL for cleanup safety
	for (int i = 0; i < num_frames; i++) {
		frames[i] = NULL;
	}

	// Decode each directory
	for (int i = 0; i < num_frames; i++) {
		if (!TIFFSetDirectory(tif, (tdir_t)i)) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to set TIFF directory %d", i);
			*st = IMG2_E_CODEC;
			goto cleanup_error;
		}

		uint32_t width = 0, height = 0;
		TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
		TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);

		size_t byte_count;
		if (!img2_image_calc_size(width, height, &byte_count)) {
			img2_log(IMG2_LOG_ERROR, "Error: TIFF page %d dimensions %ux%u exceed the limits", i, width, height);
			*st = IMG2_E_LIMIT;
			goto cleanup_error;
		}

		uint32_t *raster = (uint32_t *)_TIFFmalloc((tmsize_t)byte_count);
		if (raster == NULL) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate raster for page %d", i);
			*st = IMG2_E_NOMEM;
			goto cleanup_error;
		}

		if (!TIFFReadRGBAImageOriented(tif, width, height, raster, ORIENTATION_TOPLEFT, 0)) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to read TIFF page %d", i);
			_TIFFfree(raster);
			*st = IMG2_E_CODEC;
			goto cleanup_error;
		}

		frames[i] = img2_image_alloc(width, height);
		if (frames[i] == NULL) {
			img2_log(IMG2_LOG_ERROR, "Error: Failed to create image_t for page %d", i);
			_TIFFfree(raster);
			*st = IMG2_E_NOMEM;
			goto cleanup_error;
		}

		// Convert ABGR to RGBA
		for (size_t j = 0; j < byte_count / 4; j++) {
			uint32_t abgr = raster[j];
			frames[i]->pixels[j * 4 + 0] = TIFFGetR(abgr);
			frames[i]->pixels[j * 4 + 1] = TIFFGetG(abgr);
			frames[i]->pixels[j * 4 + 2] = TIFFGetB(abgr);
			frames[i]->pixels[j * 4 + 3] = TIFFGetA(abgr);
		}

		_TIFFfree(raster);
	}

	TIFFClose(tif);

	// Parse EXIF metadata from first frame - TIFF IS EXIF format (direct pass-through)
#ifdef IMG2_HAVE_EXIF_READER
	img2_exif_t *exif = malloc(sizeof(img2_exif_t));
	if (exif) {
		img2_exif_init(exif);

		// Pass entire TIFF file to parser (TIFF file = valid IFD structure)
		if (img2_exif_parse_tiff(exif, data, len) != 0) {
			// No EXIF data or parse failed
			free(exif);
			exif = NULL;
		}

		// Store metadata in first frame's image structure
		if (exif && frames[0]) {
			frames[0]->exif = exif;
		}
	}
#endif

	*frame_count = num_frames;
	return frames;

cleanup_error:
	for (int i = 0; i < num_frames; i++) {
		if (frames[i] != NULL) {
			img2_image_destroy(frames[i]);
		}
	}
	free(frames);
	TIFFClose(tif);
	return NULL; /* *st set at the goto */
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
 * @return Array of img2_image_t* frames, or NULL on error
 *
 * @note Caller must free returned array with img2_decoder_free_frames()
 * @note For static images, frame_count = 1
 * @note For multi-page images, frame_count = N (max MAX_TIFF_FRAMES)
 * @note Output format is RGBA8888
 */
static img2_image_t **decode_tiff_frames(const uint8_t *data, size_t len, int *frame_count, img2_status_t *st)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_tiff");
		return img2_decode_fail(st, IMG2_E_ARG);
	}

	*frame_count = 0;

	if (tiff_is_multipage(data, len)) {
		return decode_tiff_multipage(data, len, frame_count, st);
	} else {
		return decode_tiff_static(data, len, frame_count, st);
	}
}

img2_status_t img2_decode_tiff(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out)
{
	(void)opts;
	if (out == NULL) {
		return IMG2_E_ARG;
	}
	img2_status_t st = IMG2_E_INTERNAL;
	int count = 0;
	img2_image_t **frames = decode_tiff_frames(data, len, &count, &st);
	return img2_frames_adopt(out, frames, count, st);
}
