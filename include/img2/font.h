/**
 * @file font.h
 * @brief libimg2 font cache: system fonts plus the embedded DejaVu Sans Mono family
 */

#ifndef IMG2_FONT_H
#define IMG2_FONT_H

#include <img2/common.h>

IMG2_BEGIN_DECLS

/**
 * @brief Font weight
 */
typedef enum img2_font_weight {
	IMG2_FONT_WEIGHT_NORMAL = 400,    /**< Regular */
	IMG2_FONT_WEIGHT_BOLD = 700       /**< Bold */
} img2_font_weight_t;

/**
 * @brief Font style
 */
typedef enum img2_font_style {
	IMG2_FONT_STYLE_NORMAL = 0,       /**< Upright */
	IMG2_FONT_STYLE_ITALIC = 1        /**< Italic / oblique */
} img2_font_style_t;

/**
 * @brief Opaque font handle
 *
 * Interned in the library's font cache: owned by the library, never freed by callers, no
 * release API; valid until the last img2_shutdown(). Repeated loads of the same face return
 * the same pointer.
 */
typedef struct img2_font img2_font_t;

/**
 * @brief Look up a font by family (system fonts first, the embedded family as fallback)
 *
 * @param family Family name (e.g. "DejaVu Sans"; "monospace", "sans-serif", "serif" are mapped)
 * @param weight Weight
 * @param style  Style
 * @param out    Output handle (see img2_font_t); *out = NULL on error
 * @return IMG2_OK; IMG2_E_ARG (family or out NULL); IMG2_E_NOT_FOUND; IMG2_E_NOMEM
 */
img2_status_t img2_font_load(const char *family, img2_font_weight_t weight, img2_font_style_t style, img2_font_t **out);

/**
 * @brief The embedded DejaVu Sans Mono (regular) font
 *
 * @param out Output handle (see img2_font_t); *out = NULL on error
 * @return IMG2_OK; IMG2_E_ARG (out NULL); IMG2_E_NOT_FOUND
 */
img2_status_t img2_font_load_fallback(img2_font_t **out);

/**
 * @brief Raw TTF/OTF data of a font
 *
 * @param font     Font handle
 * @param size_out Output: data size in bytes
 * @return Data (valid as long as the handle), or NULL if font or size_out is NULL
 */
const uint8_t *img2_font_get_data(const img2_font_t *font, size_t *size_out);

/**
 * @brief Family name of a font
 *
 * @param font Font handle
 * @return Family name (valid as long as the handle), or NULL if font is NULL
 */
const char *img2_font_get_family(const img2_font_t *font);

/**
 * @brief One entry of the font cache
 */
typedef struct img2_font_info {
	const char *family;                /**< Family name */
	const char *path;                  /**< File path; "[embedded]" for built-in fonts */
	img2_font_weight_t weight;         /**< Weight */
	img2_font_style_t style;           /**< Style */
	bool embedded;                     /**< true for built-in fonts */
} img2_font_info_t;

/**
 * @brief Visitor for img2_font_foreach()
 *
 * @param info Cache entry (valid only during the call; its strings until img2_shutdown())
 * @param user The user pointer passed to img2_font_foreach()
 * @return 0 to continue, nonzero to stop
 */
typedef int (*img2_font_visit_fn)(const img2_font_info_t *info, void *user);

/**
 * @brief Visit the font cache in cache order (initialises the cache lazily)
 *
 * The visitor runs without any library lock held. Strings stay valid until img2_shutdown().
 *
 * @param fn   Visitor
 * @param user Opaque pointer handed to every fn call
 * @return IMG2_OK; IMG2_E_ARG (fn NULL); IMG2_E_NOMEM
 */
img2_status_t img2_font_foreach(img2_font_visit_fn fn, void *user);

IMG2_END_DECLS

#endif /* IMG2_FONT_H */
