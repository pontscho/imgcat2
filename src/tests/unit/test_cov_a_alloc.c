/**
 * @file test_cov_a_alloc.c
 * @brief Lane A allocation failures (alloc_fail.h): XML arena blocks, XMP, font scan / load / face
 *
 * Linked with -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=strdup (Linux only). The XML documents
 * are shaped so that a specific arena allocation is the first one in a fresh 64 KiB block (an allocation of
 * at least a block gets a block of its own, so the next allocation always needs a new block); failing that
 * block's malloc() fails exactly that arena allocation.
 */

#define _GNU_SOURCE

#include <sys/stat.h>

#include <ftw.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <img2/img2.h>

#include "../../libimg2/metadata/exif_reader.h"
#include "../../libimg2/metadata/xml_parser.h"
#include "../../libimg2/text/font_manager.h"
#include "../ctest.h"
#include "../test_tmpdir.h"
#include "alloc_fail.h"

#define AF_MAX_RUNS 4000

/* checked after the loops (ASSERT must not fire while armed) */
static int s_bad;

/* ===== XML arena ===== */

static void op_xml(void *ctx)
{
	const char *xml = ctx;
	img2_xml_document_t *doc = img2_xml_document_create();
	if (doc == NULL) {
		return;
	}
	if (img2_xml_document_parse(doc, xml, strlen(xml)) == 0 && img2_xml_document_root_element(doc) == NULL) {
		s_bad++;
	}
	img2_xml_document_destroy(doc);
}

/* prefix + n x fill + suffix, malloc'd before arming */
static char *make_doc(const char *prefix, char fill, size_t n, const char *suffix)
{
	size_t pl = strlen(prefix);
	size_t sl = strlen(suffix);
	char *s = malloc(pl + n + sl + 1);
	if (s != NULL) {
		memcpy(s, prefix, pl);
		memset(s + pl, fill, n);
		memcpy(s + pl + n, suffix, sl + 1);
	}
	return s;
}

CTEST(alloc_a, xml_arena_block_boundaries)
{
	char *docs[5];
	/* element node: a >64 KiB text run fills its own block, the next element's node needs a new one */
	docs[0] = make_doc("<r>", 't', 70000, "<k/></r>");
	/* element name: root (node 80 + name 8 + attributes 64) and the text node (64) fill 216 bytes of block 1;
	 * the 65456-byte text goes to a new block and leaves exactly 80 bytes: <k>'s node fits, its name does not */
	docs[1] = make_doc("<r>", 't', 65455, "<k/></r>");
	/* attribute array: a >64 KiB element name fills its own block */
	docs[2] = make_doc("<r><", 'n', 70000, "/></r>");
	/* comment node and the following text node: each comes right after a >64 KiB comment copy */
	docs[3] = make_doc("<r><!--", 'c', 70000, "-->t</r>");
	/* attribute array growth (5th attribute): right after a >64 KiB attribute value */
	docs[4] = make_doc("<r a='1' b='2' c='3' d='4' e='", 'v', 70000, "'/>");
	for (int i = 0; i < 5; i++) {
		ASSERT_NOT_NULL(docs[i]);
		ASSERT_TRUE(af_for_each_failure(op_xml, docs[i], AF_MAX_RUNS) >= 4);
		ASSERT_EQUAL(0, s_bad);
		free(docs[i]);
	}
}

/* ===== XMP ===== */

static void op_xmp(void *ctx)
{
	const char *xml = ctx;
	img2_xmp_t xmp;
	img2_xmp_init(&xmp);
	img2_status_t st = img2_xmp_parse_xml(&xmp, xml, strlen(xml));
	if (st != IMG2_OK && st != IMG2_E_NOMEM && st != IMG2_E_FORMAT) {
		s_bad++;
	}
	if (xmp.has_keywords != (xmp.keywords != NULL)) {
		s_bad++;
	}
	img2_xmp_clear(&xmp);
}

CTEST(alloc_a, xmp_document_and_keywords)
{
	static char xml[] = "<x:xmpmeta><rdf:Description><dc:subject><rdf:Bag><rdf:li>a</rdf:li><rdf:li>b</rdf:li>"
	                    "</rdf:Bag></dc:subject></rdf:Description></x:xmpmeta>";
	ASSERT_TRUE(af_for_each_failure(op_xmp, xml, AF_MAX_RUNS) >= 4);
	ASSERT_EQUAL(0, s_bad);
}

