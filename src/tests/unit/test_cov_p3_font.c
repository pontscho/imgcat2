/**
 * @file test_cov_p3_font.c
 * @brief Coverage phase 3: malformed / truncated font files are rejected before stb_truetype parses them
 *
 * stb_truetype does no range checking: stbtt_InitFont() reads the table directory, the cmap encoding records,
 * head and maxp at offsets taken from the file. The font manager reads font files into a malloc'd buffer of
 * exactly the file size, so a truncated font makes stb_truetype read past that buffer.
 *
 * Linked with -Wl,--wrap=malloc,--wrap=free (Linux only): while a size is armed, malloc() of exactly that size
 * returns a block that ends at a PROT_NONE guard page, so any read past the font data faults (SIGSEGV)
 * deterministically, without a sanitizer build. Fixture sizes are multiples of 16, so the guarded blocks keep
 * malloc's alignment.
 *
 * $HOME points at a fixture tree under the test temp dir; "~/.fonts" is scanned with fonts written in-test.
 */

#define _GNU_SOURCE

#include <sys/mman.h>
#include <sys/stat.h>

#include <ftw.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <img2/img2.h>

#include "../../libimg2/text/font_manager.h"
#include "../ctest.h"
#include "../test_tmpdir.h"

/* ===== guarded allocations ===== */

void *__real_malloc(size_t size);
void __real_free(void *ptr);
void *__wrap_malloc(size_t size);
void __wrap_free(void *ptr);

#define GUARD_SIZES 16
#define GUARD_LIVE 64

static size_t s_guard_size[GUARD_SIZES];
static int s_guard_count;
static struct {
	void *ptr;
	void *base;
	size_t len;
} s_live[GUARD_LIVE];
static long s_guarded; /* guarded blocks handed out */

static void guard_arm(size_t size)
{
	if (s_guard_count < GUARD_SIZES) {
		s_guard_size[s_guard_count++] = size;
	}
}

static void guard_disarm(void)
{
	s_guard_count = 0;
}

static void *guard_alloc(size_t size)
{
	long page = sysconf(_SC_PAGESIZE);
	size_t data_pages = (size + (size_t)page - 1) / (size_t)page;
	size_t len = (data_pages + 1) * (size_t)page;
	for (int i = 0; i < GUARD_LIVE; i++) {
		if (s_live[i].ptr != NULL) {
			continue;
		}
		uint8_t *base = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (base == MAP_FAILED) {
			return NULL;
		}
		uint8_t *guard = base + data_pages * (size_t)page;
		mprotect(guard, (size_t)page, PROT_NONE);
		s_live[i].ptr = guard - size;
		s_live[i].base = base;
		s_live[i].len = len;
		s_guarded++;
		return s_live[i].ptr;
	}
	return NULL;
}

void *__wrap_malloc(size_t size)
{
	if (size > 0 && size % 16 == 0) {
		for (int i = 0; i < s_guard_count; i++) {
			if (s_guard_size[i] == size) {
				void *p = guard_alloc(size);
				if (p != NULL) {
					return p;
				}
			}
		}
	}
	return __real_malloc(size);
}

void __wrap_free(void *ptr)
{
	if (ptr != NULL) {
		for (int i = 0; i < GUARD_LIVE; i++) {
			if (s_live[i].ptr == ptr) {
				munmap(s_live[i].base, s_live[i].len);
				s_live[i].ptr = NULL;
				return;
			}
		}
	}
	__real_free(ptr);
}

/* ===== sfnt writer ===== */

static void be16(uint8_t *p, uint32_t v)
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

#define FONT_MAX 2048

typedef struct {
	const char *tag;
	const uint8_t *data;
	size_t len;
} table_t;

typedef struct {
	uint8_t b[FONT_MAX];
	size_t len;
	size_t dir_off[8];   /* offset of each directory record */
	size_t table_off[8]; /* offset of each table's data */
} font_t;

/* sfnt with the given tables, in order, each 4-byte aligned except the last (it ends the file) */
static void font_build(font_t *f, uint32_t version, const table_t *t, int n)
{
	memset(f, 0, sizeof(*f));
	be32(f->b, version);
	be16(f->b + 4, (uint32_t)n);
	size_t off = 12 + 16 * (size_t)n;
	for (int i = 0; i < n; i++) {
		uint8_t *d = f->b + 12 + 16 * i;
		f->dir_off[i] = (size_t)(d - f->b);
		f->table_off[i] = off;
		memcpy(d, t[i].tag, 4);
		be32(d + 8, (uint32_t)off);
		be32(d + 12, (uint32_t)t[i].len);
		memcpy(f->b + off, t[i].data, t[i].len);
		off += t[i].len;
		if (i + 1 < n) {
			off = (off + 3) & ~(size_t)3;
		}
	}
	f->len = off;
}

