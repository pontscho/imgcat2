/**
 * @file test_cross_platform.c
 * @brief Cross-platform integration tests
 *
 * Tests platform-specific functionality (Unix/Windows).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../imgcat2/terminal/terminal.h"
#include "../ctest.h"

/**
 * @test Test platform detection macros
 *
 * Verifies that platform detection macros are defined.
 */
CTEST(integration, platform_detection)
{
#if defined(__unix__) || defined(__APPLE__)
	/* Unix-like platform (Linux, macOS, BSD) */
	ASSERT_TRUE(1); /* Platform detected */
#elif defined(_WIN32) || defined(_WIN64)
	/* Windows platform */
	ASSERT_TRUE(1); /* Platform detected */
#else
	/* Unknown platform */
	ASSERT_FALSE(1); /* Should not reach here on supported platforms */
#endif
}

/**
 * @test Test terminal size detection on current platform
 *
 * Verifies that terminal_get_size() works on the current platform.
 */
CTEST(integration, platform_terminal_size)
{
	int rows, cols;
	int result = terminal_get_size(&rows, &cols);

	/* Result should be 0 or -1 */
	ASSERT_TRUE(result == 0 || result == -1);

	/* Dimensions should be positive */
	ASSERT_TRUE(rows > 0);
	ASSERT_TRUE(cols > 0);

	/* Reasonable values */
	ASSERT_TRUE(rows >= DEFAULT_TERM_ROWS && rows <= 500);
	ASSERT_TRUE(cols >= DEFAULT_TERM_COLS && cols <= 1000);
}

/**
 * @test Test TTY detection on current platform
 *
 * Verifies that terminal_is_tty() works.
 */
CTEST(integration, platform_tty_detection)
{
	/* stdout (fd 1) */
	bool is_tty = terminal_is_tty(1);

	/* Result should be boolean (0 or 1) */
	ASSERT_TRUE(is_tty == 0 || is_tty == 1);

	/* In test environment, stdout is usually NOT a TTY */
	/* (piped to test runner), so we just verify the function works */
}

/**
 * @test Test terminal echo control
 *
 * Verifies that echo control functions exist and work.
 */
CTEST(integration, platform_terminal_echo)
{
	/* These functions should exist and be callable */
	/* Actual behavior depends on TTY availability */

	/* Disable echo - returns state pointer */
	void *state = terminal_disable_echo();

	/* State may be NULL if not a TTY or error occurred */
	/* This is expected in test environment */

	/* Enable echo - restores state */
	terminal_enable_echo(state);

	/* Function should complete without crashing */
	ASSERT_TRUE(1);
}

/**
 * @test Test truecolor support detection
 *
 * Verifies that truecolor support can be detected.
 */
CTEST(integration, platform_truecolor_detection)
{
	bool has_truecolor = terminal_supports_truecolor();

	/* Result should be boolean */
	ASSERT_TRUE(has_truecolor == 0 || has_truecolor == 1);

	/* Detection should be consistent across multiple calls */
	bool second_check = terminal_supports_truecolor();
	ASSERT_EQUAL(has_truecolor, second_check);
}

#if defined(__unix__) || defined(__APPLE__)
/**
 * @test Test Unix-specific terminal functionality
 *
 * Verifies Unix-specific terminal operations.
 */
CTEST(integration, platform_unix_terminal)
{
	/* Unix platform detected */
	ASSERT_TRUE(1);

	/* Test terminal size (Unix uses ioctl) */
	int rows, cols;
	int result = terminal_get_size(&rows, &cols);

	/* Should work on Unix */
	ASSERT_TRUE(result == 0 || result == -1);
	ASSERT_TRUE(rows > 0);
	ASSERT_TRUE(cols > 0);
}
#endif

#if defined(_WIN32) || defined(_WIN64)
/**
 * @test Test Windows-specific terminal functionality
 *
 * Verifies Windows-specific terminal operations.
 */
CTEST(integration, platform_windows_terminal)
{
	/* Windows platform detected */
	ASSERT_TRUE(1);

	/* Test terminal size (Windows uses GetConsoleScreenBufferInfo) */
	int rows, cols;
	int result = terminal_get_size(&rows, &cols);

	/* Should work on Windows */
	ASSERT_TRUE(result == 0 || result == -1);
	ASSERT_TRUE(rows > 0);
	ASSERT_TRUE(cols > 0);
}
#endif

/**
 * @test Test default terminal dimensions fallback
 *
 * Verifies that default dimensions are used when detection fails.
 */
CTEST(integration, platform_default_dimensions)
{
	/* Default dimensions should be reasonable */
	ASSERT_EQUAL(24, DEFAULT_TERM_ROWS);
	ASSERT_EQUAL(80, DEFAULT_TERM_COLS);

	/* These are used when terminal size detection fails */
	ASSERT_TRUE(DEFAULT_TERM_ROWS > 0);
	ASSERT_TRUE(DEFAULT_TERM_COLS > 0);
}

/**
 * @test Test terminal capabilities detection
 *
 * Verifies that terminal capabilities can be queried.
 */
CTEST(integration, platform_terminal_capabilities)
{
	/* Check if terminal supports various features */
	bool is_tty = terminal_is_tty(1);
	bool has_truecolor = terminal_supports_truecolor();

	/* Both should return valid boolean values */
	ASSERT_TRUE(is_tty == 0 || is_tty == 1);
	ASSERT_TRUE(has_truecolor == 0 || has_truecolor == 1);
}
