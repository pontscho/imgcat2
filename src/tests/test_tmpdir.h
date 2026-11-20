/**
 * @file test_tmpdir.h
 * @brief Per-build temporary directory for tests
 *
 * Tests must never write to /tmp. CTest sets $TMPDIR to the per-build
 * test-tmp directory; when a test binary is run directly without $TMPDIR,
 * the compile-time IMGCAT2_TEST_TMPDIR default (set by CMake) is used.
 */

#ifndef IMGCAT2_TESTS_TEST_TMPDIR_H
#define IMGCAT2_TESTS_TEST_TMPDIR_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef IMGCAT2_TEST_TMPDIR
#error "IMGCAT2_TEST_TMPDIR must be defined by the build system"
#endif

/**
 * @brief Return the directory tests should use for temporary files
 *
 * @return $TMPDIR if set and non-empty, else the compile-time IMGCAT2_TEST_TMPDIR
 */
static inline const char *test_tmpdir(void)
{
	const char *tmpdir = getenv("TMPDIR");
	return (tmpdir != NULL && tmpdir[0] != '\0') ? tmpdir : IMGCAT2_TEST_TMPDIR;
}

/**
 * @brief Build "<test_tmpdir()>/<name>" into buf
 *
 * @param buf Output buffer
 * @param size Size of buf in bytes
 * @param name File name to append
 * @return buf
 */
static inline char *test_tmp_path(char *buf, size_t size, const char *name)
{
	snprintf(buf, size, "%s/%s", test_tmpdir(), name);
	return buf;
}

#endif /* IMGCAT2_TESTS_TEST_TMPDIR_H */
