#ifndef EMBEDDED_FONTS_H
#define EMBEDDED_FONTS_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *data;
    size_t size;
} embedded_font_t;

extern const uint8_t embedded_dejavu_bold_data[];
extern const size_t embedded_dejavu_bold_size;
extern const embedded_font_t embedded_dejavu_bold;

extern const uint8_t embedded_dejavu_bold_italic_data[];
extern const size_t embedded_dejavu_bold_italic_size;
extern const embedded_font_t embedded_dejavu_bold_italic;

extern const uint8_t embedded_dejavu_italic_data[];
extern const size_t embedded_dejavu_italic_size;
extern const embedded_font_t embedded_dejavu_italic;

extern const uint8_t embedded_dejavu_regular_data[];
extern const size_t embedded_dejavu_regular_size;
extern const embedded_font_t embedded_dejavu_regular;

#endif /* EMBEDDED_FONTS_H */
