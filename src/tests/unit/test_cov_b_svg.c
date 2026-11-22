/**
 * @file test_cov_b_svg.c
 * @brief nanosvg decoder coverage (decoder_nanosvg.c): sizing and limits, the CSS class inliner (class
 *        lists, whitespace, malformed attributes and rules, buffer growth), stray style tags
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../libimg2/decoders/decoder.h"
#include "test_cov_b_util.h"

static img2_status_t svg_decode_str(const char *svg, img2_frames_t *out)
{
	return img2_decode_svg_nanosvg((const uint8_t *)svg, strlen(svg), NULL, out);
}

static img2_status_t svg_decode_buf(const cb_buf_t *b, img2_frames_t *out)
{
	return img2_decode_svg_nanosvg(b->d, b->n, NULL, out);
}

CTEST(cov_b_svg, plain_rect)
{
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, svg_decode_str("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"4\" height=\"3\"><rect width=\"4\" height=\"3\" fill=\"#ff0000\"/></svg>", &fr));
	ASSERT_EQUAL(1, fr.count);
	ASSERT_EQUAL_U(4u, fr.frames[0]->width);
	ASSERT_EQUAL_U(3u, fr.frames[0]->height);
	CB_ASSERT_PX(&fr, 0, 1, 1, 255, 0, 0, 255);
	img2_frames_free(&fr);
}

CTEST(cov_b_svg, sizing_and_limits)
{
	img2_frames_t fr;
	/* no size, and a zero height: the 512x512 default */
	ASSERT_EQUAL(IMG2_OK, svg_decode_str("<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>", &fr));
	ASSERT_EQUAL_U(512u, fr.frames[0]->width);
	img2_frames_free(&fr);
	ASSERT_EQUAL(IMG2_OK, svg_decode_str("<svg width=\"5\" height=\"0\"></svg>", &fr));
	ASSERT_EQUAL_U(512u, fr.frames[0]->height);
	img2_frames_free(&fr);
	/* positive sizes that round to 0 */
	ASSERT_EQUAL(IMG2_E_FORMAT, svg_decode_str("<svg width=\"0.3\" height=\"2\"></svg>", &fr));
	ASSERT_EQUAL(IMG2_E_FORMAT, svg_decode_str("<svg width=\"2\" height=\"0.3\"></svg>", &fr));
	/* beyond IMG2_MAX_DIMENSION, and within it but beyond IMG2_MAX_PIXELS */
	ASSERT_EQUAL(IMG2_E_LIMIT, svg_decode_str("<svg width=\"70000\" height=\"2\"></svg>", &fr));
	ASSERT_EQUAL(IMG2_E_LIMIT, svg_decode_str("<svg width=\"2\" height=\"70000\"></svg>", &fr));
	ASSERT_EQUAL(IMG2_E_LIMIT, svg_decode_str("<svg width=\"20000\" height=\"20000\"></svg>", &fr));
	ASSERT_NULL(fr.frames);
}

CTEST(cov_b_svg, css_class_lists)
{
	/* class lists separated by space, tab and newline; a rule with leading and trailing whitespace of every
	 * kind; a whitespace-only rule; an unknown class; a rule without '}'; an over-long class name */
	char longname[300];
	memset(longname, 'n', sizeof(longname) - 1);
	longname[sizeof(longname) - 1] = '\0';
	cb_buf_t b = { 0 };
	cb_str(&b, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"4\" height=\"2\"><style>");
	cb_str(&b, ".red { \n\tfill:#ff0000\t\n } .w {  } .op {fill-opacity:1;} .open {stroke:none");
	cb_str(&b, "</style>");
	cb_str(&b, "<rect x=\"0\" width=\"2\" height=\"2\" class=\" \top\nnope\tred \"/>");
	cb_str(&b, "<rect x=\"2\" width=\"2\" height=\"2\" fill=\"#0000ff\" class=\"w open ");
	cb_str(&b, longname);
	cb_str(&b, "\"/>");
	cb_str(&b, "<rect class=\"  \"/>");
	cb_str(&b, "<rect x=\"0\" y=\"1\" width=\"1\" height=\"1\" class=\"red\"/>"); /* the rule alone: both ends trimmed */
	cb_str(&b, "</svg>");
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, svg_decode_buf(&b, &fr));
	CB_ASSERT_PX(&fr, 0, 0, 0, 255, 0, 0, 255);
	CB_ASSERT_PX(&fr, 0, 3, 1, 0, 0, 255, 255);
	img2_frames_free(&fr);
	cb_free(&b);
}

