/**
 * @file safe_text.h
 * @brief [P38/P39] Untrusted text (EXIF / XMP strings, file paths) written to a terminal without control characters
 *
 * C0 controls (0x00-0x1F, including tab and newline), DEL (0x7F) and C1 controls are written as "\xNN" (each byte),
 * so no escape sequence of a file's metadata or name reaches the terminal. C1 means U+0080-U+009F in UTF-8
 * (C2 80 - C2 9F) and the raw bytes 0x80-0x9F outside a valid UTF-8 sequence. Valid UTF-8 and every other byte
 * (0xA0-0xFF outside a valid sequence, e.g. Latin-1 text) are written unchanged.
 *
 * JSON output escapes on its own (metadata.c print_json_string); the library log sanitizes its messages.
 */

#ifndef IMGCAT2_SAFE_TEXT_H
#define IMGCAT2_SAFE_TEXT_H

#include <stddef.h>
#include <stdio.h>

/** Buffer size for cli_safe_str() of a path: every byte of a PATH_MAX path escaped ("\xNN") plus the NUL */
#define CLI_SAFE_PATH_SIZE (4 * 4096 + 1)

/**
 * @brief Write s to f with control characters escaped (see the file comment)
 *
 * @param s NUL-terminated text (NULL: nothing is written)
 * @param f Output stream
 */
void cli_fputs_safe(const char *s, FILE *f);

/**
 * @brief The escaped form of s in buf, for a "%s" of printf (see the file comment)
 *
 * Cut at the last whole character / escape that fits when buf is too small (CLI_SAFE_PATH_SIZE holds any path).
 *
 * @param s NUL-terminated text (NULL: "")
 * @param buf Output buffer
 * @param size Size of buf (> 0)
 * @return buf
 */
const char *cli_safe_str(const char *s, char *buf, size_t size);

#endif /* IMGCAT2_SAFE_TEXT_H */
