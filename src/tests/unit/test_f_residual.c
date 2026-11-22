/**
 * @file test_f_residual.c
 * @brief Residual audit findings (F-numbers) of the library and the CLI
 *
 * F27: font family names taken from a font's FILE NAME (the fallback when the name table gives none) kept C0
 *      controls / DEL (the name-table path replaced them); `imgcat2 --fonts` printed families raw.
 */

#define _GNU_SOURCE

#include <sys/stat.h>

#include <fcntl.h>
#include <ftw.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <img2/img2.h>

#include "../../imgcat2/cli.h"
#include "../ctest.h"
#include "../test_tmpdir.h"

/* ===== F27: family names from file names ===== */

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

/* $HOME/.fonts/<name> for each name (empty files: no name table, the family comes from the file name) */
static void home_with_fonts(const char *const *names, int n)
{
	snprintf(s_home, sizeof(s_home), "%s/f_residual_%ld", test_tmpdir(), (long)getpid());
	home_remove();
	mkdir(s_home, 0755);
	char p[1024];
	snprintf(p, sizeof(p), "%s/.fonts", s_home);
	mkdir(p, 0755);
	for (int i = 0; i < n; i++) {
		snprintf(p, sizeof(p), "%s/.fonts/%s", s_home, names[i]);
		FILE *f = fopen(p, "wb");
		if (f != NULL) {
			fclose(f);
		}
	}
	setenv("HOME", s_home, 1);
}

typedef struct {
	const char *path_suffix;
	char family[256];
	int found;
} find_ctx_t;

static int find_by_path(const img2_font_info_t *info, void *user)
{
	find_ctx_t *c = user;
	size_t pl = strlen(info->path);
	size_t sl = strlen(c->path_suffix);
	if (pl >= sl && strcmp(info->path + pl - sl, c->path_suffix) == 0) {
		snprintf(c->family, sizeof(c->family), "%s", info->family);
		c->found++;
	}
	return 0;
}

static const char *const k_font_names[] = {
	"Ev\x1b[31mil\x7f-Bold.ttf", /* C0 (ESC) and DEL: replaced by '?' in the library */
	"Cone\xc2\x9b" "2J-Bold.ttf", /* C1 CSI (U+009B): kept by the library, escaped by the CLI */
};

CTEST(f27, family_from_file_name_has_no_controls)
{
	home_with_fonts(k_font_names, 2);
	img2_init();
	find_ctx_t c;
	memset(&c, 0, sizeof(c));
	c.path_suffix = "/.fonts/Ev\x1b[31mil\x7f-Bold.ttf";
	img2_font_foreach(find_by_path, &c);
	img2_shutdown();
	home_remove();
	ASSERT_EQUAL(1, c.found);
	ASSERT_STR("Ev?[31mil?", c.family);
}

CTEST(f27, font_list_escapes_family_names)
{
	home_with_fonts(k_font_names, 2);
	img2_init();
	char path[600];
	snprintf(path, sizeof(path), "%s/f_residual_fonts_%ld.txt", test_tmpdir(), (long)getpid());
	fflush(stdout);
	int saved = dup(STDOUT_FILENO);
	int f = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
	ASSERT_TRUE(saved >= 0 && f >= 0);
	ASSERT_TRUE(dup2(f, STDOUT_FILENO) >= 0);
	close(f);
	print_font_list();
	fflush(stdout);
	dup2(saved, STDOUT_FILENO);
	close(saved);
	img2_shutdown();
	home_remove();

	FILE *in = fopen(path, "rb");
	ASSERT_NOT_NULL(in);
	size_t cap = 1 << 20;
	char *out = calloc(1, cap + 1);
	ASSERT_NOT_NULL(out);
	size_t n = fread(out, 1, cap, in);
	fclose(in);
	unlink(path);
	int has_esc = memchr(out, 0x1b, n) != NULL;
	int has_c1 = memmem(out, n, "\xc2\x9b", 2) != NULL;
	int has_c0_family = strstr(out, "Ev?[31mil?: ") != NULL;
	int has_c1_family = strstr(out, "Cone\\xC2\\x9B2J: ") != NULL || strstr(out, "Cone\\xc2\\x9b2J: ") != NULL;
	free(out);
	ASSERT_FALSE(has_esc);
	ASSERT_FALSE(has_c1);
	ASSERT_TRUE(has_c0_family);
	ASSERT_TRUE(has_c1_family);
}
