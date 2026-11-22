/**
 * @file test_cov_p3_leak.c
 * @brief Coverage phase 3: leak checks with a live-block counter
 *
 * Linked with -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=strdup,--wrap=free (Linux only): every
 * allocation made by the test or the statically linked img2 archive goes through the counters below, so a block
 * that an operation leaves behind shows up as a non-zero difference of s_live (no heap introspection needed).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <img2/img2.h>

#include "../../libimg2/metadata/exif_reader.h"
#include "../ctest.h"

void *__real_malloc(size_t size);
void *__real_calloc(size_t nmemb, size_t size);
void *__real_realloc(void *ptr, size_t size);
char *__real_strdup(const char *s);
void __real_free(void *ptr);

void *__wrap_malloc(size_t size);
void *__wrap_calloc(size_t nmemb, size_t size);
void *__wrap_realloc(void *ptr, size_t size);
char *__wrap_strdup(const char *s);
void __wrap_free(void *ptr);

static long s_live; /* blocks allocated through the wrappers and not yet freed */

void *__wrap_malloc(size_t size)
{
	void *p = __real_malloc(size);
	s_live += p != NULL;
	return p;
}

void *__wrap_calloc(size_t nmemb, size_t size)
{
	void *p = __real_calloc(nmemb, size);
	s_live += p != NULL;
	return p;
}

void *__wrap_realloc(void *ptr, size_t size)
{
	void *p = __real_realloc(ptr, size);
	if (ptr == NULL && p != NULL) {
		s_live++;
	} else if (ptr != NULL && size == 0 && p == NULL) {
		s_live--; /* glibc: realloc(p, 0) frees p */
	}
	return p;
}

char *__wrap_strdup(const char *s)
{
	char *p = __real_strdup(s);
	s_live += p != NULL;
	return p;
}

void __wrap_free(void *ptr)
{
	s_live -= ptr != NULL;
	__real_free(ptr);
}

#define XMP_HEAD "<rdf:RDF><rdf:Description>"
#define XMP_TAIL "</rdf:Description></rdf:RDF>"
#define XMP_SUBJECT(a, b) "<dc:subject><rdf:Bag><rdf:li>" a "</rdf:li><rdf:li>" b "</rdf:li></rdf:Bag></dc:subject>"

/* HEIF/AVIF stills parse every XMP item into the same struct: the second packet's keywords replace the first's */
CTEST(xmp_keywords, second_packet_replaces_keywords_without_leaking)
{
	static const char first[] = XMP_HEAD XMP_SUBJECT("alpha", "beta") XMP_TAIL;
	static const char second[] = XMP_HEAD XMP_SUBJECT("gamma", "delta") XMP_TAIL;
	img2_xmp_t xmp;

	long before = s_live;
	img2_xmp_init(&xmp);
	ASSERT_EQUAL(IMG2_OK, img2_xmp_parse_xml(&xmp, first, strlen(first)));
	ASSERT_EQUAL(IMG2_OK, img2_xmp_parse_xml(&xmp, second, strlen(second)));
	ASSERT_TRUE(xmp.has_keywords);
	ASSERT_STR("gamma, delta", xmp.keywords);
	img2_xmp_clear(&xmp);
	ASSERT_EQUAL(0, s_live - before);
}

/* two dc:subject elements in one packet: the last one wins, the first string is freed */
CTEST(xmp_keywords, repeated_subject_in_one_packet_does_not_leak)
{
	static const char doc[] = XMP_HEAD XMP_SUBJECT("one", "two") XMP_SUBJECT("three", "four") XMP_TAIL;
	img2_xmp_t xmp;

	long before = s_live;
	img2_xmp_init(&xmp);
	ASSERT_EQUAL(IMG2_OK, img2_xmp_parse_xml(&xmp, doc, strlen(doc)));
	ASSERT_STR("three, four", xmp.keywords);
	img2_xmp_clear(&xmp);
	ASSERT_EQUAL(0, s_live - before);
}
