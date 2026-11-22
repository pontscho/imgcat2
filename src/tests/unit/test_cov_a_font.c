/**
 * @file test_cov_a_font.c
 * @brief Font manager (text/font_manager.c): user font directories, TTF name tables, lookups
 *
 * $HOME points at a fixture tree under the test temp dir, so "~/.fonts" and "~/.local/share/fonts" are
 * scanned with fonts written in-test (minimal sfnt files: the tables stb_truetype's stbtt_InitFont() requires
 * plus a "name" table). Every test holds an img2_init() reference while it uses the font API and drops it
 * at the end, so the next test rebuilds the cache from its own fixture tree.
 */

#define _GNU_SOURCE

#include <sys/stat.h>

#include <errno.h>
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

/* ===== minimal sfnt writer ===== */

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

typedef struct {
	uint16_t platform;
	uint16_t encoding;
	uint16_t language;
	uint16_t name_id;
	const char *str; /* raw bytes (UTF-16BE for the Microsoft platform) */
	uint16_t len;
	int32_t offset_override; /* >= 0: string offset written instead of the real one */
} name_rec_t;

#define TTF_MAX 4096

/* name table (format 0) with the records' strings; returns its size */
static size_t build_name_table(uint8_t *p, const name_rec_t *r, int n)
{
	size_t storage = 6 + 12 * (size_t)n;
	be16(p, 0);
	be16(p + 2, (uint16_t)n);
	be16(p + 4, (uint16_t)storage);
	size_t str_off = 0;
	for (int i = 0; i < n; i++) {
		uint8_t *q = p + 6 + 12 * i;
		be16(q, r[i].platform);
		be16(q + 2, r[i].encoding);
		be16(q + 4, r[i].language);
		be16(q + 6, r[i].name_id);
		be16(q + 8, r[i].len);
		be16(q + 10, (uint16_t)(r[i].offset_override >= 0 ? (uint32_t)r[i].offset_override : str_off));
		memcpy(p + storage + str_off, r[i].str, r[i].len);
		str_off += r[i].len;
	}
	return storage + str_off;
}

/*
 * sfnt with cmap/head/hhea/hmtx/loca/glyf (enough for stbtt_InitFont()) and, if name != NULL, a name table
 * written LAST and unpadded: its final string ends exactly at the end of the file.
 */
static size_t build_ttf(uint8_t *out, const name_rec_t *name, int name_count)
{
	static uint8_t cmap[12 + 262];
	memset(cmap, 0, sizeof(cmap));
	be16(cmap + 2, 1);     /* one encoding record */
	be16(cmap + 4, 3);     /* Microsoft */
	be16(cmap + 6, 1);     /* Unicode BMP */
	be32(cmap + 8, 12);    /* subtable offset */
	be16(cmap + 14, 262);  /* format 0 subtable, all glyph 0 */
	static const uint8_t zeros[64];
	uint8_t name_buf[1024];
	size_t name_len = name != NULL ? build_name_table(name_buf, name, name_count) : 0;

	struct {
		const char *tag;
		const uint8_t *data;
		size_t len;
	} t[7] = {
		{ "cmap", cmap, sizeof(cmap) }, { "head", zeros, 54 }, { "hhea", zeros, 36 }, { "hmtx", zeros, 4 },
		{ "loca", zeros, 4 }, { "glyf", zeros, 4 }, { "name", name_buf, name_len },
	};
	int n = name != NULL ? 7 : 6;

	memset(out, 0, TTF_MAX);
	be32(out, 0x00010000);
	be16(out + 4, (uint16_t)n);
	size_t off = 12 + 16 * (size_t)n;
	for (int i = 0; i < n; i++) {
		uint8_t *d = out + 12 + 16 * i;
		memcpy(d, t[i].tag, 4);
		be32(d + 8, (uint32_t)off);
		be32(d + 12, (uint32_t)t[i].len);
		memcpy(out + off, t[i].data, t[i].len);
		off += t[i].len;
		if (i + 1 < n) {
			off = (off + 3) & ~(size_t)3;
		}
	}
	return off;
}

