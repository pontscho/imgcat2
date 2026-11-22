/**
 * @file test_svg_state.c
 * @brief SVG decodes must not depend on earlier decodes in the same process (fuzz_decode_forced crash-1fd75303,
 *        fuzz_decode crash-4868a75f: widths of 1.55e38 / 4.6e22 only after other inputs, 12x8 standalone)
 *
 * nanosvg.h's transform parsers (nsvg__parseRotate / Translate / Scale / SkewX / SkewY) read their stack args[]
 * past the count of numbers actually parsed, and nsvg__parseTransform premultiplied its stack t[] after a matrix()
 * with fewer than six numbers left it unwritten. 'rotate(30 0)' (two numbers: the centre's y missing) took the
 * centre's y from whatever an earlier transform left in that stack slot, so a previous 'rotate(a x 1e30)' moved the
 * shape (and the size guessed from the shape bounds) to 1e30. Each case decodes a poisoning SVG (A) and then the
 * incomplete transform (B); B must match its explicitly written equivalent.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <img2/img2.h>

#include "../ctest.h"

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

/* Decode poison (result ignored), then svg, and compare svg with the explicitly written reference */
static void check_after(const char *poison, const char *svg, const char *reference)
{
	img2_image_t *want = svg_render(reference);
	ASSERT_NOT_NULL(want);
	img2_frames_t fr;
	img2_decode_opts_t opts = IMG2_DECODE_OPTS_INIT;
	if (img2_decode((const uint8_t *)poison, strlen(poison), &opts, &fr) == IMG2_OK) {
		img2_frames_free(&fr);
	}
	img2_image_t *got = svg_render(svg);
	ASSERT_NOT_NULL(got);
	ASSERT_EQUAL_U(want->width, got->width);
	ASSERT_EQUAL_U(want->height, got->height);
	ASSERT_EQUAL(0, memcmp(want->pixels, got->pixels, (size_t)want->stride * want->height));
	img2_image_destroy(want);
	img2_image_destroy(got);
}

/* No width / height: the size is guessed from the shape bounds, so a stale transform shows in the dimensions */
#define SVG_ELLIPSE(tf) "<svg xmlns='http://www.w3.org/2000/svg'><ellipse cx='4' cy='4' rx='4' ry='2' fill='red' transform='" tf "'/></svg>"

CTEST(svg_state, rotate_missing_centre_y)
{
	check_after(SVG_ELLIPSE("rotate(0 1e30 1e30)"), SVG_ELLIPSE("rotate(30 0)"), SVG_ELLIPSE("rotate(30 0 0)"));
}

CTEST(svg_state, rotate_without_numbers)
{
	check_after(SVG_ELLIPSE("rotate(1e30)"), SVG_ELLIPSE("rotate() translate(1)"), SVG_ELLIPSE("translate(1)"));
}

CTEST(svg_state, translate_skew_without_numbers)
{
	check_after(SVG_ELLIPSE("translate(1e30 1e30)"), SVG_ELLIPSE("translate() rotate(1)"), SVG_ELLIPSE("rotate(1)"));
	check_after(SVG_ELLIPSE("skewX(89.9999)"), SVG_ELLIPSE("skewX() rotate(1)"), SVG_ELLIPSE("rotate(1)"));
	check_after(SVG_ELLIPSE("skewY(89.9999)"), SVG_ELLIPSE("skewY() rotate(1)"), SVG_ELLIPSE("rotate(1)"));
}

CTEST(svg_state, scale_without_numbers)
{
	check_after(SVG_ELLIPSE("scale(1e30 1e30)"), SVG_ELLIPSE("scale() rotate(1)"), SVG_ELLIPSE("rotate(1)"));
}

CTEST(svg_state, matrix_with_too_few_numbers)
{
	check_after(SVG_ELLIPSE("matrix(1e30 0 0 1e30 1e30 1e30)"), SVG_ELLIPSE("matrix(1 0) rotate(1)"), SVG_ELLIPSE("rotate(1)"));
}
