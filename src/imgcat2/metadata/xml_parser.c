/**
 * @file xml_parser.c
 * @brief Pure C XML DOM parser implementation (TinyXML2 port)
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xml_parser.h"

/* ========== Arena Allocator ========== */

#define ARENA_BLOCK_SIZE (64 * 1024) /* 64KB blocks */

typedef struct arena_block_t {
	struct arena_block_t *next;
	size_t used;
	size_t size;
	char data[]; /* Flexible array member */
} arena_block_t;

typedef struct {
	arena_block_t *first_block;
	arena_block_t *current_block;
} arena_t;

static arena_t *arena_create(void)
{
	arena_t *arena = malloc(sizeof(arena_t));
	if (!arena) {
		return NULL;
	}

	arena->first_block = malloc(sizeof(arena_block_t) + ARENA_BLOCK_SIZE);
	if (!arena->first_block) {
		free(arena);
		return NULL;
	}

	arena->first_block->next = NULL;
	arena->first_block->used = 0;
	arena->first_block->size = ARENA_BLOCK_SIZE;
	arena->current_block = arena->first_block;

	return arena;
}

static void arena_destroy(arena_t *arena)
{
	if (!arena) {
		return;
	}

	arena_block_t *block = arena->first_block;
	while (block) {
		arena_block_t *next = block->next;
		free(block);
		block = next;
	}
	free(arena);
}

static void *arena_alloc(arena_t *arena, size_t size)
{
	if (!arena) {
		return NULL;
	}

	/* Align to 8-byte boundary */
	size = (size + 7) & ~7;

	/* Check if current block has enough space */
	arena_block_t *block = arena->current_block;
	if (block->used + size <= block->size) {
		void *ptr = block->data + block->used;
		block->used += size;
		return ptr;
	}

	/* Allocate new block */
	size_t block_size = size > ARENA_BLOCK_SIZE ? size : ARENA_BLOCK_SIZE;
	arena_block_t *new_block = malloc(sizeof(arena_block_t) + block_size);
	if (!new_block) {
		return NULL;
	}

	new_block->next = NULL;
	new_block->used = size;
	new_block->size = block_size;

	block->next = new_block;
	arena->current_block = new_block;

	return new_block->data;
}

static char *arena_strdup(arena_t *arena, const char *str, size_t len)
{
	if (!arena || !str) {
		return NULL;
	}

	char *copy = arena_alloc(arena, len + 1);
	if (!copy) {
		return NULL;
	}

	memcpy(copy, str, len);
	copy[len] = '\0';
	return copy;
}

/* ========== XML Node Structures ========== */

typedef struct {
	char *name;
	char *value;
} xml_attribute_t;

typedef struct xml_element_node_t {
	xml_node_t base; /* Inherit from xml_node_t */
	char *name;
	xml_attribute_t *attributes;
	int attr_count;
	int attr_capacity;
} xml_element_node_t;

typedef struct xml_text_node_t {
	xml_node_t base;
	char *text;
} xml_text_node_t;

typedef struct xml_comment_node_t {
	xml_node_t base;
	char *comment;
} xml_comment_node_t;

typedef struct xml_declaration_node_t {
	xml_node_t base;
	char *version;
	char *encoding;
} xml_declaration_node_t;

/* ========== XML Document Structure ========== */

struct xml_document_t {
	arena_t *arena;
	xml_node_t *root;
	char *error_msg;
};

/* ========== XML Tokenizer ========== */

typedef enum {
	TOKEN_NONE,
	TOKEN_ELEMENT_OPEN, /* < */
	TOKEN_ELEMENT_CLOSE, /* > */
	TOKEN_ELEMENT_END, /* </ */
	TOKEN_ELEMENT_EMPTY, /* /> */
	TOKEN_DECLARATION_OPEN, /* <? */
	TOKEN_DECLARATION_CLOSE, /* ?> */
	TOKEN_COMMENT_OPEN, /* <!-- */
	TOKEN_COMMENT_CLOSE, /* --> */
	TOKEN_TEXT,
	TOKEN_NAME,
	TOKEN_ATTRIBUTE,
	TOKEN_EOF,
	TOKEN_ERROR
} token_type_t;

typedef struct {
	const char *xml;
	size_t len;
	size_t pos;
	token_type_t current_token;
	const char *token_start;
	size_t token_len;
} xml_tokenizer_t;

