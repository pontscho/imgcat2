/**
 * @file magic.h
 * @brief Magic bytes detection and MIME type identification
 *
 * img2_mime_t, img2_detect_mime(), img2_mime_name() and img2_mime_string() are public
 * (<img2/mime.h>). Detection priority order (magic.c):
 * 1. PNG (8 byte signature)
 * 2. JPEG (3 byte signature)
 * 3. GIF (6 byte signature, GIF87a or GIF89a)
 * 4. BMP (2 byte signature "BM")
 * 5. PNM/PAM ("P1".."P7" followed by a whitespace byte; checked before
 *    TGA because the TGA heuristic can match Netpbm headers)
 * 6. TGA (header-based: byte[2] and byte[16])
 * 7. PSD (4 byte signature "8BPS")
 * 8. HDR (10 or 6 byte signature)
 */

#ifndef IMG2_INTERNAL_DECODERS_MAGIC_H
#define IMG2_INTERNAL_DECODERS_MAGIC_H

#include <stddef.h>
#include <stdint.h>

#include <img2/mime.h>

#endif /* IMG2_INTERNAL_DECODERS_MAGIC_H */
