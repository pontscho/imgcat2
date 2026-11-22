/**
 * @file test_heif_fix_encplugin.c
 * @brief HEIF encoder error paths with a fake HEVC encoder plugin (libheif's public heif_register_encoder_plugin())
 *
 * libheif picks the HEVC encoder with the highest priority, so a plugin registered above kvazaar receives every
 * img2_encode_heif() call of this process (the registry is process-global and survives heif_deinit(), hence
 * this own executable). The plugin can fail where kvazaar never does:
 * - new_encoder: heif_context_get_encoder_for_format() has already created the heif_encoder and returns it
 *   together with the error, so the caller must release it;
 * - set_parameter_quality: heif_encoder_set_lossy_quality() returns the plugin's error unchanged.
 *
 * The plugin hands out a malloc'ed token as its encoder context, also on the failing new_encoder path. libheif
 * frees it (free_encoder) only from the heif_encoder destructor, i.e. only when the heif_encoder is released:
 * a token still alive after img2_encode_heif() returned is a leaked heif_encoder.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libheif/heif.h>
#include <libheif/heif_plugin.h>

#include "../../libimg2/core/image.h"
#include "../../libimg2/encoders/encoder.h"
#include "../ctest.h"

enum { FAIL_NEW = 1, FAIL_QUALITY = 2 };

static int s_fail;
static int s_live_tokens;  /* encoder contexts handed out and not yet freed */
static int s_quality_calls;

static const char *fake_name(void)
{
	return "img2 test fake HEVC encoder";
}

static struct heif_error fake_error(const char *msg)
{
	struct heif_error e = { heif_error_Encoder_plugin_error, heif_suberror_Unspecified, msg };
	return e;
}

static struct heif_error fake_new(void **encoder)
{
	*encoder = malloc(16);
	if (*encoder == NULL) {
		return fake_error("oom");
	}
	s_live_tokens++;
	if (s_fail == FAIL_NEW) {
		return fake_error("fake: new_encoder fails");
	}
	struct heif_error ok = { heif_error_Ok, heif_suberror_Unspecified, "" };
	return ok;
}

static void fake_free(void *encoder)
{
	if (encoder != NULL) {
		s_live_tokens--;
		free(encoder);
	}
}

static struct heif_error fake_set_quality(void *encoder, int quality)
{
	(void)encoder;
	(void)quality;
	s_quality_calls++;
	return fake_error("fake: quality refused");
}

static heif_encoder_plugin k_fake_plugin = {
	.plugin_api_version = 4,
	.compression_format = heif_compression_HEVC,
	.id_name = "img2fake",
	.priority = 10000,
	.supports_lossy_compression = 1,
	.supports_lossless_compression = 0,
	.get_plugin_name = fake_name,
	.new_encoder = fake_new,
	.free_encoder = fake_free,
	.set_parameter_quality = fake_set_quality,
};

static void fake_register(void)
{
	static int registered;
	heif_init(NULL); /* keeps libheif initialised for the whole process: the plugin stays registered */
	if (!registered) {
		ASSERT_EQUAL(heif_error_Ok, heif_register_encoder_plugin(&k_fake_plugin).code);
		registered = 1;
	}
}

static img2_status_t encode_2x2(void)
{
	img2_image_t *img = img2_image_alloc(2, 2);
	ASSERT_NOT_NULL(img);
	memset(img->pixels, 0x80, 2 * 2 * 4);
	uint8_t *out = (uint8_t *)1;
	size_t size = 7;
	img2_status_t st = img2_encode_heif(img, 50, &out, &size);
	ASSERT_NULL(out);
	ASSERT_EQUAL(0, (int)size);
	img2_image_destroy(img);
	return st;
}

CTEST(heif_fix_encplugin, new_encoder_fails_without_leaking)
{
	fake_register();
	s_fail = FAIL_NEW;
	s_live_tokens = 0;
	ASSERT_EQUAL(IMG2_E_CODEC, encode_2x2());
	/* the heif_encoder that libheif returned with the error was released (its destructor freed the token) */
	ASSERT_EQUAL(0, s_live_tokens);
}

CTEST(heif_fix_encplugin, set_quality_fails)
{
	fake_register();
	s_fail = FAIL_QUALITY;
	s_live_tokens = 0;
	s_quality_calls = 0;
	ASSERT_EQUAL(IMG2_E_CODEC, encode_2x2());
	ASSERT_EQUAL(1, s_quality_calls);
	ASSERT_EQUAL(0, s_live_tokens);
}
