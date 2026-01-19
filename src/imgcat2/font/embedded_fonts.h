/**
 * @file embedded_fonts.h
 * @brief Declarations for embedded font resources.
 */

#ifndef IMGCAT2_EMBEDDED_FONTS_H
#define IMGCAT2_EMBEDDED_FONTS_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
	const uint8_t *data;
	size_t size;
} embedded_font_t;

extern const uint8_t embedded_dejavu_regular_data[];
extern const size_t embedded_dejavu_regular_size;
extern const embedded_font_t embedded_dejavu_regular;

#endif /* IMGCAT2_EMBEDDED_FONTS_H */
