/**
 * @file test_heif_fix_decplugin.c
 * @brief HEIF/AVIF decoders with a fake HEVC decoder plugin (libheif's public heif_register_decoder_plugin())
 *
 * libheif takes whatever image a decoder plugin returns. It checks the size only against an item's 'ispe'
 * (skipped when the item has none) and checks sequence frames against nothing, and it passes an image through
 * unchanged when it already is RGB / interleaved RGBA. A plugin registered with a priority above libde265 thus
 * decides what img2 gets: an image without a plane, or a large plane that nobody touches (calloc'ed, so it
 * costs address space but almost no memory).
 *
 * The fixtures are real HEVC files (kvazaar): libheif parses the coded size from the bitstream before it calls
 * the plugin. The plugin registry is process-global and survives heif_deinit(), hence this own executable; the
 * plugin is inactive (priority 0) while s_mode is FAKE_OFF.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libheif/heif.h>
#include <libheif/heif_plugin.h>
#include <libheif/heif_sequences.h>

#include "../../libimg2/core/image.h"
#include "../../libimg2/decoders/decoder.h"
#include "../ctest.h"
#include "cov_c_heif_util.h"

/* ===== fake decoder plugin ===== */

enum { FAKE_OFF = 0, FAKE_ON = 1 };

/* Frame i of a decode run gets s_script[min(i, s_script_len - 1)]; w == 0 means "no plane" */
typedef struct {
	int w;
	int h;
} fake_frame_t;

static int s_mode = FAKE_OFF;
static fake_frame_t s_script[4];
static int s_script_len;
static int s_decoded;     /* images returned in this run */
static int s_alloc_failed; /* a plane could not be allocated (no address space): the test skips */

typedef struct {
	int pending;
	uintptr_t user_data;
} fake_decoder_t;

static const char *fake_name(void)
{
	return "img2 test fake HEVC decoder";
}

static int fake_supports(enum heif_compression_format format)
{
	return (s_mode == FAKE_ON && format == heif_compression_HEVC) ? 10000 : 0;
}

static struct heif_error fake_ok(void)
{
	struct heif_error ok = { heif_error_Ok, heif_suberror_Unspecified, "" };
	return ok;
}

static struct heif_error fake_new(void **decoder)
{
	fake_decoder_t *d = calloc(1, sizeof(*d));
	if (d == NULL) {
		struct heif_error e = { heif_error_Memory_allocation_error, heif_suberror_Unspecified, "oom" };
		return e;
	}
	*decoder = d;
	return fake_ok();
}

static struct heif_error fake_new2(void **decoder, const heif_decoder_plugin_options *options)
{
	(void)options;
	return fake_new(decoder);
}

static void fake_free(void *decoder)
{
	free(decoder);
}

static struct heif_error fake_push(void *decoder, const void *data, size_t size)
{
	(void)data;
	(void)size;
	((fake_decoder_t *)decoder)->pending++;
	return fake_ok();
}

static struct heif_error fake_push2(void *decoder, const void *data, size_t size, uintptr_t user_data)
{
	((fake_decoder_t *)decoder)->user_data = user_data;
	return fake_push(decoder, data, size);
}

static struct heif_error fake_flush(void *decoder)
{
	(void)decoder;
	return fake_ok();
}

static struct heif_error fake_make_image(struct heif_image **out_img)
{
	fake_frame_t f = s_script[s_decoded < s_script_len ? s_decoded : s_script_len - 1];
	s_decoded++;
	int w = f.w > 0 ? f.w : 16;
	int h = f.w > 0 ? f.h : 16;
	struct heif_image *img = NULL;
	struct heif_error err = heif_image_create(w, h, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, &img);
	if (err.code != heif_error_Ok) {
		return err;
	}
	if (f.w > 0) {
		err = heif_image_add_plane(img, heif_channel_interleaved, w, h, 8);
		if (err.code != heif_error_Ok) {
			s_alloc_failed = 1;
			heif_image_release(img);
			return err;
		}
	}
	*out_img = img;
	return fake_ok();
}

static struct heif_error fake_decode_next2(void *decoder, struct heif_image **out_img, uintptr_t *out_user_data, const heif_security_limits *limits)
{
	(void)limits;
	fake_decoder_t *d = decoder;
	*out_img = NULL;
	if (d->pending == 0) {
		return fake_ok(); /* nothing buffered */
	}
	d->pending--;
	if (out_user_data != NULL) {
		*out_user_data = d->user_data;
	}
	return fake_make_image(out_img);
}

static struct heif_error fake_decode_next(void *decoder, struct heif_image **out_img, const heif_security_limits *limits)
{
	return fake_decode_next2(decoder, out_img, NULL, limits);
}

