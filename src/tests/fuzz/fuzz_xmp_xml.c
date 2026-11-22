/**
 * @file fuzz_xmp_xml.c
 * @brief libFuzzer target: the XMP reader (img2_xmp_parse_xml / img2_xmp_parse_jpeg) and the internal XML DOM
 *        parser (img2_xml_document_parse + a walk over every node with the accessor functions)
 *
 * Input layout: byte 0 % 3 selects the mode (0: XMP from an XML packet, 1: XMP from a JPEG APP1 segment,
 * 2: XML DOM parse + walk), the rest is the data (not NUL-terminated: the parsers take a length).
 */

#include "fuzz_common.h"

#include "metadata/xml_parser.h"

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void)argc;
	(void)argv;
	fuzz_setup_logging();
	return 0;
}

static const char *const k_names[] = { "rdf:li", "rdf:Description", "rdf:about", "dc:subject", "x:xmptk", NULL };

static size_t walk(const img2_xml_node_t *node, int depth)
{
	size_t n = 0;

	if (depth > IMG2_XML_MAX_DEPTH + 1) {
		abort(); /* deeper than the parser's documented bound */
	}
	for (const img2_xml_node_t *c = node; c != NULL; c = c->next_sibling) {
		const char *v = c->get_value != NULL ? c->get_value(c) : NULL;
		n += v != NULL ? strlen(v) : 0;
		if (c->type == IMG2_XML_NODE_ELEMENT) {
			const char *name = img2_xml_element_name(c);
			const char *text = img2_xml_element_text(c);
			n += name != NULL ? strlen(name) : 0;
			n += text != NULL ? strlen(text) : 0;
			for (size_t k = 0; k < sizeof(k_names) / sizeof(k_names[0]); k++) {
				const char *a = img2_xml_element_attribute(c, k_names[k]);
				n += a != NULL ? strlen(a) : 0;
				n += img2_xml_element_first_child(c, k_names[k]) != NULL;
				n += img2_xml_element_next_sibling(c, k_names[k]) != NULL;
			}
			if (c->get_attribute != NULL && name != NULL) {
				n += c->get_attribute(c, name) != NULL;
			}
			if (c->first_child_element != NULL) {
				n += c->first_child_element(c, NULL) != NULL;
			}
		}
		if (c->first_child != NULL) {
			if (c->first_child->parent != c) {
				abort();
			}
			n += walk(c->first_child, depth + 1);
		}
	}
	return n;
}

static void check_xmp(const img2_xmp_t *xmp)
{
	FUZZ_CHECK_CSTR(xmp->creator);
	FUZZ_CHECK_CSTR(xmp->copyright);
	FUZZ_CHECK_CSTR(xmp->description);
	FUZZ_CHECK_CSTR(xmp->title);
	FUZZ_CHECK_CSTR(xmp->create_date);
	FUZZ_CHECK_CSTR(xmp->modify_date);
	FUZZ_CHECK_CSTR(xmp->label);
	FUZZ_CHECK_CSTR(xmp->city);
	FUZZ_CHECK_CSTR(xmp->country);
	FUZZ_CHECK_CSTR(xmp->credit);
	FUZZ_CHECK_CSTR(xmp->source);
	if (xmp->has_keywords && (xmp->keywords == NULL || strlen(xmp->keywords) == 0)) {
		abort();
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size < 1) {
		return 0;
	}
	const uint8_t *body = data + 1;
	size_t len = size - 1;

	switch (data[0] % 3) {
		case 0:
		case 1: {
			img2_xmp_t xmp;
			img2_xmp_init(&xmp);
			if (data[0] % 3 == 0) {
				(void)img2_xmp_parse_xml(&xmp, (const char *)body, len);
			} else {
				(void)img2_xmp_parse_jpeg(&xmp, body, len);
			}
			check_xmp(&xmp);
			img2_xmp_clear(&xmp);
			break;
		}
		default: {
			img2_xml_document_t *doc = img2_xml_document_create();
			if (doc == NULL) {
				return 0;
			}
			if (img2_xml_document_parse(doc, (const char *)body, len) == 0) {
				volatile size_t sink = walk(img2_xml_document_root_element(doc), 1);
				(void)sink;
			}
			img2_xml_document_destroy(doc);
			break;
		}
	}
	return 0;
}
