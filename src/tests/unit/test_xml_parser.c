/**
 * @file test_xml_parser.c
 * @brief Unit tests for XML parser
 *
 * Tests XML parsing, DOM tree building, element queries, attribute extraction,
 * and edge case handling for XMP metadata parsing.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../imgcat2/metadata/xml_parser.h"
#include "../ctest.h"

/**
 * @test Test parsing of simple XML document
 *
 * Verifies that the parser can handle a basic XML document with a single element.
 */
CTEST(xml_parser, parse_simple_element)
{
	const char *xml = "<root>Hello World</root>";
	xml_document_t *doc = xml_document_create();
	ASSERT_NOT_NULL(doc);

	int result = xml_document_parse(doc, xml, strlen(xml));
	ASSERT_EQUAL(0, result);

	xml_node_t *root = xml_document_root_element(doc);
	ASSERT_NOT_NULL(root);

	const char *name = xml_element_name(root);
	ASSERT_NOT_NULL(name);
	ASSERT_STR("root", name);

	const char *text = xml_element_text(root);
	ASSERT_NOT_NULL(text);
	ASSERT_STR("Hello World", text);

	xml_document_destroy(doc);
}

/**
 * @test Test parsing of nested elements
 *
 * Verifies that the parser correctly builds a DOM tree with nested elements.
 */
CTEST(xml_parser, parse_nested_elements)
{
	const char *xml = "<root><child>Text</child></root>";
	xml_document_t *doc = xml_document_create();
	ASSERT_NOT_NULL(doc);

	int result = xml_document_parse(doc, xml, strlen(xml));
	ASSERT_EQUAL(0, result);

	xml_node_t *root = xml_document_root_element(doc);
	ASSERT_NOT_NULL(root);
	ASSERT_STR("root", xml_element_name(root));

	xml_node_t *child = xml_element_first_child(root, "child");
	ASSERT_NOT_NULL(child);
	ASSERT_STR("child", xml_element_name(child));

	const char *text = xml_element_text(child);
	ASSERT_NOT_NULL(text);
	ASSERT_STR("Text", text);

	xml_document_destroy(doc);
}

/**
 * @test Test parsing of element attributes
 *
 * Verifies that the parser correctly extracts element attributes.
 */
CTEST(xml_parser, parse_attributes)
{
	const char *xml = "<root id=\"123\" name=\"test\">Content</root>";
	xml_document_t *doc = xml_document_create();
	ASSERT_NOT_NULL(doc);

	int result = xml_document_parse(doc, xml, strlen(xml));
	ASSERT_EQUAL(0, result);

	xml_node_t *root = xml_document_root_element(doc);
	ASSERT_NOT_NULL(root);

	const char *id = xml_element_attribute(root, "id");
	ASSERT_NOT_NULL(id);
	ASSERT_STR("123", id);

	const char *name = xml_element_attribute(root, "name");
	ASSERT_NOT_NULL(name);
	ASSERT_STR("test", name);

	const char *missing = xml_element_attribute(root, "missing");
	ASSERT_NULL(missing);

	xml_document_destroy(doc);
}

/**
 * @test Test parsing of empty element
 *
 * Verifies that the parser correctly handles self-closing empty elements.
 */
CTEST(xml_parser, parse_empty_element)
{
	const char *xml = "<root><empty/></root>";
	xml_document_t *doc = xml_document_create();
	ASSERT_NOT_NULL(doc);

	int result = xml_document_parse(doc, xml, strlen(xml));
	ASSERT_EQUAL(0, result);

	xml_node_t *root = xml_document_root_element(doc);
	ASSERT_NOT_NULL(root);

	xml_node_t *empty = xml_element_first_child(root, "empty");
	ASSERT_NOT_NULL(empty);
	ASSERT_STR("empty", xml_element_name(empty));

	const char *text = xml_element_text(empty);
	ASSERT_NULL(text); /* Empty element has no text */

	xml_document_destroy(doc);
}

/**
 * @test Test parsing of XML with declaration
 *
 * Verifies that the parser skips XML declarations correctly.
 */
CTEST(xml_parser, parse_with_declaration)
{
	const char *xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><root>Data</root>";
	xml_document_t *doc = xml_document_create();
	ASSERT_NOT_NULL(doc);

	int result = xml_document_parse(doc, xml, strlen(xml));
	ASSERT_EQUAL(0, result);

	xml_node_t *root = xml_document_root_element(doc);
	ASSERT_NOT_NULL(root);
	ASSERT_STR("root", xml_element_name(root));

	xml_document_destroy(doc);
}

/**
 * @test Test parsing of XML with comments
 *
 * Verifies that the parser skips XML comments correctly.
 */