static struct heif_error fake_decode_image(void *decoder, struct heif_image **out_img)
{
	return fake_decode_next2(decoder, out_img, NULL, NULL);
}

static const heif_decoder_plugin k_fake_plugin = {
	.plugin_api_version = 5,
	.get_plugin_name = fake_name,
	.init_plugin = NULL,
	.deinit_plugin = NULL,
	.does_support_format = fake_supports,
	.new_decoder = fake_new,
	.free_decoder = fake_free,
	.push_data = fake_push,
	.decode_image = fake_decode_image,
	.set_strict_decoding = NULL,
	.id_name = "img2fake",
	.decode_next_image = fake_decode_next,
	.minimum_required_libheif_version = 0,
	.does_support_format2 = NULL,
	.new_decoder2 = fake_new2,
	.push_data2 = fake_push2,
	.flush_data = fake_flush,
	.decode_next_image2 = fake_decode_next2,
};

static void fake_register(void)
{
	static int registered;
	heif_init(NULL); /* keeps libheif initialised for the whole process: the plugin stays registered */
	if (!registered) {
		ASSERT_EQUAL(heif_error_Ok, heif_register_decoder_plugin(&k_fake_plugin).code);
		registered = 1;
	}
}

/* Activate the fake plugin with the frames f[0..n) for the next decode run */
static void fake_run(const fake_frame_t *f, int n)
{
	memcpy(s_script, f, (size_t)n * sizeof(*f));
	s_script_len = n;
	s_decoded = 0;
	s_alloc_failed = 0;
	s_mode = FAKE_ON;
}

/* ===== fixtures ===== */

typedef img2_status_t (*hd_fn_t)(const uint8_t *, size_t, const img2_decode_opts_t *, img2_frames_t *);

static const hd_fn_t k_decoders[2] = { img2_decode_heif, img2_decode_avif };

static long find4(const uint8_t *d, size_t len, const char *cc, size_t from)
{
	for (size_t i = from; i + 4 <= len; i++) {
		if (memcmp(d + i, cc, 4) == 0) {
			return (long)i;
		}
	}
	return -1;
}

/* Rename every 'ispe' box to an unknown (non-essential) property: libheif only warns for HEVC items */
static int drop_ispe(uint8_t *d, size_t len)
{
	int n = 0;
	for (long p = find4(d, len, "ispe", 0); p >= 0; p = find4(d, len, "ispe", (size_t)p + 4)) {
		memcpy(d + p, "xspe", 4);
		n++;
	}
	return n;
}

/* count HEVC top-level 16 x 16 images without 'ispe'; NULL if there is no HEVC encoder */
static uint8_t *hevc_list_without_ispe(int count, size_t *len)
{
	s_mode = FAKE_OFF;
	uint8_t *d = hb_list(heif_compression_HEVC, count, 16, 16, NULL, 0, len);
	if (d != NULL) {
		ASSERT_TRUE(drop_ispe(d, *len) > 0);
	}
	return d;
}

/* Decoder k on the file with the fake plugin running f[0..n); returns the status (frames freed) */
static img2_status_t fake_decode(int k, const uint8_t *d, size_t len, const fake_frame_t *f, int n, int *count)
{
	fake_run(f, n);
	img2_frames_t fr = { 0 };
	img2_status_t st = k_decoders[k](d, len, NULL, &fr);
	s_mode = FAKE_OFF;
	*count = fr.count;
	img2_frames_free(&fr);
	return st;
}

/* ===== still image ===== */

CTEST(heif_fix_decplugin, still_without_ispe)
{
	fake_register();
	size_t len = 0;
	uint8_t *d = hevc_list_without_ispe(1, &len);
	if (d == NULL) {
		fprintf(stderr, "skipped: no HEVC encoder\n");
		return;
	}
	ASSERT_EQUAL(IMG2_HEIF_LAYOUT_SINGLE, img2_heif_layout(d, len, NULL));
	int count = 0;
	for (int k = 0; k < 2; k++) {
		/* the real decoder (libde265): no 'ispe' is only a warning, the image decodes */
		img2_frames_t fr = { 0 };
		ASSERT_EQUAL(IMG2_OK, k_decoders[k](d, len, NULL, &fr));
		ASSERT_EQUAL(1, fr.count);
		ASSERT_EQUAL_U(16u, fr.frames[0]->width);
		img2_frames_free(&fr);

		/* the plugin's image has no interleaved plane: CODEC */
		static const fake_frame_t noplane[1] = { { 0, 0 } };
		ASSERT_EQUAL(IMG2_E_CODEC, fake_decode(k, d, len, noplane, 1, &count));
		ASSERT_EQUAL(1, s_decoded);

		/* 20000 x 20000 (4e8 pixels > IMG2_MAX_PIXELS, < libheif's 2^30): img2 still applies its own limit */
		static const fake_frame_t big[1] = { { 20000, 20000 } };
		img2_status_t st = fake_decode(k, d, len, big, 1, &count);
		if (s_alloc_failed) {
			fprintf(stderr, "skipped: no address space for a 1.6 GB plane\n");
		} else {
			ASSERT_EQUAL(IMG2_E_LIMIT, st);
			ASSERT_EQUAL(0, count);
		}
	}
	free(d);
}

