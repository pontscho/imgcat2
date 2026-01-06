/**
 * @file ansi.h
 * @brief ANSI escape sequence constants and control functions
 *
 * Provides ANSI escape sequences for cursor control, color output,
 * and terminal attribute management. Uses half-block characters (▄)
 * for efficient 2:1 vertical resolution rendering.
 */

#ifndef IMGCAT2_ANSI_H
#define IMGCAT2_ANSI_H

#include <stddef.h>
#include <stdint.h>

/**
 * @defgroup ANSICursorControl ANSI Cursor Control Sequences
 * @{
 */

/** Hide cursor (DECTCEM) */
#define ANSI_CURSOR_HIDE "\x1B[?25l"

/** Show cursor (DECTCEM) */
#define ANSI_CURSOR_SHOW "\x1B[?25h"

/** Move cursor up N lines (format string, use with sprintf) */
#define ANSI_CURSOR_UP "\x1B[%dA"

/** @} */

/**
 * @defgroup ANSIColorSequences ANSI Color Escape Sequences
 * @{
 */

/** Reset all attributes (SGR 0) */
#define ANSI_RESET "\x1b[0m"

/** Set background color to RGB (format string: r, g, b) */
#define ANSI_BG_RGB "\x1b[48;2;%d;%d;%dm"

/** Set foreground color to RGB + output half-block (format string: r, g, b) */
#define ANSI_FG_RGB_HALFBLOCK "\x1b[38;2;%d;%d;%dm▄"

/** Reset background to transparent/default */
#define ANSI_BG_TRANSPARENT "\x1b[0;39;49m"

/** Reset foreground to transparent/default + output space */
#define ANSI_FG_TRANSPARENT "\x1b[0m "

/** @} */

/**
 * @defgroup ANSICharacters Special Characters
 * @{
 */

/** Half-block character (U+2584 Lower Half Block) */
#define HALF_BLOCK_CHAR "▄"

/** @} */

/**
 * @brief Hide terminal cursor
 *
 * Outputs ANSI escape sequence to hide the cursor (DECTCEM mode).
 * Used during rendering to prevent cursor artifacts.
 *
 * @note Call ansi_cursor_show() to restore cursor visibility
 */
void ansi_cursor_hide(void);

/**
 * @brief Show terminal cursor
 *
 * Outputs ANSI escape sequence to show the cursor (DECTCEM mode).
 * Restores cursor visibility after rendering.
 */
void ansi_cursor_show(void);

/**
 * @brief Move cursor up N lines
 *
 * Outputs ANSI escape sequence to move cursor up by specified lines.
 * Used for flicker-free animation by overwriting previous frame.
 *
 * @param lines Number of lines to move up (must be > 0)
 *
 * @note Does nothing if lines <= 0
 */
void ansi_cursor_up(int lines);

/**
 * @brief Reset all ANSI attributes
 *
 * Outputs ANSI escape sequence to reset all terminal attributes
 * (colors, styles, etc.) to defaults. Called at end of rendering.
 */
void ansi_reset(void);

#endif /* IMGCAT2_ANSI_H */
