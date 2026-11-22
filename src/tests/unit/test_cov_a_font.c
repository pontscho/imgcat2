/**
 * @file test_cov_a_font.c
 * @brief Font manager (text/font_manager.c): user font directories, TTF name tables, lookups
 *
 * $HOME points at a fixture tree under the test temp dir, so "~/.fonts" and "~/.local/share/fonts" are
 * scanned with fonts written in-test (minimal sfnt files: the tables stb_truetype's stbtt_InitFont() requires
 * plus a "name" table). Every test holds an img2_init() reference while it uses the font API and drops it
 * at the end, so the next test rebuilds the cache from its own fixture tree.
 */

#include <sys/stat.h>

#include <errno.h>
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

/* fresh $HOME with an empty ~/.fonts */
static void home_reset(const char *tag)
{
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