CTEST(xml_parser, parse_with_comments)
{
	const char *xml = "<!-- Comment --><root><!-- Another comment --><data>Text</data></root>";
	xml_document_t *doc = xml_document_create();
	ASSERT_NOT_NULL(doc);

	int result = xml_document_parse(doc, xml, strlen(xml));
	ASSERT_EQUAL(0, result);

	xml_node_t *root = xml_document_root_element(doc);
	ASSERT_NOT_NULL(root);

	xml_node_t *data = xml_element_first_child(root, "data");
	ASSERT_NOT_NULL(data);
	ASSERT_STR("Text", xml_element_text(data));

	xml_document_destroy(doc);
}

/**
 * @test Test parsing of multiple sibling elements
 *
 * Verifies that the parser correctly handles multiple children at the same level.
 */
CTEST(xml_parser, parse_multiple_siblings)
{
	const char *xml = "<root><first>A</first><second>B</second><third>C</third></root>";
	xml_document_t *doc = xml_document_create();
	ASSERT_NOT_NULL(doc);

	int result = xml_document_parse(doc, xml, strlen(xml));
	ASSERT_EQUAL(0, result);

	xml_node_t *root = xml_document_root_element(doc);
	ASSERT_NOT_NULL(root);

	xml_node_t *first = xml_element_first_child(root, "first");
	ASSERT_NOT_NULL(first);
	ASSERT_STR("A", xml_element_text(first));

	xml_node_t *second = xml_element_next_sibling(first, "second");
	ASSERT_NOT_NULL(second);
	ASSERT_STR("B", xml_element_text(second));

	xml_node_t *third = xml_element_next_sibling(second, "third");
	ASSERT_NOT_NULL(third);
	ASSERT_STR("C", xml_element_text(third));

	xml_node_t *none = xml_element_next_sibling(third, NULL);
	ASSERT_NULL(none);

	xml_document_destroy(doc);
}

/**
 * @test Test parsing of XMP-like structure
 *
 * Verifies that the parser can handle real-world XMP metadata structure.
 */
CTEST(xml_parser, parse_xmp_structure)
{
	const char *xml = "<?xml version=\"1.0\"?>"
	                  "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\" "
	                  "xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
	                  "<rdf:Description>"
	                  "<dc:creator>John Doe</dc:creator>"
	                  "<dc:rights>Copyright 2024</dc:rights>"
	                  "<dc:description>Test photo</dc:description>"
	                  "</rdf:Description>"
	                  "</rdf:RDF>";

	xml_document_t *doc = xml_document_create();
	ASSERT_NOT_NULL(doc);

	int result = xml_document_parse(doc, xml, strlen(xml));
	ASSERT_EQUAL(0, result);

	xml_node_t *root = xml_document_root_element(doc);
	ASSERT_NOT_NULL(root);

	/* Navigate to Description */
	xml_node_t *desc = xml_element_first_child(root, NULL);
	ASSERT_NOT_NULL(desc);

	/* Find creator */
	xml_node_t *creator = xml_element_first_child(desc, NULL);
	ASSERT_NOT_NULL(creator);

	/* Check creator value */
	const char *creator_text = xml_element_text(creator);
	ASSERT_NOT_NULL(creator_text);
	ASSERT_STR("John Doe", creator_text);

	xml_document_destroy(doc);
}

/**
 * @test Test parsing of namespaced elements
 *
 * Verifies that the parser handles namespace prefixes in element names.
 */
CTEST(xml_parser, parse_namespaced_elements)
{
	const char *xml = "<rdf:RDF><dc:creator>Author</dc:creator></rdf:RDF>";
	xml_document_t *doc = xml_document_create();
	ASSERT_NOT_NULL(doc);

	int result = xml_document_parse(doc, xml, strlen(xml));
	ASSERT_EQUAL(0, result);

	xml_node_t *root = xml_document_root_element(doc);
	ASSERT_NOT_NULL(root);
	ASSERT_STR("rdf:RDF", xml_element_name(root));

	xml_node_t *creator = xml_element_first_child(root, "dc:creator");
	ASSERT_NOT_NULL(creator);
	ASSERT_STR("Author", xml_element_text(creator));

	xml_document_destroy(doc);
}

/**
 * @test Test whitespace trimming in text content
 *
 * Verifies that the parser trims leading and trailing whitespace from text nodes.
 */
CTEST(xml_parser, parse_whitespace_trimming)
{
	const char *xml = "<root>   Trimmed Text   </root>";
	xml_document_t *doc = xml_document_create();
	ASSERT_NOT_NULL(doc);

	int result = xml_document_parse(doc, xml, strlen(xml));
	ASSERT_EQUAL(0, result);

	xml_node_t *root = xml_document_root_element(doc);
	ASSERT_NOT_NULL(root);

	const char *text = xml_element_text(root);
	ASSERT_NOT_NULL(text);
	ASSERT_STR("Trimmed Text", text);

	xml_document_destroy(doc);
}

