/**
 * @file test_svg_text_perf.c
 * @brief SVG <text> rendering must be linear in the text length (fuzzing S17, P1: slow-unit-85b5c61e, a 17 KB SVG
 *        took ~3.5 s), and the fix must not change any pixel
 *
 * All glyphs of a <text> are one shape, so the edges of every glyph start on the same few scanlines. nanosvgrast.h
 * inserted each new active edge into the x-sorted active list by a linear scan from the list head: O(edges^2) on
 * those scanlines. nsvg__insertActive now merges the scanline's new edges in O(A + n log n), building the very list
 * the one-by-one insertion built (equal x included).
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <img2/img2.h>

#include "../ctest.h"

/* A family no system has: the embedded DejaVu Sans Mono fallback, the same font on every machine */
#define FONT "font-family='img2-svg-text-perf-no-such-font'"

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

/* 300x100 SVG with one <text> of n characters (a sentence repeated; n < 4094, the parser's text buffer) */
static char *long_text_svg(size_t n)
{
	static const char head[] = "<svg xmlns='http://www.w3.org/2000/svg' width='300' height='100'><text x='0' y='50' font-size='20' " FONT ">";
	static const char tail[] = "</text></svg>";
	static const char words[] = "The quick brown fox jumps over the lazy dog 0123456789. ";
	char *svg = malloc(sizeof(head) + n + sizeof(tail));
	if (svg == NULL) {
		return NULL;
	}
	memcpy(svg, head, sizeof(head) - 1);
	for (size_t i = 0; i < n; i++) {
		svg[sizeof(head) - 1 + i] = words[i % (sizeof(words) - 1)];
	}
	memcpy(svg + sizeof(head) - 1 + n, tail, sizeof(tail));
	return svg;
}

static double thread_cpu_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* Best of three decodes (thread CPU time: robust to a loaded machine) */
static double decode_ms(const char *svg)
{
	double best = 1e30;
	for (int i = 0; i < 3; i++) {
		double t0 = thread_cpu_ms();
		img2_image_t *img = svg_render(svg);
		double t = thread_cpu_ms() - t0;
		ASSERT_NOT_NULL(img);
		img2_image_destroy(img);
		if (t < best) {
			best = t;
		}
	}
	return best;
}

/*
 * 16x the text must take < 40x the time (linear: ~16x, less with the per-shape fixed cost; the quadratic insertion
 * took > 100x). The small decode is floored at 2 ms so a very fast build cannot fail on timer granularity.
 */
CTEST(svg_text_perf, long_text_is_linear)
{
	char *small = long_text_svg(250);
	char *big = long_text_svg(4000);
	ASSERT_NOT_NULL(small);
	ASSERT_NOT_NULL(big);
	img2_image_destroy(svg_render(small)); /* warm up: the font cache is built on the first text */
	double t_small = decode_ms(small);
	double t_big = decode_ms(big);
	free(small);
	free(big);
	CTEST_LOG("250 chars: %.2f ms, 4000 chars: %.2f ms (%.1fx)", t_small, t_big, t_big / t_small);
	ASSERT_TRUE(t_big < 40.0 * (t_small < 2.0 ? 2.0 : t_small));
}

static uint64_t pixels_fnv1a(const img2_image_t *img)
{
	uint64_t h = 0xcbf29ce484222325ULL;
	for (uint32_t y = 0; y < img->height; y++) {
		const uint8_t *row = img->pixels + (size_t)y * img->stride;
		for (size_t i = 0; i < (size_t)img->width * 4; i++) {
			h = (h ^ row[i]) * 0x100000001b3ULL;
		}
	}
	return h;
}

/*
 * Pixels unchanged by the batch insertion: FNV-1a of the RGBA pixels as rendered before the fix. Text (repeated and
 * mirrored glyphs, all anchors, tspans) and a path of coincident and overlapping subpaths give many edges with equal
 * x starting on the same scanline, the ties whose order the merge must keep.
 */
CTEST(svg_text_perf, pixels_match_pre_fix_render)
{
	static const char svg[] =
		"<svg xmlns='http://www.w3.org/2000/svg' width='160' height='72'>"
		"<text x='2' y='14' font-size='13' " FONT ">WWWWWWWWWWWWWWWWWWWW</text>"
		"<text x='80' y='30' font-size='11' text-anchor='middle' " FONT ">AVAVA |||| iIl1 MWMW</text>"
		"<text x='158' y='46' font-size='15.5' text-anchor='end' " FONT " fill='#3060c0'>Hi<tspan dy='4'>there</tspan>!</text>"
		"<path fill-rule='evenodd' fill='#c03030' fill-opacity='0.7' d='M10,50 h20 v20 h-20 Z M10,50 h20 v20 h-20 Z"
		" M20,50 h20 v20 h-20 Z M10,55 L40,55 L10,65 Z'/>"
		"<path fill='#208020' d='M60,50 h10 v10 h-10 Z M60,50 h10 v10 h-10 Z M65,50 h10 v20 h-10 Z'/>"
		"</svg>";
	img2_image_t *img = svg_render(svg);
	ASSERT_NOT_NULL(img);
	ASSERT_EQUAL_U(160, img->width);
	ASSERT_EQUAL_U(72, img->height);
	uint64_t h = pixels_fnv1a(img);
	img2_image_destroy(img);
	ASSERT_EQUAL_U(0xfda36b156cb1b432ULL, h);
}