/* cmap with one Microsoft / Unicode BMP record and a format 0 subtable */
static const uint8_t *cmap_table(size_t *len)
{
	static uint8_t cmap[12 + 262];
	be16(cmap + 2, 1);
	be16(cmap + 4, 3);
	be16(cmap + 6, 1);
	be32(cmap + 8, 12);
	be16(cmap + 14, 262);
	*len = sizeof(cmap);
	return cmap;
}

/* name table: one Microsoft family record "\0N\0a\0m\0e" */
static const uint8_t k_name[] = { 0, 0, 0, 1, 0, 18, 0, 3, 0, 1, 0x04, 0x09, 0, 1, 0, 8, 0, 0, 0, 'N', 0, 'a', 0, 'm', 0, 'e' };
static const uint8_t k_zeros[64];

/*
 * Valid font: cmap, head, hhea, hmtx, loca, glyf, maxp, name. extra_tail > 0 pads the last table so the file
 * size becomes a multiple of 16 (for the guard).
 */
static void font_valid(font_t *f, uint32_t version, const char *last_tag, size_t last_len)
{
	size_t cmap_len = 0;
	const uint8_t *cmap = cmap_table(&cmap_len);
	table_t t[8] = {
		{ "cmap", cmap, cmap_len }, { "head", k_zeros, 54 }, { "hhea", k_zeros, 36 }, { "hmtx", k_zeros, 4 },
		{ "loca", k_zeros, 4 },     { "glyf", k_zeros, 4 },  { "name", k_name, sizeof(k_name) },
		{ last_tag, k_zeros, last_len },
	};
	font_build(f, version, t, 8);
}

/* grows the last table so that the file size is a multiple of 16 */
static void font_pad16(font_t *f, int last)
{
	size_t pad = (16 - f->len % 16) % 16;
	f->len += pad;
	be32(f->b + f->dir_off[last] + 12, (uint32_t)(f->len - f->table_off[last]));
}

/* ===== fixture tree ===== */

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

static void home_reset(const char *tag)
{
	static int registered;
	if (!registered) {
		atexit(home_remove);
		registered = 1;
	}
	home_remove();
	snprintf(s_home, sizeof(s_home), "%s/cov_p3_font_%ld_%s", test_tmpdir(), (long)getpid(), tag);
	mkdir(s_home, 0755);
	char p[1024];
	snprintf(p, sizeof(p), "%s/.fonts", s_home);
	mkdir(p, 0755);
	setenv("HOME", s_home, 1);
}

/* writes ~/.fonts/<name> and arms the guard for its size */
static void put_font(const char *name, const uint8_t *data, size_t len)
{
	char p[1024];
	snprintf(p, sizeof(p), "%s/.fonts/%s", s_home, name);
	FILE *f = fopen(p, "wb");
	if (f != NULL) {
		fwrite(data, 1, len, f);
		fclose(f);
	}
	guard_arm(len);
}

typedef struct {
	const char *file;
	char family[64];
	int found;
} find_t;

static int find_cb(const img2_font_info_t *info, void *user)
{
	find_t *c = user;
	const char *slash = strrchr(info->path, '/');
	if (slash != NULL && strcmp(slash + 1, c->file) == 0) {
		snprintf(c->family, sizeof(c->family), "%s", info->family);
		c->found++;
	}
	return 0;
}

static const char *family_of(const char *file, find_t *c)
{
	memset(c, 0, sizeof(*c));
	c->file = file;
	img2_font_foreach(find_cb, c);
	return c->found == 1 ? c->family : NULL;
}

/* ===== tests ===== */