static void tokenizer_init(xml_tokenizer_t *tok, const char *xml, size_t len)
{
	tok->xml = xml;
	tok->len = len;
	tok->pos = 0;
	tok->current_token = TOKEN_NONE;
	tok->token_start = NULL;
	tok->token_len = 0;
}

static void skip_whitespace(xml_tokenizer_t *tok)
{
	while (tok->pos < tok->len && isspace((unsigned char)tok->xml[tok->pos])) {
		tok->pos++;
	}
}

static bool match_string(xml_tokenizer_t *tok, const char *str)
{
	size_t str_len = strlen(str);
	if (tok->pos + str_len > tok->len) {
		return false;
	}
	return memcmp(tok->xml + tok->pos, str, str_len) == 0;
}

static token_type_t next_token(xml_tokenizer_t *tok)
{
	skip_whitespace(tok);

	if (tok->pos >= tok->len) {
		tok->current_token = TOKEN_EOF;
		return TOKEN_EOF;
	}

	const char *p = tok->xml + tok->pos;

	/* Check for markup */
	if (*p == '<') {
		if (match_string(tok, "<!--")) {
			tok->pos += 4;
			tok->current_token = TOKEN_COMMENT_OPEN;
			return TOKEN_COMMENT_OPEN;

		} else if (match_string(tok, "<?")) {
			tok->pos += 2;
			tok->current_token = TOKEN_DECLARATION_OPEN;
			return TOKEN_DECLARATION_OPEN;

		} else if (match_string(tok, "</")) {
			tok->pos += 2;
			tok->current_token = TOKEN_ELEMENT_END;
			return TOKEN_ELEMENT_END;

		} else {
			tok->pos++;
			tok->current_token = TOKEN_ELEMENT_OPEN;
			return TOKEN_ELEMENT_OPEN;
		}
	}

	/* Check for closing markers */
	if (match_string(tok, "/>")) {
		tok->pos += 2;
		tok->current_token = TOKEN_ELEMENT_EMPTY;
		return TOKEN_ELEMENT_EMPTY;

	} else if (match_string(tok, "?>")) {
		tok->pos += 2;
		tok->current_token = TOKEN_DECLARATION_CLOSE;
		return TOKEN_DECLARATION_CLOSE;

	} else if (match_string(tok, "-->")) {
		tok->pos += 3;
		tok->current_token = TOKEN_COMMENT_CLOSE;
		return TOKEN_COMMENT_CLOSE;

	} else if (*p == '>') {
		tok->pos++;
		tok->current_token = TOKEN_ELEMENT_CLOSE;
		return TOKEN_ELEMENT_CLOSE;
	}

	/* Read name (element/attribute name) - single word with name characters */
	if (isalpha((unsigned char)*p) || *p == '_' || *p == ':') {
		tok->token_start = p;
		tok->pos++;
		while (tok->pos < tok->len) {
			char c = tok->xml[tok->pos];
			if (isalnum((unsigned char)c) || c == '_' || c == ':' || c == '-' || c == '.') {
				tok->pos++;
			} else {
				break;
			}
		}
		tok->token_len = tok->xml + tok->pos - tok->token_start;
		tok->current_token = TOKEN_NAME;
		return TOKEN_NAME;
	}

	/* Read text content (anything else until '<') */
	if (*p != '<') {
		tok->token_start = p;
		while (tok->pos < tok->len && tok->xml[tok->pos] != '<') {
			tok->pos++;
		}
		tok->token_len = tok->xml + tok->pos - tok->token_start;
		tok->current_token = TOKEN_TEXT;
		return TOKEN_TEXT;
	}

	tok->current_token = TOKEN_ERROR;
	return TOKEN_ERROR;
}

static char *read_until(xml_tokenizer_t *tok, const char *delimiter, arena_t *arena)
{
	const char *start = tok->xml + tok->pos;
	size_t delim_len = strlen(delimiter);

	while (tok->pos + delim_len <= tok->len) {
		if (memcmp(tok->xml + tok->pos, delimiter, delim_len) == 0) {
			size_t len = tok->xml + tok->pos - start;
			char *result = arena_strdup(arena, start, len);
			return result;
		}
		tok->pos++;
	}

	return NULL; /* Delimiter not found */
}

