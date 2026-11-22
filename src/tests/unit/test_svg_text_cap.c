/**
 * @file test_svg_text_cap.c
 * @brief An over-long SVG <text> content run is truncated at a UTF-8 code point boundary, not dropped whole
 *
 * nanosvg.h collects at most 4094 bytes of content per <text> / <tspan> (NSVGparser.textContent[4096], the cap
 * bounds the text cost). A run (the text between two tags) that did not fit was dropped whole, so one 8000-character
 * run rendered nothing. Now as much as fits is kept, never splitting a multi-byte sequence, and the runs of one <text>
 * (split by comments) share the budget. Each case renders the over-long SVG and the same SVG with the text cut to
 * the expected prefix by hand: the pixels must match. text-anchor='end' at the right edge puts the LAST glyphs on
 * the canvas and shifts every glyph by the whole text width, so a cut one character off changes the pixels.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <img2/img2.h>

#include "../ctest.h"

/* A family no system has: the embedded DejaVu Sans Mono fallback, the same font on every machine */
#define FONT "font-family='img2-svg-text-cap-no-such-font'"

#define CAP 4094 /* sizeof(NSVGparser.textContent) - 2 */

static const char head[] = "<svg xmlns='http://www.w3.org/2000/svg' width='200' height='40'>"
                           "<text x='200' y='28' font-size='20' text-anchor='end' " FONT ">";
static const char tail[] = "</text></svg>";

static img2_image_t *svg_render(const char *svg)
{
	img2_frames_t fr;
	img2_decode_opts_t opts = IMG2_DECODE_OPTS_INIT;
	if (img2_decode((const uint8_t *)svg, strlen(svg), &opts, &fr) != IMG2_OK) {
		return NULL;
	}
	img2_image_t *img = fr.frames[0];
	fr.frames[0] = NULL;
	img2_frames_free(&fr);
	return img;
}

/* head + body + tail, malloc'd (NULL when out of memory) */
static char *wrap(const char *body, size_t n)
{
	char *svg = malloc(sizeof(head) - 1 + n + sizeof(tail));
	if (svg == NULL) {
		return NULL;
	}
	memcpy(svg, head, sizeof(head) - 1);
	memcpy(svg + sizeof(head) - 1, body, n);
	memcpy(svg + sizeof(head) - 1 + n, tail, sizeof(tail));
	return svg;
}

/* n ASCII characters, no spaces (the parser trims the run ends: a cut must not land next to whitespace) */
static void fill_ascii(char *dst, size_t n, size_t phase)
{
	static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
	for (size_t i = 0; i < n; i++) {
		dst[i] = chars[(i + phase) % (sizeof(chars) - 1)];
	}
}

static int count_inked(const img2_image_t *img)
{
	int n = 0;
	for (uint32_t y = 0; y < img->height; y++) {
		const uint8_t *row = img->pixels + (size_t)y * img->stride;
		for (uint32_t x = 0; x < img->width; x++) {
			n += row[x * 4 + 3] != 0;
		}
	}
	return n;
}

static int same_pixels(const img2_image_t *a, const img2_image_t *b)
{
	if (a->width != b->width || a->height != b->height) {
		return 0;
	}
	for (uint32_t y = 0; y < a->height; y++) {
		if (memcmp(a->pixels + (size_t)y * a->stride, b->pixels + (size_t)y * b->stride, (size_t)a->width * 4) != 0) {
			return 0;
		}
	}
	return 1;
}