/* the guard works: a valid font is parsed from a guarded buffer (its family comes from the name table) */
CTEST(p3_font, valid_font_from_a_guarded_buffer)
{
	home_reset("valid");
	font_t f;
	font_valid(&f, 0x00010000, "maxp", 6);
	font_pad16(&f, 7);
	put_font("Valid.ttf", f.b, f.len);
	long before = s_guarded;
	img2_init();
	find_t c;
	const char *fam = family_of("Valid.ttf", &c);
	img2_font_t *font = NULL;
	img2_status_t st = img2_font_load("Name", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL, &font);
	img2_font_face_t *face = st == IMG2_OK ? img2_font_face_open(font) : NULL;
	int face_ok = face != NULL;
	img2_font_face_close(face);
	img2_shutdown();
	guard_disarm();
	ASSERT_NOT_NULL(fam);
	ASSERT_STR("Name", fam);
	ASSERT_EQUAL(IMG2_OK, st);
	ASSERT_TRUE(face_ok);
	ASSERT_TRUE(s_guarded - before >= 2); /* the name parse and the file load both used a guarded buffer */
}

typedef struct {
	const char *file;   /* family expected from the file name */
	font_t font;
} bad_font_t;

/* every malformed font: its family comes from the file name and it cannot be opened as a face */
static void check_rejected(bad_font_t *bad, int n)
{
	home_reset("bad");
	for (int i = 0; i < n; i++) {
		put_font(bad[i].file, bad[i].font.b, bad[i].font.len);
	}
	img2_init();
	char fam[16][64];
	int found[16];
	int face_null[16];
	for (int i = 0; i < n; i++) {
		find_t c;
		const char *f = family_of(bad[i].file, &c);
		found[i] = f != NULL;
		snprintf(fam[i], sizeof(fam[i]), "%s", f != NULL ? f : "");
		img2_font_t *font = NULL;
		/* the family is the file name without ".ttf" */
		char want[64];
		snprintf(want, sizeof(want), "%.*s", (int)(strlen(bad[i].file) - 4), bad[i].file);
		img2_font_load(want, IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL, &font);
		img2_font_t *fallback = NULL;
		img2_font_load_fallback(&fallback);
		img2_font_face_t *face = font != fallback ? img2_font_face_open(font) : NULL;
		face_null[i] = face == NULL;
		img2_font_face_close(face);
	}
	img2_shutdown();
	guard_disarm();
	for (int i = 0; i < n; i++) {
		char want[64];
		snprintf(want, sizeof(want), "%.*s", (int)(strlen(bad[i].file) - 4), bad[i].file);
		if (!found[i] || strcmp(fam[i], want) != 0 || !face_null[i]) {
			CTEST_ERR("%s: found %d family '%s' face rejected %d", bad[i].file, found[i], fam[i], face_null[i]);
		}
	}
}

/* the last table (index 7) moved to the end of the file, `len` bytes long, file size a multiple of 16 */
static void font_short_last(font_t *v, size_t len)
{
	v->len = v->table_off[7] + len;
	while (v->len % 16 != 0) {
		v->len++;
	}
	memmove(v->b + v->len - len, v->b + v->table_off[7], len);
	memset(v->b + v->table_off[7], 0, v->len - len - v->table_off[7]);
	be32(v->b + v->dir_off[7] + 8, (uint32_t)(v->len - len));
	be32(v->b + v->dir_off[7] + 12, (uint32_t)len);
}

static void reject_one(const char *file, const font_t *font)
{
	static bad_font_t bad;
	bad.file = file;
	bad.font = *font;
	check_rejected(&bad, 1);
}

/* table directory cut off: 8 records announced, the file ends inside the third */
CTEST(p3_font, truncated_table_directory)
{
	static font_t v;
	font_valid(&v, 0x00010000, "maxp", 6);
	v.len = 12 + 16 * 2 + 4;
	reject_one("TruncDir.ttf", &v);
}

/* cmap starts exactly at the end of the file (offset + length inside the file, but empty) */
CTEST(p3_font, cmap_at_the_end_of_the_file)
{
	static font_t v;
	font_valid(&v, 0x00010000, "maxp", 6);
	font_pad16(&v, 7);
	be32(v.b + v.dir_off[0] + 8, (uint32_t)v.len);
	be32(v.b + v.dir_off[0] + 12, 0);
	reject_one("CmapPastEnd.ttf", &v);
}

/* a table that runs past the end of the file */
CTEST(p3_font, table_past_the_end)
{
	static font_t v;
	font_valid(&v, 0x00010000, "maxp", 6);
	font_pad16(&v, 7);
	be32(v.b + v.dir_off[7] + 12, (uint32_t)(v.len - v.table_off[7] + 1));
	reject_one("LongMaxp.ttf", &v);
}

/* offset + length wraps in 32 bits (0xFFFFFFF0 + 0x20 = 0x10): only 64-bit arithmetic rejects it. glyf is
 * not read by stbtt_InitFont, so the old code accepted the font and took the family from its name table. */