static char *read_attribute_value(xml_tokenizer_t *tok, arena_t *arena)
{
	skip_whitespace(tok);

	if (tok->pos >= tok->len || tok->xml[tok->pos] != '=') {
		return NULL;
	}
	tok->pos++;

	skip_whitespace(tok);

	if (tok->pos >= tok->len) {
		return NULL;
	}

	char quote = tok->xml[tok->pos];
	if (quote != '"' && quote != '\'') {
		return NULL;
	}
	tok->pos++;

	const char *start = tok->xml + tok->pos;
	while (tok->pos < tok->len && tok->xml[tok->pos] != quote) {
		tok->pos++;
	}

	if (tok->pos >= tok->len) {
		return NULL; /* Unterminated string */
	}

	size_t len = tok->xml + tok->pos - start;
	tok->pos++; /* Skip closing quote */

	return arena_strdup(arena, start, len);
}

/* ========== Virtual Function Implementations ========== */

static const char *element_get_value(const xml_node_t *node)
{
	if (node->type != XML_NODE_ELEMENT) {
		return NULL;
	}
	xml_element_node_t *elem = (xml_element_node_t *)node;
	return elem->name;
}

static xml_node_t *element_first_child_element(const xml_node_t *node, const char *name)
{
	if (!node) {
		return NULL;
	}

	xml_node_t *child = node->first_child;
	while (child) {
		if (child->type == XML_NODE_ELEMENT) {
			if (!name) {
				return child;
			}
			xml_element_node_t *elem = (xml_element_node_t *)child;
			if (strcmp(elem->name, name) == 0) {
				return child;
			}
		}
		child = child->next_sibling;
	}
	return NULL;
}

static const char *element_get_attribute(const xml_node_t *node, const char *name)
{
	if (!node || node->type != XML_NODE_ELEMENT || !name) {
		return NULL;
	}

	xml_element_node_t *elem = (xml_element_node_t *)node;
	for (int i = 0; i < elem->attr_count; i++) {
		if (strcmp(elem->attributes[i].name, name) == 0) {
			return elem->attributes[i].value;
		}
	}
	return NULL;
}

static const char *text_get_value(const xml_node_t *node)
{
	if (node->type != XML_NODE_TEXT) {
		return NULL;
	}
	xml_text_node_t *text = (xml_text_node_t *)node;
	return text->text;
}

static const char *comment_get_value(const xml_node_t *node)
{
	if (node->type != XML_NODE_COMMENT) {
		return NULL;
	}
	xml_comment_node_t *comment = (xml_comment_node_t *)node;
	return comment->comment;
}

/* ========== Node Creation ========== */

static xml_node_t *create_element_node(arena_t *arena, const char *name, size_t name_len)
{
	xml_element_node_t *elem = arena_alloc(arena, sizeof(xml_element_node_t));
	if (!elem) {
		return NULL;
	}

	memset(elem, 0, sizeof(xml_element_node_t));
	elem->base.type = XML_NODE_ELEMENT;
	elem->base.get_value = element_get_value;
	elem->base.first_child_element = element_first_child_element;
	elem->base.get_attribute = element_get_attribute;
	elem->name = arena_strdup(arena, name, name_len);
	elem->attr_capacity = 4;
	elem->attributes = arena_alloc(arena, sizeof(xml_attribute_t) * elem->attr_capacity);

	return (xml_node_t *)elem;
}

static xml_node_t *create_text_node(arena_t *arena, const char *text, size_t text_len)
{
	xml_text_node_t *node = arena_alloc(arena, sizeof(xml_text_node_t));
	if (!node) {
		return NULL;
	}

	memset(node, 0, sizeof(xml_text_node_t));
	node->base.type = XML_NODE_TEXT;
	node->base.get_value = text_get_value;
	node->text = arena_strdup(arena, text, text_len);

	return (xml_node_t *)node;
}

static xml_node_t *create_comment_node(arena_t *arena, const char *comment, size_t comment_len)
{
	xml_comment_node_t *node = arena_alloc(arena, sizeof(xml_comment_node_t));
	if (!node) {
		return NULL;
	}

	memset(node, 0, sizeof(xml_comment_node_t));
	node->base.type = XML_NODE_COMMENT;
	node->base.get_value = comment_get_value;
	node->comment = arena_strdup(arena, comment, comment_len);

	return (xml_node_t *)node;
}

static void add_child_node(xml_node_t *parent, xml_node_t *child)
{
	if (!parent || !child) {
		return;
	}

	child->parent = parent;

	if (!parent->first_child) {
		parent->first_child = child;
	} else {
		xml_node_t *last = parent->first_child;
		while (last->next_sibling) {
			last = last->next_sibling;
		}
		last->next_sibling = child;
	}
}

