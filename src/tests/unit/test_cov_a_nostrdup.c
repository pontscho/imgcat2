/**
 * @file test_cov_a_nostrdup.c
 * @brief Font manager with every strdup() failing: an empty font cache and no fallback font
 *
 * Linked with -Wl,--wrap=strdup (Linux only): while s_fail_strdup is set, every strdup() made by the test or
 * the statically linked img2 archive returns NULL. alloc_fail.h fails one allocation per run; an empty cache
 * needs all of them to fail (system font directories: path copies; embedded entries and the fallback:
 * family copies).
 */

#define _GNU_SOURCE

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <img2/img2.h>

#include "../../libimg2/text/font_manager.h"
#include "../ctest.h"

char *__real_strdup(const char *s);
char *__wrap_strdup(const char *s);

static bool s_fail_strdup;

char *__wrap_strdup(const char *s)
{
	return s_fail_strdup ? NULL : __real_strdup(s);
}

static int count_font(const img2_font_info_t *info, void *user)
{
	(void)info;
	(*(int *)user)++;
	return 0;
}

CTEST(font_nostrdup, empty_cache_and_no_fallback)
{
	setenv("HOME", "/nonexistent-cov-a-home", 1); /* ~/... directories do not exist */
	s_fail_strdup = true;
	img2_init();
	int count = 0;
	img2_status_t st_each = img2_font_foreach(count_font, &count);
	img2_font_t *mono = (img2_font_t *)1;
	img2_status_t st_mono = img2_font_load("monospace", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL, &mono);
	img2_font_t *other = (img2_font_t *)1;
	img2_status_t st_other = img2_font_load("Other", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL, &other);
	img2_font_t *fb = (img2_font_t *)1;
	img2_status_t st_fb = img2_font_load_fallback(&fb);
	img2_shutdown();
	s_fail_strdup = false;

	ASSERT_EQUAL(IMG2_OK, st_each);
	ASSERT_EQUAL(0, count);
	ASSERT_EQUAL(IMG2_E_NOT_FOUND, st_mono); /* "DejaVu Sans Mono" is not cached and there is no fallback */
	ASSERT_NULL(mono);
	ASSERT_EQUAL(IMG2_E_NOT_FOUND, st_other);
	ASSERT_NULL(other);
	ASSERT_EQUAL(IMG2_E_NOMEM, st_fb);
	ASSERT_NULL(fb);
}
