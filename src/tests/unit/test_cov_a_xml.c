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

/* ===== well-formed variants ===== */

CTEST(xml_ok, names_text_and_attributes)
{
	ASSERT_EQUAL(0, parse("<?xml version='1.0'?><!-- a --><!-- b --><_r:x-y.z_ a='1' b=\"2\" :c = '3'>  12 <k/> 34  </_r:x-y.z_>"));
	img2_xml_node_t *root = img2_xml_document_root_element(s_doc);
	ASSERT_STR("_r:x-y.z_", img2_xml_element_name(root));
	ASSERT_STR("12", root_text()); /* trailing whitespace of a text run trimmed */
	ASSERT_STR("1", img2_xml_element_attribute(root, "a"));
	ASSERT_STR("2", img2_xml_element_attribute(root, "b"));
	ASSERT_STR("3", img2_xml_element_attribute(root, ":c"));
	ASSERT_NULL(img2_xml_element_attribute(root, "missing"));
	img2_xml_document_destroy(s_doc);
	s_doc = NULL;
}

CTEST(xml_ok, dom_queries_and_vtable)
{
	ASSERT_EQUAL(0, parse("<r><!--note-->text<a>1</a>mid<b x='y'/><a>2</a></r>"));
	img2_xml_node_t *root = img2_xml_document_root_element(s_doc);
	img2_xml_node_t *comment = root->first_child;
	ASSERT_EQUAL(IMG2_XML_NODE_COMMENT, comment->type);
	img2_xml_node_t *text = comment->next_sibling;
	ASSERT_EQUAL(IMG2_XML_NODE_TEXT, text->type);

	/* per-type virtual functions, also called with a node of another type */
	ASSERT_STR("r", root->get_value(root));
	ASSERT_NULL(root->get_value(text));
	ASSERT_STR("text", text->get_value(text));
	ASSERT_NULL(text->get_value(root));
	ASSERT_STR("note", comment->get_value(comment));
	ASSERT_NULL(comment->get_value(root));
	ASSERT_NULL(root->first_child_element(NULL, NULL));
	ASSERT_NULL(root->first_child_element(root, "missing"));
	img2_xml_node_t *b = root->first_child_element(root, "b");
	ASSERT_NOT_NULL(b);
	ASSERT_STR("y", b->get_attribute(b, "x"));
	ASSERT_NULL(b->get_attribute(b, "z"));
	ASSERT_NULL(b->get_attribute(NULL, "x"));
	ASSERT_NULL(b->get_attribute(text, "x"));
	ASSERT_NULL(b->get_attribute(b, NULL));

	/* public queries */
	img2_xml_node_t *a = img2_xml_element_first_child(root, "a");
	ASSERT_STR("1", img2_xml_element_text(a));
	ASSERT_TRUE(b == img2_xml_element_next_sibling(a, NULL));
	img2_xml_node_t *a2 = img2_xml_element_next_sibling(a, "a");
	ASSERT_STR("2", img2_xml_element_text(a2));
	ASSERT_NULL(img2_xml_element_next_sibling(a2, NULL));
	ASSERT_NULL(img2_xml_element_next_sibling(a, "missing"));
	ASSERT_NULL(img2_xml_element_next_sibling(NULL, NULL));
	ASSERT_NULL(img2_xml_element_text(b)); /* no text child */
	ASSERT_STR("text", img2_xml_element_text(root));
	ASSERT_NULL(img2_xml_element_first_child(NULL, NULL));
	ASSERT_NULL(img2_xml_element_first_child(text, NULL));
	ASSERT_NULL(img2_xml_element_attribute(NULL, "x"));
	ASSERT_NULL(img2_xml_element_attribute(text, "x"));
	ASSERT_NULL(img2_xml_element_attribute(b, NULL));
	ASSERT_NULL(img2_xml_element_text(NULL));
	ASSERT_NULL(img2_xml_element_text(text));
	ASSERT_NULL(img2_xml_element_name(NULL));
	ASSERT_NULL(img2_xml_element_name(text));
	ASSERT_NULL(img2_xml_document_root_element(NULL));
	img2_xml_document_destroy(s_doc);
	s_doc = NULL;
}

/* ===== malformed documents ===== */

CTEST(xml_bad, every_parse_error)
{
	static const char *const bad[] = {
		"",                   /* no root element */
		"text",               /* character data before the root */
		"<?xml version='1.0'", /* unterminated declaration, then nothing */
		"<!-- unterminated",
		"<1a/>",              /* element name expected */
		"<a",                 /* end of input inside the start tag */
		"<a/",
		"<a/b>",              /* '/' not followed by '>' */
		"<a 1='x'>",          /* attribute name expected */
		"<a b>",              /* '=' expected */
		"<a b",               /* end of input after the attribute name */
		"<a b=",              /* end of input after '=' */
		"<a b= ",
		"<a b=x>",            /* quote expected */
		"<a b='x",            /* unterminated value */
		"<a><!-- x",          /* unterminated comment in content */
		"<a><?pi?></a>",      /* processing instruction in content */
		"<a>12",              /* end of input in content */
		"<a>abc",
		"<a></1>",            /* closing tag name expected */
		"<a></b>",            /* mismatched closing tag */
		"<a></ab>",
		"<ab></a>",
		"<a></a",             /* end of input in the closing tag */
		"<a></a x>",
	};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		if (parse(bad[i]) != -1) {
			CTEST_ERR("parsed: '%s'", bad[i]);
		}
	}
	img2_xml_document_t *doc = img2_xml_document_create();
	ASSERT_NOT_NULL(doc);
	ASSERT_EQUAL(-1, img2_xml_document_parse(NULL, "<a/>", 4));
	ASSERT_EQUAL(-1, img2_xml_document_parse(doc, NULL, 4));
	img2_xml_document_destroy(doc);
	img2_xml_document_destroy(NULL);
	img2_xml_document_destroy(s_doc);
	s_doc = NULL;
}