CTEST(p3_font, offset_plus_length_wraps_32_bits)
{
	static font_t v;
	font_valid(&v, 0x00010000, "maxp", 6);
	font_pad16(&v, 7);
	be32(v.b + v.dir_off[5] + 8, 0xFFFFFFF0u);
	be32(v.b + v.dir_off[5] + 12, 0x20);
	reject_one("Wrap32.ttf", &v);
}

/* head shorter than the 54 bytes stbtt_InitFont reads (it ends the file) */
CTEST(p3_font, short_head)
{
	static font_t v;
	font_valid(&v, 0x00010000, "head", 54);
	be32(v.b + v.dir_off[1], 0x6A756E6B); /* the full head record becomes 'junk' */
	font_short_last(&v, 4);
	reject_one("ShortHead.ttf", &v);
}

/* maxp shorter than 6 bytes (it ends the file) */
CTEST(p3_font, short_maxp)
{
	static font_t v;
	font_valid(&v, 0x00010000, "maxp", 6);
	font_short_last(&v, 2);
	reject_one("ShortMaxp.ttf", &v);
}

/* cmap header announcing 40 encoding records in a 12-byte table (it ends the file) */
CTEST(p3_font, short_cmap)
{
	static font_t v;
	font_valid(&v, 0x00010000, "cmap", 12);
	be32(v.b + v.dir_off[0], 0x6A756E6B); /* the full cmap record becomes 'junk' */
	font_short_last(&v, 12);
	v.b[v.len - 12 + 3] = 40;
	reject_one("ShortCmap.ttf", &v);
}

/* cmap shorter than its 4-byte header */
CTEST(p3_font, cmap_without_header)
{
	static font_t v;
	font_valid(&v, 0x00010000, "cmap", 12);
	be32(v.b + v.dir_off[0], 0x6A756E6B);
	font_short_last(&v, 2);
	reject_one("TinyCmap.ttf", &v);
}

/* structurally valid, but without a cmap table: stbtt_InitFont() itself refuses it */
CTEST(p3_font, valid_structure_without_cmap)
{
	static font_t v;
	font_valid(&v, 0x00010000, "maxp", 6);
	be32(v.b + v.dir_off[0], 0x6A756E6B);
	font_pad16(&v, 7);
	reject_one("NoCmap.ttf", &v);
}

/* fewer than 12 bytes */
CTEST(p3_font, shorter_than_the_header)
{
	static font_t v;
	memset(&v, 0, sizeof(v));
	be32(v.b, 0x00010000);
	be16(v.b + 4, 8);
	v.len = 8;
	reject_one("Tiny.ttf", &v);
}

/* not an sfnt (a WOFF file, a font collection): the old code handed them to stbtt_InitFont as well */
CTEST(p3_font, unknown_sfnt_version)
{
	static font_t v;
	font_valid(&v, 0x774F4646, "maxp", 6); /* 'wOFF' */
	font_pad16(&v, 7);
	reject_one("Woff.ttf", &v);
	font_valid(&v, 0x74746366, "maxp", 6); /* 'ttcf' */
	font_pad16(&v, 7);
	reject_one("Collection.ttf", &v);
}

/* every sfnt version stb_truetype knows is accepted (stbtt_InitFont itself ignores the version: with a glyf
 * table it parses the font as TrueType) */
CTEST(p3_font, accepted_sfnt_versions)
{
	static const struct {
		uint32_t version;
		const char *file;
		const char *family;
	} ok[] = {
		{ 0x00010000, "V1.ttf", "Name" },
		{ 0x74727565, "VTrue.ttf", "Name" }, /* 'true' */
		{ 0x74797031, "VTyp1.ttf", "Name" }, /* 'typ1' */
		{ 0x31000000, "VOne.ttf", "Name" },  /* '1\0\0\0' */
		{ 0x4F54544F, "VOtto.ttf", "Name" }, /* 'OTTO' */
	};
	for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) {
		home_reset("ver");
		font_t f;
		font_valid(&f, ok[i].version, "maxp", 6);
		font_pad16(&f, 7);
		put_font(ok[i].file, f.b, f.len);
		img2_init();
		find_t c;
		const char *fam = family_of(ok[i].file, &c);
		char got[64];
		snprintf(got, sizeof(got), "%s", fam != NULL ? fam : "");
		img2_shutdown();
		guard_disarm();
		ASSERT_STR(ok[i].family, got);
	}
}
