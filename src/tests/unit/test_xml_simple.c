/**
 * @file test_xml_simple.c
 * @brief Simple XML parser debug test
 */

#include <stdio.h>
#include <string.h>

#include "../../imgcat2/metadata/xml_parser.h"

int main(void)
{
	const char *xml = "<root>Hello</root>";
	printf("Parsing: %s\n", xml);

	xml_document_t *doc = xml_document_create();
	if (!doc) {
		printf("ERROR: xml_document_create failed\n");
		return 1;
	}

	int result = xml_document_parse(doc, xml, strlen(xml));
	printf("Parse result: %d (0=success, -1=failure)\n", result);

	if (result == 0) {
		xml_node_t *root = xml_document_root_element(doc);
		if (root) {
			const char *name = xml_element_name(root);
			const char *text = xml_element_text(root);
			printf("Root element: %s\n", name ? name : "(null)");
			printf("Root text: %s\n", text ? text : "(null)");
		} else {
			printf("ERROR: No root element\n");
		}
	}

	xml_document_destroy(doc);
	return result;
}
