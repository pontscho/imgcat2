/**
 * @file common.h
 * @brief libimg2 common definitions: status codes, library setup, memory release, version
 */

#ifndef IMG2_COMMON_H
#define IMG2_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <img2/img2_config.h>

#ifdef __cplusplus
/** Opens a block of C declarations (extern "C" in C++) */
#define IMG2_BEGIN_DECLS extern "C" {
/** Closes a block opened by IMG2_BEGIN_DECLS */
#define IMG2_END_DECLS }
#else
/** Opens a block of C declarations (extern "C" in C++) */
#define IMG2_BEGIN_DECLS
/** Closes a block opened by IMG2_BEGIN_DECLS */
#define IMG2_END_DECLS
#endif

IMG2_BEGIN_DECLS

/**
 * @brief Status code returned by fallible libimg2 functions
 *
 * Values are stable; new codes are only appended.
 */
typedef enum img2_status {
	IMG2_OK = 0,                 /**< Success */
	IMG2_E_NOMEM = -1,           /**< Out of memory */
	IMG2_E_ARG = -2,             /**< Invalid argument */
	IMG2_E_FORMAT = -3,          /**< Malformed or unrecognised data */
	IMG2_E_UNSUPPORTED = -4,     /**< Format or feature not built into this library */
	IMG2_E_LIMIT = -5,           /**< Resource limit exceeded (dimensions, pixels, file size) */
	IMG2_E_IO = -6,              /**< I/O error */
	IMG2_E_CODEC = -7,           /**< Error reported by a codec library */
	IMG2_E_INTERNAL = -8,        /**< Internal error */
	IMG2_E_NOT_FOUND = -9        /**< Requested item (e.g. a font) not found */
} img2_status_t;

/**
 * @brief Human-readable description of a status code
 *
 * @param status Status code
 * @return Static string, never NULL ("unknown status" for values outside the enum)
 */
const char *img2_strerror(img2_status_t status);

/**
 * @brief Free a buffer returned by the library (e.g. by img2_encode())
 *
 * @param ptr Buffer to free (NULL-safe)
 */
void img2_free(void *ptr);

/**
 * @brief Process-global library setup (reference counted; optional but recommended)
 *
 * No function requires it: the codec tables are const and the font cache initialises
 * lazily. Every img2_init() should be paired with an img2_shutdown().
 *
 * @return IMG2_OK
 */
img2_status_t img2_init(void);

/**
 * @brief Release one img2_init() reference
 *
 * The last call frees the font cache (font handles become invalid). It must not race
 * any other img2_* call. An unbalanced call (reference count already 0) is ignored.
 */
void img2_shutdown(void);

/**
 * @brief Version of the built library
 *
 * @return IMG2_VERSION_STRING of the library that is linked (e.g. "1.0.0")
 */
const char *img2_version_string(void);

IMG2_END_DECLS

#endif /* IMG2_COMMON_H */