/**
 * @test Test parsing with single quotes in attributes
 *
 * Verifies that the parser handles attributes with single quote delimiters.
 */
CTEST(xml_parser, parse_single_quote_attributes)
{
	const char *xml = "<root id='test' name='value'>Content</root>";
	xml_document_t *doc = xml_document_create();
	ASSERT_NOT_NULL(doc);

	int result = xml_document_parse(doc, xml, strlen(xml));
	ASSERT_EQUAL(0, result);

	xml_node_t *root = xml_document_root_element(doc);
	ASSERT_NOT_NULL(root);

	const char *id = xml_element_attribute(root, "id");
	ASSERT_NOT_NULL(id);
	ASSERT_STR("test", id);

	const char *name = xml_element_attribute(root, "name");
	ASSERT_NOT_NULL(name);
	ASSERT_STR("value", name);

	xml_document_destroy(doc);
}

/**
 * @test Test parsing error handling for malformed XML
 *
 * Verifies that the parser returns error code for malformed XML.
 */
CTEST(xml_parser, parse_malformed_xml)
{
	const char *xml = "<root><unclosed>";
	xml_document_t *doc = xml_document_create();
	ASSERT_NOT_NULL(doc);

	int result = xml_document_parse(doc, xml, strlen(xml));
	ASSERT_NOT_EQUAL(0, result);

	xml_document_destroy(doc);
}

/**
 * @test Test parsing error handling for mismatched tags
 *
 * Verifies that the parser detects mismatched opening and closing tags.
 */
CTEST(xml_parser, parse_mismatched_tags)
{
	const char *xml = "<root><child></wrong></root>";
	xml_document_t *doc = xml_document_create();
	ASSERT_NOT_NULL(doc);

	int result = xml_document_parse(doc, xml, strlen(xml));
	ASSERT_NOT_EQUAL(0, result);

	xml_document_destroy(doc);
}

/**
 * @test Test first_child with NULL name (any element)
 *
 * Verifies that passing NULL for element name returns first child element of any type.
 */
CTEST(xml_parser, first_child_any_element)
{
	const char *xml = "<root><first>A</first><second>B</second></root>";
	xml_document_t *doc = xml_document_create();
	ASSERT_NOT_NULL(doc);

	int result = xml_document_parse(doc, xml, strlen(xml));
	ASSERT_EQUAL(0, result);

	xml_node_t *root = xml_document_root_element(doc);
	ASSERT_NOT_NULL(root);

	xml_node_t *any_child = xml_element_first_child(root, NULL);
	ASSERT_NOT_NULL(any_child);
	ASSERT_STR("first", xml_element_name(any_child));

	xml_document_destroy(doc);
}

/**
 * @test Test deeply nested structure
 *
 * Verifies that the parser handles deeply nested XML structures.
 */
CTEST(xml_parser, parse_deeply_nested)
{
	const char *xml = "<a><b><c><d><e>Deep</e></d></c></b></a>";
	xml_document_t *doc = xml_document_create();
	ASSERT_NOT_NULL(doc);

	int result = xml_document_parse(doc, xml, strlen(xml));
	ASSERT_EQUAL(0, result);

	xml_node_t *a = xml_document_root_element(doc);
	ASSERT_NOT_NULL(a);

	xml_node_t *b = xml_element_first_child(a, "b");
	ASSERT_NOT_NULL(b);

	xml_node_t *c = xml_element_first_child(b, "c");
	ASSERT_NOT_NULL(c);

	xml_node_t *d = xml_element_first_child(c, "d");
	ASSERT_NOT_NULL(d);

	xml_node_t *e = xml_element_first_child(d, "e");
	ASSERT_NOT_NULL(e);

	ASSERT_STR("Deep", xml_element_text(e));

	xml_document_destroy(doc);
}

/**
 * @test Test multiple attributes
 *
 * Verifies that the parser handles elements with many attributes.
 */
CTEST(xml_parser, parse_many_attributes)
{
	const char *xml = "<root a=\"1\" b=\"2\" c=\"3\" d=\"4\" e=\"5\">Text</root>";
	xml_document_t *doc = xml_document_create();
	ASSERT_NOT_NULL(doc);

	int result = xml_document_parse(doc, xml, strlen(xml));
	ASSERT_EQUAL(0, result);

	xml_node_t *root = xml_document_root_element(doc);
	ASSERT_NOT_NULL(root);

	ASSERT_STR("1", xml_element_attribute(root, "a"));
	ASSERT_STR("2", xml_element_attribute(root, "b"));
	ASSERT_STR("3", xml_element_attribute(root, "c"));
	ASSERT_STR("4", xml_element_attribute(root, "d"));
	ASSERT_STR("5", xml_element_attribute(root, "e"));

	xml_document_destroy(doc);
}