/* Microsoft / Unicode BMP / English (US) / family name record */
static name_rec_t family_rec(const char *utf16be, uint16_t len)
{
	name_rec_t r = { 3, 1, 0x409, 1, utf16be, len, -1 };
	return r;
}

/* ===== fixture tree ===== */

static char s_home[512];

static void path_in_home(char *buf, size_t size, const char *rel)
{
	snprintf(buf, size, "%s/%s", s_home, rel);
}

static void mkdir_in_home(const char *rel)
{
	char p[1024];
	path_in_home(p, sizeof(p), rel);
	mkdir(p, 0755);
}

static void write_in_home(const char *rel, const uint8_t *data, size_t len)
{
	char p[1024];
	path_in_home(p, sizeof(p), rel);
	FILE *f = fopen(p, "wb");
	if (f != NULL) {
		fwrite(data, 1, len, f);
		fclose(f);
	}
}

static int rm_entry(const char *path, const struct stat *sb, int flag, struct FTW *ftw)
{
	(void)sb;
	(void)flag;
	(void)ftw;
	remove(path);
	return 0;
}

/* removes the current fixture tree (also at exit) */
static void home_remove(void)
{
	if (s_home[0] != '\0') {
		nftw(s_home, rm_entry, 16, FTW_DEPTH | FTW_PHYS);
	}
}

/* fresh $HOME with an empty ~/.fonts (the previous fixture tree is removed) */
static void home_reset(const char *tag)
{
	static int registered;
	if (!registered) {
		atexit(home_remove);
		registered = 1;
	}
	home_remove();
	snprintf(s_home, sizeof(s_home), "%s/cov_a_font_%ld_%s", test_tmpdir(), (long)getpid(), tag);
	mkdir(s_home, 0755);
	mkdir_in_home(".fonts");
	setenv("HOME", s_home, 1);
}

typedef struct {
	const char *path_suffix;
	char family[256];
	int found;
	img2_font_weight_t weight;
	img2_font_style_t style;
} find_ctx_t;

static int find_by_path(const img2_font_info_t *info, void *user)
{
	find_ctx_t *c = user;
	size_t pl = strlen(info->path);
	size_t sl = strlen(c->path_suffix);
	if (pl >= sl && strcmp(info->path + pl - sl, c->path_suffix) == 0) {
		snprintf(c->family, sizeof(c->family), "%s", info->family);
		c->weight = info->weight;
		c->style = info->style;
		c->found++;
	}
	return 0;
}

/* family of the cached font whose path ends with suffix (NULL if none) */
static const char *family_of(const char *suffix, find_ctx_t *c)
{
	memset(c, 0, sizeof(*c));
	c->path_suffix = suffix;
	img2_font_foreach(find_by_path, c);
	return c->found == 1 ? c->family : NULL;
}

/* ===== TTF name table ===== */

CTEST(font_name, odd_length_utf16_name_stays_in_bounds)
{
	/* 5 bytes: "O", "d", then a lone trailing byte at the very end of the file (malloc'd with the exact
	 * file size): the converter must not read the missing second byte of a third code unit */
	home_reset("odd");
	uint8_t ttf[TTF_MAX];
	name_rec_t r = family_rec("\0O\0d\0", 5);
	write_in_home(".fonts/OddName.ttf", ttf, build_ttf(ttf, &r, 1));
	img2_init();
	find_ctx_t c;
	const char *fam = family_of("/.fonts/OddName.ttf", &c);
	img2_shutdown(); /* before any ASSERT: the next test must rebuild the cache */
	ASSERT_NOT_NULL(fam);
	ASSERT_STR("Od", fam);
}

CTEST(font_name, name_string_outside_the_file_is_ignored)
{
	/* the record points 0xFF00 bytes into a ~400-byte file: the family comes from the file name instead */
	home_reset("oob");
	uint8_t ttf[TTF_MAX];
	name_rec_t r = family_rec("\0X", 2);
	r.offset_override = 0xFF00;
	write_in_home(".fonts/Outside-Regular.ttf", ttf, build_ttf(ttf, &r, 1));
	img2_init();
	find_ctx_t c;
	const char *fam = family_of("/.fonts/Outside-Regular.ttf", &c);
	img2_shutdown();
	ASSERT_NOT_NULL(fam);
	ASSERT_STR("Outside", fam);
}

