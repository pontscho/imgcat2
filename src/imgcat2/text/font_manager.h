/**
 * @file font_manager.h
 * @brief Cross-platform font discovery and management
 *
 * Provides system font discovery with fallback to embedded fonts.
 * Supports Linux, macOS, and Windows font paths.
 */

#ifndef IMGCAT2_FONT_MANAGER_H
#define IMGCAT2_FONT_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Font weight enumeration
 */
typedef enum {
	FONT_WEIGHT_NORMAL = 400,
	FONT_WEIGHT_BOLD = 700
} font_weight_t;

/**
 * @brief Font style enumeration
 */
typedef enum {
	FONT_STYLE_NORMAL = 0,
	FONT_STYLE_ITALIC = 1
} font_style_t;

/**
 * @brief Opaque font handle
 */
typedef struct font_t font_t;

/**
 * @brief Initialize font manager
 *
 * Scans system font directories and builds font cache.
 * Must be called before any other font_* functions.
 *
 * @return true on success, false on failure
 */
bool font_manager_init(void);

/**
 * @brief Cleanup font manager
 *
 * Frees all cached fonts and resources.
 * Should be called at program exit.
 */
void font_manager_cleanup(void);

/**
 * @brief Load a font by family name
 *
 * Searches system fonts first, falls back to embedded font if not found.
 * The returned font is cached and managed by font_manager.
 *
 * @param family Font family name (e.g., "Arial", "DejaVu Sans")
 * @param weight Font weight (FONT_WEIGHT_NORMAL or FONT_WEIGHT_BOLD)
 * @param style Font style (FONT_STYLE_NORMAL or FONT_STYLE_ITALIC)
 * @return Font handle or NULL on failure
 *
 * @note The returned pointer is owned by font_manager, do not free
 */
font_t *font_manager_load(const char *family, font_weight_t weight, font_style_t style);

/**
 * @brief Load the embedded fallback font
 *
 * Returns a handle to the built-in monospace font.
 * Always succeeds unless font_manager is not initialized.
 *
 * @return Font handle or NULL if not initialized
 */
font_t *font_manager_load_fallback(void);

/**
 * @brief Get TTF font data from a font handle
 *
 * Returns pointer to the raw TTF data for use with stb_truetype.
 * The data remains valid until font_manager_cleanup() is called.
 *
 * @param font Font handle
 * @param size_out Output: size of font data in bytes
 * @return Pointer to TTF data or NULL on error
 */
const uint8_t *font_get_data(const font_t *font, size_t *size_out);

/**
 * @brief Get font family name
 *
 * @param font Font handle
 * @return Font family name or NULL on error
 */
const char *font_get_family(const font_t *font);

#ifdef __cplusplus
}
#endif

#endif /* IMGCAT2_FONT_MANAGER_H */