static int add_attribute(xml_element_node_t *elem, const char *name, size_t name_len, char *value, arena_t *arena)
{
	if (!elem || !name || !value) {
		return -1;
	}

	if (elem->attr_count >= elem->attr_capacity) {
		/* Need to grow attributes array */
		int new_capacity = elem->attr_capacity * 2;
		xml_attribute_t *new_attrs = arena_alloc(arena, sizeof(xml_attribute_t) * new_capacity);
		if (!new_attrs) {
			return -1;
		}
		memcpy(new_attrs, elem->attributes, sizeof(xml_attribute_t) * elem->attr_count);
		elem->attributes = new_attrs;
		elem->attr_capacity = new_capacity;
	}

	elem->attributes[elem->attr_count].name = arena_strdup(arena, name, name_len);
	elem->attributes[elem->attr_count].value = value; /* Already arena-allocated */
	elem->attr_count++;

	return 0;
}

/* ========== DOM Tree Builder ========== */

static int parse_element(xml_tokenizer_t *tok, xml_document_t *doc, xml_node_t *parent);

static int parse_content(xml_tokenizer_t *tok, xml_document_t *doc, xml_node_t *parent)
{
	while (1) {
		token_type_t token = next_token(tok);

		if (token == TOKEN_EOF || token == TOKEN_ELEMENT_END) {
			return 0;
		}

		if (token == TOKEN_TEXT || token == TOKEN_NAME) {
			/* Accumulate all consecutive text/name tokens */
			const char *text_start = tok->token_start;
			const char *text_end = tok->token_start + tok->token_len;

			/* Keep consuming text/name tokens */
			while (1) {
				size_t saved_pos = tok->pos;
				token_type_t next_tok = next_token(tok);
				if (next_tok == TOKEN_TEXT || next_tok == TOKEN_NAME) {
					/* Extend text range to include this token */
					text_end = tok->token_start + tok->token_len;

				} else {
					/* Not text, rewind */
					tok->pos = saved_pos;
					tok->current_token = token; /* Restore previous token */
					break;
				}
			}

			size_t text_len = text_end - text_start;

			/* Trim leading whitespace */
			while (text_len > 0 && isspace((unsigned char)*text_start)) {
				text_start++;
				text_len--;
			}

			/* Trim trailing whitespace */
			while (text_len > 0 && isspace((unsigned char)text_start[text_len - 1])) {
				text_len--;
			}

			if (text_len > 0) {
				xml_node_t *text_node = create_text_node(doc->arena, text_start, text_len);
				if (!text_node) {
					return -1;
				}
				add_child_node(parent, text_node);
			}

		} else if (token == TOKEN_ELEMENT_OPEN) {
			if (parse_element(tok, doc, parent) != 0) {
				return -1;
			}

		} else if (token == TOKEN_COMMENT_OPEN) {
			char *comment_text = read_until(tok, "-->", doc->arena);
			if (!comment_text) {
				return -1;
			}
			tok->pos += 3; /* Skip --> */

			xml_node_t *comment_node = create_comment_node(doc->arena, comment_text, strlen(comment_text));
			if (!comment_node) {
				return -1;
			}
			add_child_node(parent, comment_node);

		} else {
			return -1; /* Unexpected token */
		}
	}
}

static int parse_element(xml_tokenizer_t *tok, xml_document_t *doc, xml_node_t *parent)
{
	/* Expect element name */
	token_type_t token = next_token(tok);
	if (token != TOKEN_NAME) {
		return -1;
	}

	xml_node_t *elem = create_element_node(doc->arena, tok->token_start, tok->token_len);
	if (!elem) {
		return -1;
	}

	char *elem_name = ((xml_element_node_t *)elem)->name;

	/* Parse attributes */
	while (1) {
		skip_whitespace(tok);

		if (tok->pos >= tok->len) {
			return -1;
		}

		/* Check for element close */
		if (tok->xml[tok->pos] == '>') {
			tok->pos++;
			break;
		}

		if (tok->xml[tok->pos] == '/' && tok->pos + 1 < tok->len && tok->xml[tok->pos + 1] == '>') {
			tok->pos += 2;
			/* Empty element, add to parent and return */
			add_child_node(parent, elem);
			return 0;
		}

		/* Read attribute name */
		token = next_token(tok);
		if (token != TOKEN_NAME) {
			return -1;
		}

		const char *attr_name = tok->token_start;
		size_t attr_name_len = tok->token_len;

		/* Read attribute value */
		char *attr_value = read_attribute_value(tok, doc->arena);
		if (!attr_value) {
			return -1;

		} else if (add_attribute((xml_element_node_t *)elem, attr_name, attr_name_len, attr_value, doc->arena) != 0) {
			return -1;
		}
	}

	/* Add element to parent */
	add_child_node(parent, elem);

	/* Parse element content */
	if (parse_content(tok, doc, elem) != 0) {
		return -1;
	}

	/* Expect closing tag name (parse_content already consumed TOKEN_ELEMENT_END) */
	token = next_token(tok);
	if (token != TOKEN_NAME) {
		return -1;
	}

	/* Verify closing tag matches opening tag */
	if (tok->token_len != strlen(elem_name) || memcmp(tok->token_start, elem_name, tok->token_len) != 0) {
		return -1;
	}

	skip_whitespace(tok);
	if (tok->pos >= tok->len || tok->xml[tok->pos] != '>') {
		return -1;
	}
	tok->pos++;

	return 0;
}

