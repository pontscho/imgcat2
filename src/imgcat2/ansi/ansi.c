/**
 * @file ansi.c
 * @brief ANSI escape sequence control functions implementation
 *
 * Implements simple cursor control and attribute reset functions
 * that output ANSI escape sequences to stdout.
 */

#include <stdio.h>

#include "ansi.h"

/**
 * @brief Hide terminal cursor
 */
void ansi_cursor_hide(void)
{
	printf(ANSI_CURSOR_HIDE);
	fflush(stdout);
}

/**
 * @brief Show terminal cursor
 */
void ansi_cursor_show(void)
{
	printf(ANSI_CURSOR_SHOW);
	fflush(stdout);
}

/**
 * @brief Move cursor up N lines
 */
void ansi_cursor_up(int lines)
{
	if (lines <= 0) {
		return;
	}

	printf(ANSI_CURSOR_UP, lines);
	fflush(stdout);
}

/**
 * @brief Reset all ANSI attributes
 */
void ansi_reset(void)
{
	printf(ANSI_RESET);
	fflush(stdout);
}
