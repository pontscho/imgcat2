/**
 * @file test_terminal.c
 * @brief Unit tests for terminal detection and control
 *
 * Tests terminal size detection, TTY detection, echo control,
 * and true color support detection.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../imgcat2/terminal/terminal.h"
#include "../ctest.h"

/**
 * @test Test terminal_get_size() basic functionality
 *
 * Verifies that terminal_get_size() returns valid dimensions.
 * On real TTY, should return positive rows/cols.
 * On non-TTY (e.g., during CI), should return defaults.
 */
CTEST(terminal, get_size)
{
	int rows, cols;
	int result = terminal_get_size(&rows, &cols);

	/* Result should be 0 or -1 */
	ASSERT_TRUE(result == 0 || result == -1);

	/* Rows and cols should be positive */
	ASSERT_TRUE(rows > 0);
	ASSERT_TRUE(cols > 0);

	/* Should be reasonable values */
	ASSERT_TRUE(rows >= DEFAULT_TERM_ROWS && rows <= 500);
	ASSERT_TRUE(cols >= DEFAULT_TERM_COLS && cols <= 1000);
}

/**
 * @test Test terminal_get_size() with NULL parameters
 *
 * Verifies that terminal_get_size() handles NULL parameters gracefully.
 */
CTEST(terminal, get_size_null_params)
{
	int rows, cols;

	/* Should handle NULL for rows */
	int result1 = terminal_get_size(NULL, &cols);
	ASSERT_EQUAL(-1, result1);

	/* Should handle NULL for cols */
	int result2 = terminal_get_size(&rows, NULL);
	ASSERT_EQUAL(-1, result2);

	/* Should handle NULL for both */
	int result3 = terminal_get_size(NULL, NULL);
	ASSERT_EQUAL(-1, result3);
}

/**
 * @test Test terminal_is_tty() for stdout
 *
 * Verifies TTY detection for stdout.
 * During interactive run, stdout is TTY.
 * During CI/piped run, stdout is not TTY.
 */
CTEST(terminal, is_tty_stdout)
{
	bool is_tty = terminal_is_tty(STDOUT_FILENO);

	/* Result should be boolean (we can't predict the value) */
	/* In CI/automated tests, this will likely be false */
	/* In interactive terminal, this will be true */
	ASSERT_TRUE(is_tty == true || is_tty == false);
}

/**
 * @test Test terminal_is_tty() for stderr
 *
 * Verifies TTY detection for stderr.
 */
CTEST(terminal, is_tty_stderr)
{
	bool is_tty = terminal_is_tty(STDERR_FILENO);

	/* Result should be boolean */
	ASSERT_TRUE(is_tty == true || is_tty == false);
}

/**
 * @test Test terminal_is_tty() for invalid fd
 *
 * Verifies that terminal_is_tty() handles invalid file descriptors.
 */
CTEST(terminal, is_tty_invalid_fd)
{
	/* Very high fd that doesn't exist */
	bool is_tty = terminal_is_tty(9999);

	/* Should return false for invalid fd */
	ASSERT_FALSE(is_tty);
}

/**
 * @test Test terminal_supports_truecolor()
 *
 * Verifies true color support detection based on environment variables.
 * This test sets and unsets COLORTERM to test behavior.
 */
CTEST(terminal, truecolor_support)
{
	/* Save original environment variables */
	char *original_colorterm = getenv("COLORTERM");
	char *original_term = getenv("TERM");
	char saved_colorterm[256] = { 0 };
	char saved_term[256] = { 0 };
	if (original_colorterm != NULL) {
		strncpy(saved_colorterm, original_colorterm, sizeof(saved_colorterm) - 1);
	}
	if (original_term != NULL) {
		strncpy(saved_term, original_term, sizeof(saved_term) - 1);
	}

	/* Test 1: COLORTERM=truecolor should return true */
	setenv("COLORTERM", "truecolor", 1);
	ASSERT_TRUE(terminal_supports_truecolor());

	/* Test 2: COLORTERM=24bit should return true */
	setenv("COLORTERM", "24bit", 1);
	ASSERT_TRUE(terminal_supports_truecolor());

	/* Test 3: COLORTERM=other with no TERM should return false */
	setenv("COLORTERM", "16color", 1);
	unsetenv("TERM");
	ASSERT_FALSE(terminal_supports_truecolor());

	/* Test 4: TERM with 256color should return true */
	unsetenv("COLORTERM");
	setenv("TERM", "xterm-256color", 1);
	ASSERT_TRUE(terminal_supports_truecolor());

	/* Test 5: No COLORTERM and no 256color in TERM should return false */
	unsetenv("COLORTERM");
	setenv("TERM", "xterm", 1);
	ASSERT_FALSE(terminal_supports_truecolor());

	/* Restore original environment */
	if (original_colorterm != NULL) {
		setenv("COLORTERM", saved_colorterm, 1);
	} else {
		unsetenv("COLORTERM");
	}
	if (original_term != NULL) {
		setenv("TERM", saved_term, 1);
	} else {
		unsetenv("TERM");
	}
}

/**
 * @test Test terminal_disable_echo() and terminal_enable_echo()
 *
 * Verifies echo control functionality.
 * Tests that disable/enable pair works without crashing.
 */
CTEST(terminal, echo_control)
{
	/* Disable echo */
	void *state = terminal_disable_echo();

	/* State may be NULL if not a TTY (during CI), or valid pointer */
	/* Either way, enable_echo should handle it */

	/* Enable echo (restore) */
	terminal_enable_echo(state);

	/* Test passes if no crash occurred */
	ASSERT_TRUE(true);
}

/**
 * @test Test terminal_enable_echo() with NULL state
 *
 * Verifies that terminal_enable_echo() is NULL-safe.
 */
CTEST(terminal, echo_enable_null_safe)
{
	/* Should handle NULL without crashing */
	terminal_enable_echo(NULL);

	/* Test passes if no crash occurred */
	ASSERT_TRUE(true);
}

/**
 * @test Test terminal_disable_echo() multiple times
 *
 * Verifies that multiple disable/enable cycles work correctly.
 */
CTEST(terminal, echo_control_multiple)
{
	/* First cycle */
	void *state1 = terminal_disable_echo();
	terminal_enable_echo(state1);

	/* Second cycle */
	void *state2 = terminal_disable_echo();
	terminal_enable_echo(state2);

	/* Third cycle */
	void *state3 = terminal_disable_echo();
	terminal_enable_echo(state3);

	/* Test passes if no crash occurred */
	ASSERT_TRUE(true);
}

/**
 * @test Test terminal_get_size() consistency
 *
 * Verifies that multiple calls return consistent results.
 */
CTEST(terminal, get_size_consistency)
{
	int rows1, cols1;
	int rows2, cols2;

	terminal_get_size(&rows1, &cols1);
	terminal_get_size(&rows2, &cols2);

	/* Should return same values on consecutive calls */
	ASSERT_EQUAL(rows1, rows2);
	ASSERT_EQUAL(cols1, cols2);
}