/* ========== Public API Implementation ========== */

xml_document_t *xml_document_create(void)
{
	xml_document_t *doc = malloc(sizeof(xml_document_t));
	if (!doc) {
		return NULL;
	}

	doc->arena = arena_create();
	if (!doc->arena) {
		free(doc);
		return NULL;
	}

	doc->root = NULL;
	doc->error_msg = NULL;

	return doc;
}

void xml_document_destroy(xml_document_t *doc)
{
	if (!doc) {
		return;
	}

	arena_destroy(doc->arena);
	free(doc);
}

int xml_document_parse(xml_document_t *doc, const char *xml_data, size_t len)
{
	if (!doc || !xml_data) {
		return -1;
	}

	xml_tokenizer_t tok;
	tokenizer_init(&tok, xml_data, len);

	/* Skip XML declaration if present */
	token_type_t token = next_token(&tok);
	if (token == TOKEN_DECLARATION_OPEN) {
		read_until(&tok, "?>", doc->arena);
		tok.pos += 2;
		token = next_token(&tok);
	}

	/* Skip comments */
	while (token == TOKEN_COMMENT_OPEN) {
		read_until(&tok, "-->", doc->arena);
		tok.pos += 3;
		token = next_token(&tok);
	}

	/* Parse root element */
	if (token == TOKEN_ELEMENT_OPEN) {
		/* Create temporary root to hold the actual root element */
		xml_node_t temp_root;
		memset(&temp_root, 0, sizeof(xml_node_t));

		if (parse_element(&tok, doc, &temp_root) != 0) {
			return -1;
		}

		doc->root = temp_root.first_child;

	} else {
		return -1;
	}

	return 0;
}

xml_node_t *xml_document_root_element(xml_document_t *doc)
{
	if (!doc) {
		return NULL;
	}
	return doc->root;
}

xml_node_t *xml_element_first_child(const xml_node_t *elem, const char *name)
{
	if (!elem || elem->type != XML_NODE_ELEMENT) {
		return NULL;
	}
	return element_first_child_element(elem, name);
}

const char *xml_element_attribute(const xml_node_t *elem, const char *attr_name)
{
	if (!elem || elem->type != XML_NODE_ELEMENT || !attr_name) {
		return NULL;
	}
	return element_get_attribute(elem, attr_name);
}

const char *xml_element_text(const xml_node_t *elem)
{
	if (!elem || elem->type != XML_NODE_ELEMENT) {
		return NULL;
	}

	/* Return first text child */
	xml_node_t *child = elem->first_child;
	while (child) {
		if (child->type == XML_NODE_TEXT) {
			return text_get_value(child);
		}
		child = child->next_sibling;
	}

	return NULL;
}

const char *xml_element_name(const xml_node_t *elem)
{
	if (!elem || elem->type != XML_NODE_ELEMENT) {
		return NULL;
	}
	xml_element_node_t *element = (xml_element_node_t *)elem;
	return element->name;
}

xml_node_t *xml_element_next_sibling(const xml_node_t *elem, const char *name)
{
	if (!elem) {
		return NULL;
	}

	xml_node_t *sibling = elem->next_sibling;
	while (sibling) {
		if (sibling->type == XML_NODE_ELEMENT) {
			if (!name) {
				return sibling;
			}
			xml_element_node_t *elem_sibling = (xml_element_node_t *)sibling;
			if (strcmp(elem_sibling->name, name) == 0) {
				return sibling;
			}
		}
		sibling = sibling->next_sibling;
	}
	return NULL;
}