/* ===== top-level image list ===== */

CTEST(heif_fix_decplugin, list_without_ispe)
{
	fake_register();
	size_t len = 0;
	uint8_t *d = hevc_list_without_ispe(2, &len);
	if (d == NULL) {
		fprintf(stderr, "skipped: no HEVC encoder\n");
		return;
	}
	ASSERT_EQUAL(IMG2_HEIF_LAYOUT_LIST, img2_heif_layout(d, len, NULL));
	int count = 0;
	for (int k = 0; k < 2; k++) {
		img2_frames_t fr = { 0 };
		ASSERT_EQUAL(IMG2_OK, k_decoders[k](d, len, NULL, &fr));
		ASSERT_EQUAL(2, fr.count);
		img2_frames_free(&fr);

		/* second image without a plane: CODEC after the first one was decoded */
		static const fake_frame_t noplane[2] = { { 16, 16 }, { 0, 0 } };
		ASSERT_EQUAL(IMG2_E_CODEC, fake_decode(k, d, len, noplane, 2, &count));
		ASSERT_EQUAL(2, s_decoded);

		/* the handles report 0 x 0, so the pre-check cannot see the sizes; the running total of the decoded
		 * images can: 16 x 16 + 32768 x 32768 > 2^30 (the 4 GiB plane is calloc'ed and never touched) */
		static const fake_frame_t huge[2] = { { 16, 16 }, { 32768, 32768 } };
		img2_status_t st = fake_decode(k, d, len, huge, 2, &count);
		if (s_alloc_failed) {
			fprintf(stderr, "skipped: no address space for a 4 GiB plane\n");
		} else {
			ASSERT_EQUAL(IMG2_E_LIMIT, st);
			ASSERT_EQUAL(2, s_decoded);
		}
	}
	free(d);
}

/* ===== sequence track ===== */

CTEST(heif_fix_decplugin, sequence_frames)
{
	fake_register();
	s_mode = FAKE_OFF;
	hb_seq_t p = { heif_compression_HEVC, 2, 1000, 1, 100, 0 };
	size_t len = 0;
	uint8_t *d = hb_seq(&p, &len);
	if (d == NULL) {
		fprintf(stderr, "skipped: no HEVC sequence encoder\n");
		return;
	}
	ASSERT_EQUAL(IMG2_HEIF_LAYOUT_SEQUENCE, img2_heif_layout(d, len, NULL));
	int count = 0;
	for (int k = 0; k < 2; k++) {
		img2_frames_t fr = { 0 };
		ASSERT_EQUAL(IMG2_OK, k_decoders[k](d, len, NULL, &fr));
		ASSERT_EQUAL(2, fr.count);
		img2_frames_free(&fr);

		/* the plugin's frame matches the sample entry (16 x 16) but has no plane: CODEC */
		static const fake_frame_t noplane[1] = { { 0, 0 } };
		ASSERT_EQUAL(IMG2_E_CODEC, fake_decode(k, d, len, noplane, 1, &count));
		ASSERT_EQUAL(1, s_decoded);

		/* frames are not checked against the 16 x 16 sample entry: the second one is 32768 x 32768, which
		 * the running total rejects before img2 allocates anything for it */
		static const fake_frame_t huge[2] = { { 16, 16 }, { 32768, 32768 } };
		img2_status_t st = fake_decode(k, d, len, huge, 2, &count);
		if (s_alloc_failed) {
			fprintf(stderr, "skipped: no address space for a 4 GiB plane\n");
		} else {
			ASSERT_EQUAL(IMG2_E_LIMIT, st);
			ASSERT_EQUAL(2, s_decoded);
		}

		/* an oversized frame below 2^30 (20000 x 20000): the per-frame limit of the allocation */
		static const fake_frame_t big[2] = { { 16, 16 }, { 20000, 20000 } };
		st = fake_decode(k, d, len, big, 2, &count);
		if (!s_alloc_failed) {
			ASSERT_EQUAL(IMG2_E_LIMIT, st);
		}
	}
	free(d);
}
