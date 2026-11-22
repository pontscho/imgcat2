/**
 * @file test_cov_a_xml.c
 * @brief XML parser (metadata/xml_parser.c): nesting-depth limit, malformed documents, DOM queries
 *
 * Every document is built in-test. The deep-nesting cases parse on a thread with a 1 MiB stack, so a parser
 * without a depth limit overflows the stack deterministically (and quickly) instead of depending on the
 * process stack limit.
 */

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../libimg2/metadata/xml_parser.h"
#include "../ctest.h"

/* ===== nesting depth ===== */

typedef struct {
	const char *xml;
	size_t len;
	int result;
} deep_ctx_t;

static void *deep_parse_thread(void *arg)
{
	deep_ctx_t *c = arg;
	img2_xml_document_t *doc = img2_xml_document_create();
	c->result = doc != NULL ? img2_xml_document_parse(doc, c->xml, c->len) : -2;
	img2_xml_document_destroy(doc);
	return NULL;
}

/* parse on a thread with a 1 MiB stack; returns img2_xml_document_parse()'s result (-2: no document) */
static int parse_on_small_stack(const char *xml, size_t len)
{
	deep_ctx_t c = { xml, len, -3 };
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 1024 * 1024);
	pthread_t th;
	if (pthread_create(&th, &attr, deep_parse_thread, &c) != 0) {
		pthread_attr_destroy(&attr);
		return -4;
	}
	pthread_join(th, NULL);
	pthread_attr_destroy(&attr);
	return c.result;
}

/* "<a>" x open, then "</a>" x close */
static char *nested(int open, int close, size_t *len)
{
	char *xml = malloc((size_t)open * 3 + (size_t)close * 4 + 1);
	if (xml == NULL) {
		return NULL;
	}
	char *p = xml;
	for (int i = 0; i < open; i++) {
		memcpy(p, "<a>", 3);
		p += 3;
	}
	for (int i = 0; i < close; i++) {
		memcpy(p, "</a>", 4);
		p += 4;
	}
	*p = '\0';
	*len = (size_t)(p - xml);
	return xml;
}

CTEST(xml_depth, hostile_nesting_is_a_parse_error)
{
	/* 200000 levels: without a depth limit the recursive descent overflows the 1 MiB stack (SIGSEGV) */
	size_t len = 0;
	char *xml = nested(200000, 0, &len);
	ASSERT_NOT_NULL(xml);
	ASSERT_EQUAL(-1, parse_on_small_stack(xml, len));
	free(xml);
}

CTEST(xml_depth, limit_is_inclusive)
{
	size_t len = 0;
	char *xml = nested(IMG2_XML_MAX_DEPTH, IMG2_XML_MAX_DEPTH, &len);
	ASSERT_NOT_NULL(xml);
	ASSERT_EQUAL(0, parse_on_small_stack(xml, len));
	free(xml);

	xml = nested(IMG2_XML_MAX_DEPTH + 1, IMG2_XML_MAX_DEPTH + 1, &len);
	ASSERT_NOT_NULL(xml);
	ASSERT_EQUAL(-1, parse_on_small_stack(xml, len));
	free(xml);
}

/* ===== helpers ===== */

static img2_xml_document_t *s_doc;

/* parses xml into s_doc (replacing the previous one); returns img2_xml_document_parse()'s result */
static int parse(const char *xml)
{
	img2_xml_document_destroy(s_doc);
	s_doc = img2_xml_document_create();
	if (s_doc == NULL) {
		return -2;
	}
	return img2_xml_document_parse(s_doc, xml, strlen(xml));
}

static const char *root_text(void)
{
	return img2_xml_element_text(img2_xml_document_root_element(s_doc));
}

/* ===== '>' and other closing markers in character data ===== */

CTEST(xml_text, closing_markers_are_character_data)
{
	/* '>' is legal unescaped in XML character data (XMP writers emit it, e.g. "a > b" in dc:description);
	 * "/>", "?>" and "-->" outside markup are plain text too */
	ASSERT_EQUAL(0, parse("<r>a > b</r>"));
	ASSERT_STR("a > b", root_text());
	ASSERT_EQUAL(0, parse("<r>> x</r>"));
	ASSERT_STR("> x", root_text());
	ASSERT_EQUAL(0, parse("<r>1/>2</r>"));
	ASSERT_STR("1/>2", root_text());
	ASSERT_EQUAL(0, parse("<r>/> ?> --></r>"));
	ASSERT_STR("/> ?> -->", root_text());
	img2_xml_document_destroy(s_doc);
	s_doc = NULL;
}