/* ===== fonts ===== */

static void be16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

static void be32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

/* minimal sfnt (cmap/head/hhea/hmtx/loca/glyf, no name table): stbtt_InitFont() accepts it */
static size_t build_ttf(uint8_t *out)
{
	static const char *const tags[6] = { "cmap", "head", "hhea", "hmtx", "loca", "glyf" };
	const size_t lens[6] = { 12 + 262, 54, 36, 4, 4, 4 };
	memset(out, 0, 1024);
	be32(out, 0x00010000);
	be16(out + 4, 6);
	size_t off = 12 + 16 * 6;
	for (int i = 0; i < 6; i++) {
		uint8_t *d = out + 12 + 16 * i;
		memcpy(d, tags[i], 4);
		be32(d + 8, (uint32_t)off);
		be32(d + 12, (uint32_t)lens[i]);
		if (i == 0) {
			be16(out + off + 2, 1);
			be16(out + off + 4, 3);
			be16(out + off + 6, 1);
			be32(out + off + 8, 12);
			be16(out + off + 14, 262);
		}
		off = (off + lens[i] + 3) & ~(size_t)3;
	}
	return off;
}

static char s_home[512];

static int rm_entry(const char *path, const struct stat *sb, int flag, struct FTW *ftw)
{
	(void)sb;
	(void)flag;
	(void)ftw;
	remove(path);
	return 0;
}

static void home_remove(void)
{
	if (s_home[0] != '\0') {
		nftw(s_home, rm_entry, 16, FTW_DEPTH | FTW_PHYS);
	}
}

/* $HOME/.fonts/NoName.ttf (family from the file name) */
static void font_home(void)
{
	snprintf(s_home, sizeof(s_home), "%s/cov_a_alloc_%ld", test_tmpdir(), (long)getpid());
	mkdir(s_home, 0755);
	char p[1024];
	snprintf(p, sizeof(p), "%s/.fonts", s_home);
	mkdir(p, 0755);
	snprintf(p, sizeof(p), "%s/.fonts/NoName.ttf", s_home);
	uint8_t ttf[1024];
	size_t n = build_ttf(ttf);
	FILE *f = fopen(p, "wb");
	if (f != NULL) {
		fwrite(ttf, 1, n, f);
		fclose(f);
	}
	setenv("HOME", s_home, 1);
	atexit(home_remove);
}

static int count_font(const img2_font_info_t *info, void *user)
{
	(void)info;
	(*(int *)user)++;
	return 0;
}

/* scan only: the cache build (system directories, then ~/.local/share/fonts, ~/.fonts, embedded) */
static void op_scan(void *ctx)
{
	img2_init();
	img2_font_foreach(count_font, ctx);
	img2_shutdown();
}

/* scan + load of the fixture font + a face on it */
static void op_load(void *ctx)
{
	(void)ctx;
	img2_init();
	img2_font_t *font = NULL;
	if (img2_font_load("NoName", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL, &font) == IMG2_OK) {
		img2_font_face_t *face = img2_font_face_open(font);
		img2_font_face_close(face);
	} else if (font != NULL) {
		s_bad++;
	}
	img2_shutdown();
}

CTEST(alloc_a, font_scan_tail_load_and_face)
{
	font_home();
	int count = 0;
	/* warm-up run: in the coverage build the profile runtime allocates value-profiling nodes (through the
	 * wrapped calloc) the first time an indirect call site sees a target, which would shift the counts */
	op_scan(&count);
	/* the fixture's allocations (~ expansion, name-table parse, file-name family, entry, path), the embedded
	 * entries, the fallback and foreach's snapshot are the last ~24 allocations of a scan */
	long total = af_count_allocs(op_scan, &count);
	ASSERT_TRUE(count > 4);
	af_fail_range(op_scan, &count, total - 24, total);

	/* the file load, the interned handle and the face are the last allocations of op_load */
	op_load(NULL); /* warm-up, see op_scan */
	total = af_count_allocs(op_load, NULL);
	af_fail_range(op_load, NULL, total - 8, total);
	ASSERT_EQUAL(0, s_bad);
}