/* ===== user font directory scan ===== */

static void symlink_in_home(const char *target, const char *rel)
{
	char p[1024];
	path_in_home(p, sizeof(p), rel);
	unlink(p);
	if (symlink(target, p) != 0) {
		fprintf(stderr, "symlink %s: %s\n", p, strerror(errno));
	}
}

#define SYSFS_SHORT_READ "/sys/devices/system/cpu/online" /* st_size 4096, a few bytes readable */

/* the fixture tree: every scan / name-table / file-load variant */
static void build_scan_fixture(void)
{
	home_reset("scan");
	mkdir_in_home(".local");
	mkdir_in_home(".local/share");
	mkdir_in_home(".local/share/fonts");
	mkdir_in_home(".local/share/fonts/sub");
	uint8_t ttf[TTF_MAX];
	name_rec_t r;

	r = family_rec("\0P\0l\0a\0i\0n", 10);
	write_in_home(".fonts/Plain.ttf", ttf, build_ttf(ttf, &r, 1));
	r = family_rec("\0U\0\xe9\x4e\x2d", 6); /* U+0055 U+00E9 U+4E2D: 1-, 2- and 3-byte UTF-8 */
	write_in_home(".fonts/Uni-Bold.ttf", ttf, build_ttf(ttf, &r, 1));
	r = family_rec("\0A\0\x01\0B\0\x7f", 8); /* C0 control and DEL are replaced */
	write_in_home(".fonts/Ctl.ttf", ttf, build_ttf(ttf, &r, 1));
	write_in_home(".fonts/NoName.ttf", ttf, build_ttf(ttf, NULL, 0));
	r = family_rec("", 0);
	write_in_home(".fonts/ZeroLen-Italic.ttf", ttf, build_ttf(ttf, &r, 1));
	name_rec_t other[2] = { { 1, 0, 0, 1, "Mac", 3, -1 }, { 3, 1, 0x409, 4, "\0F\0u\0l\0l", 8, -1 } };
	write_in_home(".fonts/OtherRecords.ttf", ttf, build_ttf(ttf, other, 2)); /* no MS family record */
	r = family_rec("\0S\0o\0l\0o", 8);
	write_in_home(".local/share/fonts/sub/Solo-Bold.ttf", ttf, build_ttf(ttf, &r, 1));
	write_in_home(".fonts/Gone.ttf", ttf, build_ttf(ttf, NULL, 0));
	static const uint8_t zeros[12];
	write_in_home(".fonts/Garbage.ttf", zeros, sizeof(zeros)); /* stbtt_InitFont() fails: no tables */
	write_in_home(".fonts/Empty.ttf", zeros, 0);
	/* weight / style from the file name (empty files: the family comes from the name) */
	write_in_home(".fonts/Wa-b.ttf", zeros, 0);
	write_in_home(".fonts/Wb-b-x.ttf", zeros, 0);
	write_in_home(".fonts/Sa-i.otf", zeros, 0);
	write_in_home(".fonts/Sb-i-x.TTF", zeros, 0);
	write_in_home(".fonts/Sc-Oblique.ttf", zeros, 0);
	write_in_home(".fonts/NoDash.ttf", zeros, 0);
	write_in_home(".fonts/x", zeros, 0);          /* name shorter than an extension */
	write_in_home(".fonts/notes.txt", zeros, 0);  /* not a font */
	symlink_in_home("does-not-exist.ttf", ".fonts/Dangling.ttf");
	symlink_in_home(SYSFS_SHORT_READ, ".fonts/Sys.ttf");
	char p[1024];
	path_in_home(p, sizeof(p), ".fonts/Huge.ttf"); /* sparse, above the 50 MiB name-parse limit */
	write_in_home(".fonts/Huge.ttf", zeros, 0);
	if (truncate(p, 51L * 1024 * 1024) != 0) {
		fprintf(stderr, "truncate %s: %s\n", p, strerror(errno));
	}
}

