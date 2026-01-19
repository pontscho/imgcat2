/**
 * @file xml_parser.h
 * @brief Pure C XML DOM parser (TinyXML2 port)
 *
 * Lightweight XML parser for XMP metadata extraction.
 * Uses arena allocator for efficient memory management.
 */

#ifndef XML_PARSER_H
#define XML_PARSER_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief XML node types
 */
typedef enum {
	XML_NODE_ELEMENT, /**< Element node (e.g., <tag>) */
	XML_NODE_TEXT, /**< Text content node */
	XML_NODE_COMMENT, /**< Comment node */
	XML_NODE_DECLARATION /**< XML declaration (e.g., <?xml?>) */
} xml_node_type_t;

/* Forward declarations */
typedef struct xml_node_t xml_node_t;
typedef struct xml_document_t xml_document_t;

/**
 * @brief XML node structure
 *
 * Represents a node in the XML DOM tree with virtual function table
 * for polymorphic operations (similar to C++ virtual functions).
 */
struct xml_node_t {
	xml_node_type_t type; /**< Node type */
	xml_node_t *parent; /**< Parent node (NULL for root) */
	xml_node_t *first_child; /**< First child node */
	xml_node_t *next_sibling; /**< Next sibling node */

	/* Virtual function table (vtable pattern for polymorphism) */
	const char *(*get_value)(const xml_node_t *node);
	xml_node_t *(*first_child_element)(const xml_node_t *node, const char *name);
	const char *(*get_attribute)(const xml_node_t *node, const char *name);
};

/**
 * @brief Create a new XML document
 *
 * @return Pointer to newly created document, or NULL on failure
 */
xml_document_t *xml_document_create(void);

/**
 * @brief Destroy an XML document and free all memory
 *
 * Uses arena allocator, so this is a single free operation.
 *
 * @param doc Document to destroy (NULL-safe)
 */
void xml_document_destroy(xml_document_t *doc);

/**
 * @brief Parse XML data into the document
 *
 * @param doc Document to parse into
 * @param xml_data XML string data (need not be null-terminated)
 * @param len Length of xml_data in bytes
 * @return 0 on success, -1 on parse error
 */
int xml_document_parse(xml_document_t *doc, const char *xml_data, size_t len);

/**
 * @brief Get the root element of the document
 *
 * @param doc Document
 * @return Root element node, or NULL if document is empty
 */
xml_node_t *xml_document_root_element(xml_document_t *doc);

/**
 * @brief Find first child element with given name
 *
 * @param elem Parent element
 * @param name Element name to search for (NULL = any element)
 * @return First matching child element, or NULL if not found
 */
xml_node_t *xml_element_first_child(const xml_node_t *elem, const char *name);

/**
 * @brief Get attribute value from element
 *
 * @param elem Element node
 * @param attr_name Attribute name
 * @return Attribute value string, or NULL if not found
 */
const char *xml_element_attribute(const xml_node_t *elem, const char *attr_name);

/**
 * @brief Get text content of element
 *
 * Returns the concatenated text of all text node children.
 *
 * @param elem Element node
 * @return Text content, or NULL if element has no text
 */
const char *xml_element_text(const xml_node_t *elem);

/**
 * @brief Get element tag name
 *
 * @param elem Element node
 * @return Tag name string, or NULL if not an element
 */
const char *xml_element_name(const xml_node_t *elem);

/**
 * @brief Find next sibling element with given name
 *
 * @param elem Current element
 * @param name Element name to search for (NULL = any element)
 * @return Next matching sibling element, or NULL if not found
 */
xml_node_t *xml_element_next_sibling(const xml_node_t *elem, const char *name);

#endif /* XML_PARSER_H */
