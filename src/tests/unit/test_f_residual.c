/**
 * @file test_f_residual.c
 * @brief Residual audit findings (F-numbers) of the library and the CLI
 *
 * F27: font family names taken from a font's FILE NAME (the fallback when the name table gives none) kept C0
 *      controls / DEL (the name-table path replaced them); `imgcat2 --fonts` printed families raw.
 * F17: the JPEG XL frame duration in ms, ticks * 1000 * tps_den / tps_num, wrapped in 64 bits before the
 *      UINT32_MAX clamp (img2_decode_ticks_to_ms()).
 * F21: img2_decode() accepted any img2_decode_opts_t.jpeg_deblock (documented -1 / 0 / 1); the JPEG decoder
 *      treated every non-zero value as "on".
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

#ifdef IMG2_HAVE_JXL
#include <jxl/encode.h>
#endif

#include "../../imgcat2/cli.h"
#include "../../libimg2/decoders/decoder.h"
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

/* ===== F17: tick durations in ms ===== */

CTEST(f17, ticks_to_ms_saturates_instead_of_wrapping)
{
	ASSERT_EQUAL(0, (int)img2_decode_ticks_to_ms(5, 0, 1));          /* no tick rate */
	ASSERT_EQUAL(100, (int)img2_decode_ticks_to_ms(100, 1000, 1));   /* 1 tick = 1 ms */
	ASSERT_EQUAL(333, (int)img2_decode_ticks_to_ms(10, 30, 1));      /* floor(333.3) */
	ASSERT_EQUAL(4004, (int)img2_decode_ticks_to_ms(120, 30000, 1001)); /* NTSC: floor(4004.0) */
	ASSERT_EQUAL(1, (int)img2_decode_ticks_to_ms(1, 1000, 1));
	/* largest values below the clamp, and the first one above it */
	ASSERT_TRUE(img2_decode_ticks_to_ms(4294967u, 1000, 1) == 4294967u);
	ASSERT_TRUE(img2_decode_ticks_to_ms(UINT32_MAX, 1000, 1) == UINT32_MAX);
	ASSERT_TRUE(img2_decode_ticks_to_ms(4294967u, 1, 1) == 4294967000u);
	ASSERT_TRUE(img2_decode_ticks_to_ms(4294968u, 1, 1) == UINT32_MAX);
	ASSERT_TRUE(img2_decode_ticks_to_ms(2u * 4294967u + 1u, 2, 1) == UINT32_MAX); /* 4294967500: over by the remainder */
	/* ticks * 1000 * tps_den >= 2^64 (wrapped to a small value before the clamp) */
	ASSERT_TRUE(img2_decode_ticks_to_ms(UINT32_MAX, 1, UINT32_MAX) == UINT32_MAX);
	ASSERT_TRUE(img2_decode_ticks_to_ms(4000000000u, 4000000000u, 4000000000u) == UINT32_MAX);
	ASSERT_TRUE(img2_decode_ticks_to_ms(UINT32_MAX, 4096u, 1u << 30) == UINT32_MAX);
	/* exact below the clamp with large rates (remainder part of the division) */
	ASSERT_TRUE(img2_decode_ticks_to_ms(7u, 3000000000u, 4000000000u) == 9333u);
	ASSERT_TRUE(img2_decode_ticks_to_ms(UINT32_MAX, UINT32_MAX, 1) == 1000u);
	ASSERT_TRUE(img2_decode_ticks_to_ms(UINT32_MAX - 1u, UINT32_MAX, 1) == 999u);
}

