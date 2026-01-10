/**
 * @file terminal_unix.c
 * @brief Unix/POSIX terminal control implementation
 *
 * Implements terminal operations using POSIX APIs:
 * - ioctl(TIOCGWINSZ) for terminal size
 * - isatty() for TTY detection
 * - termios for echo control
 * - getenv() for color support detection
 */

#include <sys/ioctl.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "terminal.h"

/**
 * @brief Get terminal dimensions using ioctl(TIOCGWINSZ)
 */
int terminal_get_size(int *rows, int *cols)
{
	if (rows == NULL || cols == NULL) {
		return -1;
	}

	/* Initialize outputs to defaults */
	*rows = DEFAULT_TERM_ROWS;
	*cols = DEFAULT_TERM_COLS;

	/* Get terminal size with ioctl */
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
		/* ioctl failed, use defaults */
		fprintf(stderr, "Warning: Failed to get terminal size: %s (using defaults %dx%d)\n", strerror(errno), DEFAULT_TERM_COLS, DEFAULT_TERM_ROWS);
		return -1;
	}

	/* Validate dimensions */
	if (ws.ws_row <= 0 || ws.ws_col <= 0) {
		fprintf(stderr, "Warning: Invalid terminal size %ux%u (using defaults %dx%d)\n", ws.ws_col, ws.ws_row, DEFAULT_TERM_COLS, DEFAULT_TERM_ROWS);
		return -1;
	}

	/* Success */
	*rows = ws.ws_row;
	*cols = ws.ws_col;
	return 0;
}

/**
 * @brief Get terminal pixel dimensions using ioctl(TIOCGWINSZ)
 */
int terminal_get_pixels(int *width, int *height)
{
	/* Get terminal size with ioctl */
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
		/* ioctl failed, use defaults */
		fprintf(stderr, "Warning: Failed to get terminal size: %s (using defaults %dx%d)\n", strerror(errno), DEFAULT_TERM_COLS, DEFAULT_TERM_ROWS);
		return -1;
	}

	/* Validate dimensions */
	if (ws.ws_row <= 0 || ws.ws_col <= 0) {
		fprintf(stderr, "Warning: Invalid terminal size %ux%u (using defaults %dx%d)\n", ws.ws_col, ws.ws_row, DEFAULT_TERM_COLS, DEFAULT_TERM_ROWS);
		return -1;
	}

	/* Success */
	*width = ws.ws_xpixel;
	*height = ws.ws_ypixel;
	return 0;
}

/**
 * @brief Check if file descriptor is a TTY using isatty()
 */
bool terminal_is_tty(int fd)
{
	return isatty(fd) != 0;
}

/**
 * @brief Disable terminal echo using termios
 */
void *terminal_disable_echo(void)
{
	/* Allocate storage for original termios state */
	struct termios *orig_termios = malloc(sizeof(struct termios));
	if (orig_termios == NULL) {
		fprintf(stderr, "Error: Failed to allocate termios state: %s\n", strerror(errno));
		return NULL;
	}

	/* Get current terminal attributes */
	if (tcgetattr(STDOUT_FILENO, orig_termios) == -1) {
		fprintf(stderr, "Error: Failed to get terminal attributes: %s\n", strerror(errno));
		free(orig_termios);
		return NULL;
	}

	/* Create new termios with echo disabled */
	struct termios new_termios = *orig_termios;

	/* Disable echo */
	new_termios.c_lflag &= ~ECHO;

	/* Enable canonical mode and signal processing */
	new_termios.c_lflag |= (ICANON | ISIG);

	/* Enable CR to NL translation */
	new_termios.c_iflag |= ICRNL;

	/* Apply new settings */
	if (tcsetattr(STDOUT_FILENO, TCSANOW, &new_termios) == -1) {
		fprintf(stderr, "Error: Failed to set terminal attributes: %s\n", strerror(errno));
		free(orig_termios);
		return NULL;
	}

	/* Return original state for restoration */
	return orig_termios;
}

/**
 * @brief Enable terminal echo (restore original state)
 */
void terminal_enable_echo(void *state)
{
	if (state == NULL) {
		return;
	}

	struct termios *orig_termios = (struct termios *)state;

	/* Restore original terminal attributes */
	if (tcsetattr(STDOUT_FILENO, TCSANOW, orig_termios) == -1) {
		fprintf(stderr, "Error: Failed to restore terminal attributes: %s\n", strerror(errno));
	}

	/* Free the state */
	free(orig_termios);
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

/**
 * @brief Check if terminal is iTerm2
 */
bool terminal_is_iterm2(void)
{
	/* Primary detection: TERM_PROGRAM environment variable */
	const char *term_program = getenv("TERM_PROGRAM");
	if (term_program != NULL && strcmp(term_program, "iTerm.app") == 0) {
		return true;
	}

	/* Fallback detection: LC_TERMINAL environment variable */
	const char *lc_terminal = getenv("LC_TERMINAL");
	if (lc_terminal != NULL && strcmp(lc_terminal, "iTerm2") == 0) {
		return true;
	}

	/* Not iTerm2 */
	return false;
}

/**
 * @brief Check if terminal is Ghostty
 */
bool terminal_is_ghostty(void)
{
	/* Detection: TERM_PROGRAM environment variable */
	const char *term_program = getenv("TERM_PROGRAM");
	if (term_program != NULL && strcmp(term_program, "ghostty") == 0) {
		return true;
	}

	/* Not Ghostty */
	return false;
}

/**
 * @brief Check if terminal is Kitty
 */
bool terminal_is_kitty(void)
{
	/* Primary detection: TERM environment variable */
	const char *term = getenv("TERM");
	if (term != NULL && strcmp(term, "xterm-kitty") == 0) {
		return true;
	}

	/* Secondary detection: TERM_PROGRAM environment variable */
	const char *term_program = getenv("TERM_PROGRAM");
	if (term_program != NULL && strcmp(term_program, "kitty") == 0) {
		return true;
	}

	/* Not Kitty */
	return false;
}

/**
 * @brief Check if terminal is WezTerm
 */
bool terminal_is_wezterm(void)
{
	const char *term_program = getenv("TERM_PROGRAM");
	return (term_program != NULL && strcmp(term_program, "WezTerm") == 0);
}

/**
 * @brief Check if terminal is Konsole
 */
bool terminal_is_konsole(void)
{
	return (getenv("KONSOLE_VERSION") != NULL || getenv("KONSOLE_DBUS_SESSION") != NULL || getenv("KONSOLE_DBUS_SERVICE") != NULL);
}

/**
 * @brief Check if running inside tmux
 */
bool terminal_is_tmux(void)
{
	/* Check if TMUX environment variable is set */
	return getenv("TMUX") != NULL;
}