typedef struct {
	const char *suffix;
	const char *family;
	img2_font_weight_t weight;
	img2_font_style_t style;
} expect_t;

CTEST(font_scan, families_weights_and_styles)
{
	build_scan_fixture();
	const expect_t ex[] = {
		{ "/.fonts/Plain.ttf", "Plain", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL },
		{ "/.fonts/Uni-Bold.ttf", "U\xc3\xa9\xe4\xb8\xad", IMG2_FONT_WEIGHT_BOLD, IMG2_FONT_STYLE_NORMAL },
		{ "/.fonts/Ctl.ttf", "A?B?", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL },
		{ "/.fonts/NoName.ttf", "NoName", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL },
		{ "/.fonts/ZeroLen-Italic.ttf", "ZeroLen", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_ITALIC },
		{ "/.fonts/OtherRecords.ttf", "OtherRecords", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL },
		{ "/sub/Solo-Bold.ttf", "Solo", IMG2_FONT_WEIGHT_BOLD, IMG2_FONT_STYLE_NORMAL },
		{ "/.fonts/Garbage.ttf", "Garbage", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL },
		{ "/.fonts/Empty.ttf", "Empty", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL },
		{ "/.fonts/Wa-b.ttf", "Wa", IMG2_FONT_WEIGHT_BOLD, IMG2_FONT_STYLE_NORMAL },
		{ "/.fonts/Wb-b-x.ttf", "Wb-b", IMG2_FONT_WEIGHT_BOLD, IMG2_FONT_STYLE_NORMAL },
		{ "/.fonts/Sa-i.otf", "Sa", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_ITALIC },
		{ "/.fonts/Sb-i-x.TTF", "Sb-i", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_ITALIC },
		{ "/.fonts/Sc-Oblique.ttf", "Sc", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_ITALIC },
		{ "/.fonts/NoDash.ttf", "NoDash", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL },
		{ "/.fonts/Dangling.ttf", "Dangling", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL },
		{ "/.fonts/Sys.ttf", "Sys", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL },
		{ "/.fonts/Huge.ttf", "Huge", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL },
	};
	const size_t n = sizeof(ex) / sizeof(ex[0]);
	find_ctx_t got[sizeof(ex) / sizeof(ex[0])];
	find_ctx_t c;
	img2_init();
	for (size_t i = 0; i < n; i++) {
		family_of(ex[i].suffix, &got[i]);
	}
	int skipped_short = family_of("/.fonts/x", &c) == NULL && family_of("/notes.txt", &c) == NULL;
	img2_shutdown();
	for (size_t i = 0; i < n; i++) {
		if (got[i].found != 1 || strcmp(got[i].family, ex[i].family) != 0 || got[i].weight != ex[i].weight || got[i].style != ex[i].style) {
			CTEST_ERR("%s: found %d family '%s' weight %d style %d", ex[i].suffix, got[i].found, got[i].family, (int)got[i].weight, (int)got[i].style);
		}
	}
	ASSERT_TRUE(skipped_short);
}

static int stop_at_first(const img2_font_info_t *info, void *user)
{
	(void)info;
	(*(int *)user)++;
	return 1;
}

