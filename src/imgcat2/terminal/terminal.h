/**
 * @file terminal.h
 * @brief Platform-agnostic terminal control API
 *
 * Provides a unified interface for terminal operations across Unix/POSIX
 * and Windows platforms, including terminal size detection, TTY checks,
 * echo control, and true color detection.
 */

#ifndef IMGCAT2_TERMINAL_H
#define IMGCAT2_TERMINAL_H

#include <stdbool.h>
#include <stdint.h>

#if defined(__linux__) || defined(__APPLE__)
#include "kitty.h"
#include "iterm2.h"
#endif

/**
 * @defgroup TerminalDefaults Default Terminal Dimensions
 * @{
 */

/** Default terminal rows (fallback) */
#define DEFAULT_TERM_ROWS 24

/** Default terminal columns (fallback) */
#define DEFAULT_TERM_COLS 80

/** @} */

/**
 * @brief Get terminal dimensions
 *
 * Retrieves the current terminal size (rows and columns).
 * Platform-specific implementation:
 * - Unix: ioctl(TIOCGWINSZ)
 * - Windows: GetConsoleScreenBufferInfo()
 *
 * @param rows Output parameter for terminal rows
 * @param cols Output parameter for terminal columns
 *
 * @return 0 on success, -1 on failure (sets defaults)
 *
 * @note On failure, *rows and *cols are set to DEFAULT_TERM_ROWS/COLS
 */
int terminal_get_size(int *rows, int *cols);

/**
 * @brief Get terminal pixel dimensions
 *
 * Retrieves the current terminal size in pixels (width and height).
 * Platform-specific implementation:
 * - Unix: ioctl(TIOCGWINSZ)
 * - Windows: GetConsoleScreenBufferInfo() + font size
 *
 * @param width Output parameter for terminal width in pixels
 * @param height Output parameter for terminal height in pixels
 *
 * @return 0 on success, -1 on failure
 */
int terminal_get_pixels(int *width, int *height);

/**
 * @brief Check if file descriptor is a TTY
 *
 * Determines if the given file descriptor refers to a terminal device.
 * Used to detect if output is redirected to a file or pipe.
 * Platform-specific implementation:
 * - Unix: isatty()
 * - Windows: GetFileType() == FILE_TYPE_CHAR
 *
 * @param fd File descriptor to check (e.g., STDOUT_FILENO)
 *
 * @return true if fd is a TTY, false otherwise
 */
bool terminal_is_tty(int fd);

/**
 * @brief Disable terminal echo
 *
 * Disables character echo on the terminal (input characters not displayed).
 * Returns the original terminal state for restoration.
 * Platform-specific implementation:
 * - Unix: termios (clear ECHO flag)
 * - Windows: SetConsoleMode (clear ENABLE_ECHO_INPUT)
 *
 * @return Opaque pointer to original terminal state (pass to terminal_enable_echo)
 *
 * @note Caller must call terminal_enable_echo() to restore state
 * @note Returns NULL on failure
 */
void *terminal_disable_echo(void);

/**
 * @brief Enable terminal echo (restore original state)
 *
 * Restores the terminal state saved by terminal_disable_echo().
 * Re-enables character echo if it was enabled before.
 *
 * @param state Opaque pointer returned by terminal_disable_echo()
 *
 * @note NULL-safe (does nothing if state is NULL)
 * @note Frees the state pointer after restoration
 */
void terminal_enable_echo(void *state);

/**
 * @brief Check if terminal supports 24-bit true color
 *
 * Detects if the terminal supports 24-bit RGB color (16.7 million colors)
 * by checking environment variables:
 * - COLORTERM == "truecolor" or "24bit"
 * - TERM contains "256color" (likely supports true color)
 *
 * @return true if true color supported, false otherwise
 *
 * @note Cross-platform (same implementation Unix/Windows)
 */
bool terminal_supports_truecolor(void);

/**
 * @brief Check if terminal is iTerm2
 *
 * Detects if the current terminal is iTerm2 by checking environment variables.
 * iTerm2 sets TERM_PROGRAM="iTerm.app" and optionally LC_TERMINAL="iTerm2".
 *
 * @return true if running in iTerm2, false otherwise
 *
 * @note macOS/Unix only - returns false on Windows
 * @note Used to enable iTerm2 inline images protocol (OSC 1337)
 */
bool terminal_is_iterm2(void);

/**
 * @brief Check if terminal is Ghostty
 *
 * Detects if the current terminal is Ghostty by checking environment variables.
 * Ghostty sets TERM_PROGRAM="ghostty".
 *
 * @return true if running in Ghostty, false otherwise
 *
 * @note Used to enable Kitty graphics protocol
 */
bool terminal_is_ghostty(void);

/**
 * @brief Check if terminal is Kitty
 *
 * Detects if the current terminal is Kitty by checking environment variables.
 * Kitty sets TERM="xterm-kitty" and optionally TERM_PROGRAM="kitty".
 *
 * @return true if running in Kitty, false otherwise
 *
 * @note Used to enable Kitty graphics protocol
 */
bool terminal_is_kitty(void);

bool terminal_is_wezterm(void);
bool terminal_is_konsole(void);

/**
 * @brief Check if running inside tmux
 *
 * Detects if the current terminal session is running inside tmux
 * by checking the TMUX environment variable.
 *
 * @return true if inside tmux, false otherwise
 */
bool terminal_is_tmux(void);

#endif /* IMGCAT2_TERMINAL_H */
