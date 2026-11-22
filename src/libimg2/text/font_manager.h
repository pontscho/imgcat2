/**
 * @file font_manager.h
 * @brief Cross-platform font discovery and management
 *
 * Provides system font discovery with fallback to embedded fonts.
 * Supports Linux, macOS, and Windows font paths.
 */

#ifndef IMG2_INTERNAL_TEXT_FONT_MANAGER_H
#define IMG2_INTERNAL_TEXT_FONT_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <img2/font.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Public (<img2/font.h>): weight/style enums, img2_font_t, img2_font_load, img2_font_load_fallback,
 * img2_font_get_data, img2_font_get_family, img2_font_info_t, img2_font_foreach. Internal below. */

/* Every entry point initialises the font cache lazily (scan of the system font directories + the
 * embedded fallbacks, under s_font_lock) through img2_font_cache_init_lazy(); the final img2_shutdown()
 * frees it. A lazy build while no img2_init() reference is held takes an implicit reference that is
 * never released, so the cache then lives until process exit. */

/**
 * @brief Internal: cleanup for the final img2_shutdown()
 *
 * Runs with s_init_lock held by the caller; takes s_font_lock and frees the
 * cache only if *refs is still 0 (lock order s_init_lock -> s_font_lock).
 */
void img2_font_manager_cleanup_final(const int *refs);

/**
 * @brief Internal: build the font cache if it is not built yet
 *
 * Runs with s_init_lock held by the caller (img2_font_cache_init_lazy); takes s_font_lock.
 *
 * @return true if this call built the cache, false if it was already built
 */
bool img2_font_manager_init_lazy(void);

/**
 * @brief Internal (core/init.c): lazy font-cache build from a font entry point
 *
 * Takes s_init_lock, builds the cache via img2_font_manager_init_lazy() and, if it was built
 * while no img2_init() reference is held, takes the implicit, never released reference.
 * Must be called without s_font_lock held.
 */
void img2_font_cache_init_lazy(void);

/**
 * @brief Load a font by family name
 *
 * Searches system fonts first, falls back to embedded font if not found.
 * [R4] Handles are interned in the cache: the library owns them, they stay
 * valid until the last img2_shutdown() (until process exit if the cache was
 * built lazily without an img2_init() reference); there is no release call.
 *
 * @param family Font family name (e.g., "Arial", "DejaVu Sans")
 * @param weight Font weight (IMG2_FONT_WEIGHT_NORMAL or IMG2_FONT_WEIGHT_BOLD)
 * @param style Font style (IMG2_FONT_STYLE_NORMAL or IMG2_FONT_STYLE_ITALIC)
 * @return Font handle or NULL on failure
 *
 * @note The returned pointer is owned by font_manager, do not free
 */
img2_font_t *img2_font_find(const char *family, img2_font_weight_t weight, img2_font_style_t style);

/**
 * @brief The embedded DejaVu Sans Mono fallback handle (interned; NULL on OOM)
 */
img2_font_t *img2_font_find_fallback(void);

/**
 * @brief Opaque parsed font face (wraps stb_truetype inside font_manager.c)
 */
typedef struct img2_font_face img2_font_face_t;

/**
 * @brief One glyph outline vertex (field-for-field copy of the stb_truetype vertex)
 */
typedef struct img2_font_vertex {
	short x, y, cx, cy, cx1, cy1;
	unsigned char type;
} img2_font_vertex_t;

/** @brief Glyph vertex types (values equal STBTT_vmove/vline/vcurve/vcubic) */
enum { IMG2_FONT_VMOVE = 1, IMG2_FONT_VLINE = 2, IMG2_FONT_VCURVE = 3, IMG2_FONT_VCUBIC = 4 };

/**
 * @brief Parse a font for glyph access
 *
 * @param font Font handle
 * @return Face handle, or NULL if the font cannot be parsed or on OOM
 */
img2_font_face_t *img2_font_face_open(const img2_font_t *font);

/**
 * @brief Free a face returned by img2_font_face_open() (NULL-safe)
 */
void img2_font_face_close(img2_font_face_t *face);

/**
 * @brief Scale factor that maps the font's ascent-descent to @p pixels
 */
float img2_font_face_scale_for_pixel_height(const img2_font_face_t *face, float pixels);

/**
 * @brief Vertical metrics in unscaled font units
 */
void img2_font_face_vmetrics(const img2_font_face_t *face, int *ascent, int *descent, int *line_gap);

/**
 * @brief Horizontal metrics of a codepoint in unscaled font units
 */
void img2_font_face_hmetrics(const img2_font_face_t *face, int codepoint, int *advance, int *lsb);

/**
 * @brief Kerning advance between two codepoints in unscaled font units
 */
int img2_font_face_kern(const img2_font_face_t *face, int cp1, int cp2);

/**
 * @brief Glyph outline of a codepoint
 *
 * @param face Face handle
 * @param codepoint Unicode codepoint
 * @param out Output: malloc'd vertex array (free with img2_font_face_free_shape()), NULL if none
 * @return Number of vertices (0 if none or on error)
 */
int img2_font_face_glyph_shape(const img2_font_face_t *face, int codepoint, img2_font_vertex_t **out);

/**
 * @brief Free a vertex array returned by img2_font_face_glyph_shape() (NULL-safe)
 */
void img2_font_face_free_shape(img2_font_vertex_t *v);

#ifdef __cplusplus
}
#endif

#endif /* IMG2_INTERNAL_TEXT_FONT_MANAGER_H */