CTEST(cov_b_svg, css_limits_and_malformed_attributes)
{
	/* a class list longer than the 512-byte buffer; rules that overflow the 4 KiB style buffer; a class
	 * attribute without its closing quote at the end of the data */
	char rule[1200];
	memset(rule, ' ', sizeof(rule) - 1);
	memcpy(rule, "fill:#00ff00;", 13);
	rule[sizeof(rule) - 1] = '\0';
	cb_buf_t b = { 0 };
	cb_str(&b, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"2\" height=\"2\"><style>");
	const char *names[5] = { ".k1 {", ".k2 {", ".k3 {", ".k4 {", ".k5 {" };
	for (int i = 0; i < 5; i++) {
		cb_str(&b, names[i]);
		cb_str(&b, rule);
		cb_str(&b, "} ");
	}
	cb_str(&b, "</style><rect width=\"2\" height=\"2\" class=\"k1 k2 k3 k4 k5");
	for (int i = 0; i < 130; i++) { /* the class list is cut at 511 bytes */
		cb_str(&b, " pad");
	}
	cb_str(&b, "\"/><rect class=\"unterminated");
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, svg_decode_buf(&b, &fr));
	CB_ASSERT_PX(&fr, 0, 1, 1, 0, 255, 0, 255);
	img2_frames_free(&fr);
	cb_free(&b);
}

CTEST(cov_b_svg, css_output_growth)
{
	const int modes[4] = { SVG_GROW_COPY, SVG_GROW_REST, SVG_GROW_EXACT, SVG_GROW_STYLE };
	for (int i = 0; i < 4; i++) {
		cb_buf_t b = { 0 };
		svg_growth(&b, modes[i]);
		img2_frames_t fr;
		ASSERT_EQUAL(IMG2_OK, svg_decode_buf(&b, &fr));
		CB_ASSERT_PX(&fr, 0, 0, 0, 0, 255, 0, 255);
		img2_frames_free(&fr);
		cb_free(&b);
	}
}

CTEST(cov_b_svg, style_tags_out_of_order)
{
	img2_frames_t fr;
	/* "</style>" before "<style>" (here in a comment): the CSS is the text between the real tags */
	ASSERT_EQUAL(IMG2_OK, svg_decode_str("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"2\" height=\"2\"><!-- </style> -->"
	                                     "<style>.g {fill:#00ff00;}</style><rect width=\"2\" height=\"2\" class=\"g\"/></svg>",
	                                     &fr));
	CB_ASSERT_PX(&fr, 0, 1, 1, 0, 255, 0, 255);
	img2_frames_free(&fr);
	/* <style> without </style>: no CSS processing */
	ASSERT_EQUAL(IMG2_OK, svg_decode_str("<svg width=\"2\" height=\"2\"><style>.g {fill:#00ff00;}<rect width=\"2\" height=\"2\" class=\"g\"/></svg>", &fr));
	img2_frames_free(&fr);
}

CTEST(cov_b_svg, argument_checks)
{
	const uint8_t one = '<';
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_svg_nanosvg(&one, 1, NULL, NULL));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_svg_nanosvg(NULL, 1, NULL, &fr));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_svg_nanosvg(&one, 0, NULL, &fr));
}