#ifdef IMG2_HAVE_JXL
/* Two-frame 1 x 1 lossless JXL animation at tps_num / 1 ticks per second, each frame `duration` ticks (malloc) */
static uint8_t *jxl_anim(uint32_t tps_num, uint32_t duration, size_t *len)
{
	JxlEncoder *enc = JxlEncoderCreate(NULL);
	uint8_t *out = NULL;
	if (enc == NULL) {
		return NULL;
	}
	JxlBasicInfo bi;
	JxlEncoderInitBasicInfo(&bi);
	bi.xsize = 1;
	bi.ysize = 1;
	bi.bits_per_sample = 8;
	bi.uses_original_profile = JXL_TRUE;
	bi.num_color_channels = 3;
	bi.have_animation = JXL_TRUE;
	bi.animation.tps_numerator = tps_num;
	bi.animation.tps_denominator = 1;
	JxlColorEncoding ce;
	JxlColorEncodingSetToSRGB(&ce, JXL_FALSE);
	JxlEncoderFrameSettings *fs = NULL;
	if (JxlEncoderSetBasicInfo(enc, &bi) != JXL_ENC_SUCCESS || JxlEncoderSetColorEncoding(enc, &ce) != JXL_ENC_SUCCESS ||
	    (fs = JxlEncoderFrameSettingsCreate(enc, NULL)) == NULL || JxlEncoderSetFrameLossless(fs, JXL_TRUE) != JXL_ENC_SUCCESS) {
		goto done;
	}
	JxlPixelFormat fmt = { .num_channels = 3, .data_type = JXL_TYPE_UINT8, .endianness = JXL_NATIVE_ENDIAN, .align = 0 };
	for (int i = 0; i < 2; i++) {
		uint8_t px[3] = { (uint8_t)(i * 99), 7, 7 };
		JxlFrameHeader fh;
		JxlEncoderInitFrameHeader(&fh);
		fh.duration = duration;
		if (JxlEncoderSetFrameHeader(fs, &fh) != JXL_ENC_SUCCESS || JxlEncoderAddImageFrame(fs, &fmt, px, sizeof(px)) != JXL_ENC_SUCCESS) {
			goto done;
		}
	}
	JxlEncoderCloseInput(enc);
	size_t cap = 4096;
	out = malloc(cap);
	uint8_t *next = out;
	size_t avail = cap;
	if (out == NULL || JxlEncoderProcessOutput(enc, &next, &avail) != JXL_ENC_SUCCESS) { /* a few hundred bytes */
		free(out);
		out = NULL;
		goto done;
	}
	*len = (size_t)(next - out);
done:
	JxlEncoderDestroy(enc);
	return out;
}

CTEST(f17, jxl_delay_saturates_in_the_decoder)
{
	/* 30 ticks/s: 128849019 ticks = 4294967300 ms (over UINT32_MAX by the remainder: 4294967 s + 300 ms);
	 * 128849018 ticks = 4294967266 ms (exact, below the clamp) */
	const uint32_t ticks[2] = { 128849019u, 128849018u };
	const uint32_t want[2] = { UINT32_MAX, 4294967266u };
	for (int i = 0; i < 2; i++) {
		size_t len = 0;
		uint8_t *d = jxl_anim(30, ticks[i], &len);
		ASSERT_NOT_NULL(d);
		img2_frames_t fr = { 0 };
		ASSERT_EQUAL(IMG2_OK, img2_decode(d, len, NULL, &fr));
		free(d);
		ASSERT_EQUAL(2, fr.count);
		uint32_t got = fr.frames[0]->delay_ms;
		img2_frames_free(&fr);
		ASSERT_TRUE(got == want[i]);
	}
}
#endif

/* ===== F21: jpeg_deblock outside -1 / 0 / 1 ===== */

CTEST(f21, jpeg_deblock_out_of_range_is_arg)
{
	img2_image_t *img = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_create(4, 4, &img));
	img2_frames_t one = { .frames = &img, .count = 1 };
	img2_encode_opts_t eo = { .quality = 90 };
	uint8_t *jpg = NULL;
	size_t jlen = 0;
	img2_status_t est = img2_encode(&one, IMG2_FORMAT_JPEG, &eo, &jpg, &jlen);
	img2_image_destroy(img);
	ASSERT_EQUAL(IMG2_OK, est);

	const int bad[] = { -2, 2, 5, INT32_MIN, INT32_MAX };
	img2_decode_opts_t o = IMG2_DECODE_OPTS_INIT;
	img2_frames_t fr = { 0 };
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		o.jpeg_deblock = bad[i];
		img2_status_t st = img2_decode(jpg, jlen, &o, &fr);
		img2_frames_free(&fr);
		ASSERT_EQUAL(IMG2_E_ARG, st);
	}
	for (int v = -1; v <= 1; v++) {
		o.jpeg_deblock = v;
		ASSERT_EQUAL(IMG2_OK, img2_decode(jpg, jlen, &o, &fr));
		ASSERT_EQUAL(1, fr.count);
		img2_frames_free(&fr);
	}
	ASSERT_EQUAL(IMG2_OK, img2_decode(jpg, jlen, NULL, &fr)); /* NULL options: the default */
	img2_frames_free(&fr);
	img2_free(jpg);
}