CTEST(font_scan, lookups_load_failures_and_fallbacks)
{
	build_scan_fixture();
	img2_init();
	char gone[1024];
	path_in_home(gone, sizeof(gone), ".fonts/Gone.ttf");
	int visits = 0;
	img2_font_foreach(stop_at_first, &visits); /* builds the cache; the visitor stops the walk */
	unlink(gone);                                /* scanned, then removed: loading it fails */

	img2_font_t *fallback = NULL;
	img2_status_t st_fb = img2_font_load_fallback(&fallback);
	img2_font_t *f[12] = { 0 };
	img2_status_t st[12];
	st[0] = img2_font_load("Plain", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL, &f[0]);
	st[1] = img2_font_load("Plain", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL, &f[1]); /* interned */
	st[2] = img2_font_load("Solo", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_ITALIC, &f[2]);  /* any variant */
	st[3] = img2_font_load("Empty", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL, &f[3]); /* size 0 */
	st[4] = img2_font_load("Gone", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL, &f[4]);  /* fopen fails */
	st[5] = img2_font_load("Sys", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL, &f[5]);   /* short read */
	st[6] = img2_font_load("Garbage", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL, &f[6]);
	st[7] = img2_font_load("mono", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL, &f[7]);
	st[8] = img2_font_load("Courier", IMG2_FONT_WEIGHT_BOLD, IMG2_FONT_STYLE_NORMAL, &f[8]);
	st[9] = img2_font_load("sans-serif", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL, &f[9]);
	st[10] = img2_font_load("sans", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL, &f[10]);
	st[11] = img2_font_load("serif", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL, &f[11]);
	img2_font_face_t *garbage_face = img2_font_face_open(f[6]);
	size_t sz = 1;
	const uint8_t *no_font = img2_font_get_data(NULL, &sz);
	const uint8_t *no_size = img2_font_get_data(f[0], NULL);
	const char *no_family = img2_font_get_family(NULL);
	img2_font_face_t *no_face = img2_font_face_open(NULL);
	img2_font_t *unused = NULL;
	img2_status_t st_null_family = img2_font_load(NULL, IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL, &unused);
	img2_status_t st_null_out = img2_font_load("Plain", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL, NULL);
	char fam2[64]; /* copies: the handles' strings are freed by the final img2_shutdown() */
	char fam6[64];
	snprintf(fam2, sizeof(fam2), "%s", img2_font_get_family(f[2]));
	snprintf(fam6, sizeof(fam6), "%s", img2_font_get_family(f[6]));
	img2_font_t *plain = f[1];
	int same = f[0] == f[1];
	img2_shutdown();

	ASSERT_EQUAL(1, visits);
	ASSERT_EQUAL(IMG2_OK, st_fb);
	for (int i = 0; i < 12; i++) {
		ASSERT_EQUAL(IMG2_OK, st[i]);
	}
	ASSERT_TRUE(same);
	ASSERT_NOT_NULL(plain);
	ASSERT_STR("Solo", fam2);
	ASSERT_STR("Garbage", fam6);
	ASSERT_TRUE(fallback == f[3]); /* every failed file load falls back to the embedded font */
	ASSERT_TRUE(fallback == f[4]);
	if (access(SYSFS_SHORT_READ, R_OK) == 0) {
		ASSERT_TRUE(fallback == f[5]);
	}
	ASSERT_NULL(garbage_face);
	ASSERT_NULL(no_font);
	ASSERT_NULL(no_size);
	ASSERT_NULL(no_family);
	ASSERT_NULL(no_face);
	ASSERT_EQUAL(IMG2_E_ARG, st_null_family);
	ASSERT_EQUAL(IMG2_E_ARG, st_null_out);
}

CTEST(font_scan, home_unset_and_internal_entry_points)
{
	char *saved = getenv("HOME") != NULL ? strdup(getenv("HOME")) : NULL;
	unsetenv("HOME"); /* "~/..." is then used verbatim (and does not exist) */
	img2_init();
	int visits = 0;
	img2_font_foreach(stop_at_first, &visits);
	bool built_again = img2_font_manager_init_lazy(); /* already built: false */
	img2_font_cache_init_lazy();                       /* already built: nothing to do */
	const int one = 1;
	img2_font_manager_cleanup_final(&one); /* a reference is left: keeps the cache */
	img2_font_t *still = img2_font_find_fallback();
	img2_font_manager_cleanup_final(NULL); /* frees it; the next call rebuilds lazily */
	img2_font_t *rebuilt = img2_font_find_fallback();
	img2_shutdown();
	if (saved != NULL) {
		setenv("HOME", saved, 1);
		free(saved);
	}
	ASSERT_EQUAL(1, visits);
	ASSERT_FALSE(built_again);
	ASSERT_NOT_NULL(still);
	ASSERT_NOT_NULL(rebuilt);
}