/* Render body (n bytes) and the expected prefix (m bytes): inked, equal; the prefix one byte shorter differs */
static void check_render(const char *body, size_t n, const char *expect, size_t m)
{
	char *svg = wrap(body, n);
	char *ref = wrap(expect, m);
	ASSERT_NOT_NULL(svg);
	ASSERT_NOT_NULL(ref);
	img2_image_t *got = svg_render(svg);
	img2_image_t *want = svg_render(ref);
	ASSERT_NOT_NULL(got);
	ASSERT_NOT_NULL(want);
	int inked = count_inked(got);
	CTEST_LOG("inked pixels: %d (expected render: %d)", inked, count_inked(want));
	ASSERT_TRUE(inked > 0);
	ASSERT_TRUE(same_pixels(got, want));
	img2_image_destroy(want);
	free(ref);

	/* sanity: the pixels do tell a one-character difference apart */
	ref = wrap(expect, m - 1);
	ASSERT_NOT_NULL(ref);
	want = svg_render(ref);
	ASSERT_NOT_NULL(want);
	ASSERT_FALSE(same_pixels(got, want));
	img2_image_destroy(want);
	free(ref);
	img2_image_destroy(got);
	free(svg);
}

typedef struct {
	int warns;
} cap_log_t;

static void log_cb(img2_log_level_t level, const char *msg, void *user)
{
	cap_log_t *c = (cap_log_t *)user;
	if (level == IMG2_LOG_WARN && strstr(msg, "truncated") != NULL) {
		c->warns++;
	}
}

/* One 8000-character run renders its first 4094 bytes (it rendered nothing), one WARN per parse */
CTEST(svg_text_cap, long_run_truncated)
{
	char body[8000];
	fill_ascii(body, sizeof(body), 0);

	img2_log_level_t saved = img2_get_log_level();
	img2_set_log_level(IMG2_LOG_WARN);
	cap_log_t c = { 0 };
	img2_set_log_callback(log_cb, &c);
	/* two over-long texts in one SVG: still one warning */
	char *svg = wrap(body, sizeof(body));
	ASSERT_NOT_NULL(svg);
	char *two = malloc(strlen(svg) + sizeof(body) + 64);
	ASSERT_NOT_NULL(two);
	size_t hl = sizeof(head) - 1;
	memcpy(two, svg, hl + sizeof(body));
	size_t at = hl + sizeof(body);
	memcpy(two + at, "</text><text " FONT ">", strlen("</text><text " FONT ">"));
	at += strlen("</text><text " FONT ">");
	memcpy(two + at, body, sizeof(body));
	at += sizeof(body);
	memcpy(two + at, tail, sizeof(tail));
	img2_image_t *img = svg_render(two);
	img2_set_log_callback(NULL, NULL);
	img2_set_log_level(saved);
	ASSERT_NOT_NULL(img);
	img2_image_destroy(img);
	free(two);
	free(svg);
	ASSERT_EQUAL(1, c.warns);

	check_render(body, sizeof(body), body, CAP);
}

/* The 4094-byte boundary inside a 3-byte sequence (U+20AC at bytes 4093..4095): the partial character is left out */
CTEST(svg_text_cap, cut_inside_utf8_sequence)
{
	char body[5000];
	fill_ascii(body, CAP - 1, 0);
	memcpy(body + CAP - 1, "\xE2\x82\xAC", 3);
	fill_ascii(body + CAP + 2, sizeof(body) - (CAP + 2), 7);

	check_render(body, sizeof(body), body, CAP - 1);
}

/* Two runs of one <text> split by a comment: the second only partly fits (1093 bytes left, a 2-byte U+00E9 sequence
 * straddles the end: 546 of them are kept) */
CTEST(svg_text_cap, second_run_partial)
{
	enum { FIRST = 3001, SECOND = 3000 };
	static const char comment[] = "<!-- split -->";
	char body[FIRST + sizeof(comment) - 1 + SECOND * 2];
	fill_ascii(body, FIRST, 0);
	memcpy(body + FIRST, comment, sizeof(comment) - 1);
	char *run2 = body + FIRST + sizeof(comment) - 1;
	for (size_t i = 0; i < SECOND; i++) {
		run2[2 * i] = (char)0xC3;
		run2[2 * i + 1] = (char)0xA9;
	}

	char expect[CAP];
	memcpy(expect, body, FIRST);
	memcpy(expect + FIRST, run2, 546 * 2);
	ASSERT_EQUAL(CAP - 1, FIRST + 546 * 2);

	check_render(body, sizeof(body), expect, FIRST + 546 * 2);
}
