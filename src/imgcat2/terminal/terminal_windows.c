/**
 * @file terminal_windows.c
 * @brief Windows terminal control implementation
 *
 * Implements terminal operations using Windows Console API:
 * - GetConsoleScreenBufferInfo() for terminal size
 * - GetFileType() for TTY detection
 * - SetConsoleMode() for echo control and ANSI escape sequences
 * - getenv() for color support detection
 */

#ifdef _WIN32

#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "terminal.h"

/**
 * @brief Get terminal dimensions using GetConsoleScreenBufferInfo
 */
int terminal_get_size(int *rows, int *cols)
{
	if (rows == NULL || cols == NULL) {
		return -1;
	}

	/* Initialize outputs to defaults */
	*rows = DEFAULT_TERM_ROWS;
	*cols = DEFAULT_TERM_COLS;

	/* Get console handle */
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hConsole == INVALID_HANDLE_VALUE) {
		fprintf(stderr, "Warning: Failed to get console handle (using defaults %dx%d)\n", DEFAULT_TERM_COLS, DEFAULT_TERM_ROWS);
		return -1;
	}

	/* Get console screen buffer info */
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) {
		fprintf(stderr, "Warning: Failed to get console info (using defaults %dx%d)\n", DEFAULT_TERM_COLS, DEFAULT_TERM_ROWS);
		return -1;
	}

	/* Calculate dimensions from window rect */
	int width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
	int height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

	/* Validate dimensions */
	if (width <= 0 || height <= 0) {
		fprintf(stderr, "Warning: Invalid console size %dx%d (using defaults %dx%d)\n", width, height, DEFAULT_TERM_COLS, DEFAULT_TERM_ROWS);
		return -1;
	}

	/* Success */
	*cols = width;
	*rows = height;
	return 0;
}

/**
 * @brief Check if file descriptor is a TTY using GetFileType
 */
bool terminal_is_tty(int fd)
{
	/* Convert fd to Windows HANDLE */
	HANDLE h = (HANDLE)_get_osfhandle(fd);
	if (h == INVALID_HANDLE_VALUE) {
		return false;
	}

	/* Check if file type is character device (console) */
	DWORD type = GetFileType(h);
	return (type == FILE_TYPE_CHAR);
}

/**
 * @brief Disable terminal echo using SetConsoleMode
 */
void *terminal_disable_echo(void)
{
	/* Get console handle */
	HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hStdout == INVALID_HANDLE_VALUE) {
		fprintf(stderr, "Error: Failed to get console handle\n");
		return NULL;
	}

	/* Get current console mode */
	DWORD orig_mode;
	if (!GetConsoleMode(hStdout, &orig_mode)) {
		fprintf(stderr, "Error: Failed to get console mode\n");
		return NULL;
	}

	/* Create new mode with echo disabled */
	DWORD new_mode = orig_mode;

	/* Disable echo input */
	new_mode &= ~ENABLE_ECHO_INPUT;

	/* Enable processed input and line input */
	new_mode |= ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT;

	/* CRITICAL: Enable ANSI escape sequence processing */
	new_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;

	/* Apply new console mode */
	if (!SetConsoleMode(hStdout, new_mode)) {
		fprintf(stderr, "Error: Failed to set console mode\n");
		return NULL;
	}

	/* Return original mode (cast to void*) */
	return (void *)(uintptr_t)orig_mode;
}

/**
 * @brief Enable terminal echo (restore original state)
 */
void terminal_enable_echo(void *state)
{
	if (state == NULL) {
		return;
	}

	/* Get console handle */
	HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hStdout == INVALID_HANDLE_VALUE) {
		fprintf(stderr, "Error: Failed to get console handle\n");
		return;
	}

	/* Restore original console mode */
	DWORD orig_mode = (DWORD)(uintptr_t)state;
	if (!SetConsoleMode(hStdout, orig_mode)) {
		fprintf(stderr, "Error: Failed to restore console mode\n");
	}
}

/**
 * @brief Check if terminal supports 24-bit true color
 */
bool terminal_supports_truecolor(void)
{
	/* Check COLORTERM environment variable */
	const char *colorterm = getenv("COLORTERM");
	if (colorterm != NULL) {
		if (strcmp(colorterm, "truecolor") == 0 || strcmp(colorterm, "24bit") == 0) {
			return true;
		}
	}

	/* Check TERM environment variable */
	const char *term = getenv("TERM");
	if (term != NULL) {
		/* If TERM contains "256color", assume true color support */
		if (strstr(term, "256color") != NULL) {
			return true;
		}
	}

	/* No true color support detected */
	return false;
}

bool terminal_is_iterm2(void)
{
	/* iTerm2 is not available on Windows */
	return false;
}

bool terminal_is_ghostty(void)
{
	/* Ghostty is not available on Windows */
	return false;
}

#endif /* _WIN32 */
