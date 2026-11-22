/**
 * @file test_cov_c_heif.c
 * @brief Coverage lane C: HEIF / AVIF decoders (still, image list, sequence track, metadata) and the HEIF encoder
 *
 * White-box: img2_decode_heif() and img2_decode_avif() are called directly, so both decoders run on the same
 * fixtures whatever codec the file uses. Fixtures are built in-test with the vendored libheif, mostly with its
 * built-in uncompressed ('unci') codec (fast, exact pixels) and then byte-patched where a malformed or extreme
 * header is needed (ispe, item type, sample entry size, stts delta, truncation). AV1 (aom, speed 9) is used
 * where a codec must reject its data.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libheif/heif.h>
#include <libheif/heif_sequences.h>

#include "../../libimg2/core/image.h"
#include "../../libimg2/decoders/decoder.h"
#include "../../libimg2/encoders/encoder.h"
#include "../../libimg2/metadata/exif_reader.h"
#include "../ctest.h"
#include "cov_c_heif_util.h"
#include "cov_c_util.h"

/* ===== decode helpers ===== */

typedef img2_status_t (*hd_fn_t)(const uint8_t *, size_t, const img2_decode_opts_t *, img2_frames_t *);

static const hd_fn_t k_decoders[2] = { img2_decode_heif, img2_decode_avif };

/* Both decoders must return `st` (and `count` frames on success) */
static void hd_expect(const uint8_t *data, size_t len, img2_status_t st, int count)
{
	for (int k = 0; k < 2; k++) {
		img2_frames_t fr = { 0 };
		ASSERT_EQUAL(st, k_decoders[k](data, len, NULL, &fr));
		ASSERT_EQUAL(st == IMG2_OK ? count : 0, fr.count);
		img2_frames_free(&fr);
	}
}

/* Decode with decoder k, expect one frame; returns the frames (caller frees) */
static img2_frames_t hd_one(int k, const uint8_t *data, size_t len)
{
	img2_frames_t fr = { 0 };
	ASSERT_EQUAL(IMG2_OK, k_decoders[k](data, len, NULL, &fr));
	ASSERT_EQUAL(1, fr.count);
	return fr;
}

/* ===== byte patching ===== */

static long find4(const uint8_t *d, size_t len, const char *cc, size_t from)
{
	for (size_t i = from; i + 4 <= len; i++) {
		if (memcmp(d + i, cc, 4) == 0) {
			return (long)i;
		}
	}
	return -1;
}

static long find4_last(const uint8_t *d, size_t len, const char *cc)
{
	long last = -1;
	for (long p = find4(d, len, cc, 0); p >= 0; p = find4(d, len, cc, (size_t)p + 1)) {
		last = p;
	}
	return last;
}

/* every 'ispe' box: width x height */
static void patch_ispe(uint8_t *d, size_t len, uint32_t w, uint32_t h)
{
	for (long p = find4(d, len, "ispe", 0); p >= 0; p = find4(d, len, "ispe", (size_t)p + 4)) {
		cov_put_be32(d + p + 8, w);
		cov_put_be32(d + p + 12, h);
	}
}

/* the 'uncv' visual sample entry's width / height (VisualSampleEntry: +24 / +26 after the 4cc) */
static void patch_sample_entry_size(uint8_t *d, size_t len, uint16_t w, uint16_t h)
{
	long p = find4(d, len, "uncv", 0);
	ASSERT_TRUE(p > 0);
	cov_put_be16(d + p + 28, w);
	cov_put_be16(d + p + 30, h);
}

/* ===== argument checks ===== */

CTEST(cov_c_heif, argument_checks)
{
	img2_frames_t fr = { 0 };
	const uint8_t b[4] = { 0 };
	for (int k = 0; k < 2; k++) {
		ASSERT_EQUAL(IMG2_E_ARG, k_decoders[k](b, sizeof(b), NULL, NULL));
		ASSERT_EQUAL(IMG2_E_ARG, k_decoders[k](NULL, 4, NULL, &fr));
		ASSERT_EQUAL(IMG2_E_ARG, k_decoders[k](b, 0, NULL, &fr));
		ASSERT_NULL(fr.frames);
	}

	int hint = 7;
	ASSERT_EQUAL(IMG2_HEIF_LAYOUT_SINGLE, img2_heif_layout(NULL, 4, &hint));
	ASSERT_EQUAL(1, hint);
	ASSERT_EQUAL(IMG2_HEIF_LAYOUT_SINGLE, img2_heif_layout(b, 0, NULL));
	ASSERT_EQUAL(IMG2_HEIF_LAYOUT_SINGLE, img2_heif_layout(b, sizeof(b), NULL)); /* unreadable */
	ASSERT_FALSE(img2_heif_is_animated(b, sizeof(b)));

	/* img2_heif_decode_sequence: every argument, an unreadable file, a file without a track */
	int fc = 0;
	int lc = 0;
	img2_status_t st = IMG2_OK;
	ASSERT_NULL(img2_heif_decode_sequence(NULL, 4, "X", &fc, &lc, &st));
	ASSERT_EQUAL(IMG2_E_ARG, st);
	ASSERT_NULL(img2_heif_decode_sequence(b, 0, "X", &fc, &lc, &st));
	ASSERT_NULL(img2_heif_decode_sequence(b, 4, NULL, &fc, &lc, &st));
	ASSERT_NULL(img2_heif_decode_sequence(b, 4, "X", NULL, &lc, &st));
	ASSERT_NULL(img2_heif_decode_sequence(b, 4, "X", &fc, NULL, &st));
	ASSERT_EQUAL(IMG2_E_ARG, st);
	ASSERT_NULL(img2_heif_decode_sequence(b, 4, "X", &fc, &lc, &st));
	ASSERT_EQUAL(IMG2_E_CODEC, st);

	heif_init(NULL);
	size_t len = 0;
	uint8_t *still = hb_list(heif_compression_uncompressed, 1, 16, 16, NULL, 0, &len);
	ASSERT_NOT_NULL(still);
	st = IMG2_OK;
	ASSERT_NULL(img2_heif_decode_sequence(still, len, "X", &fc, &lc, &st));
	ASSERT_EQUAL(IMG2_E_CODEC, st);

	/* unreadable data through both still decoders */
	hd_expect(b, sizeof(b), IMG2_E_CODEC, 0);
	free(still);
	heif_deinit();
}

/* ===== still image ===== */

CTEST(cov_c_heif, still_errors)
{
	heif_init(NULL);
	size_t len = 0;
	uint8_t *d = hb_list(heif_compression_uncompressed, 1, 16, 16, NULL, 0, &len);
	ASSERT_NOT_NULL(d);
	hd_expect(d, len, IMG2_OK, 1);

	/* image data cut short: heif_decode_image fails */
	hd_expect(d, len - 1, IMG2_E_CODEC, 0);

	/* the only (primary) item becomes an unsupported type: no primary image handle */
	long p = find4_last(d, len, "unci");
	ASSERT_TRUE(p > 0);
	memcpy(d + p, "lhv1", 4);
	hd_expect(d, len, IMG2_E_CODEC, 0);
	free(d);
	heif_deinit();
}

/* ===== still image metadata ===== */

/* EXIF / XMP extraction of the still decoders (exif_reader.h fields) */
typedef struct {
	int has_exif;
	int has_xmp;
	char make[32];
	char creator[32];
	char title[32];
} meta_seen_t;

static meta_seen_t meta_decode(int k, const hb_meta_t *metas, int n)
{
	size_t len = 0;
	uint8_t *d = hb_list(heif_compression_uncompressed, 1, 16, 16, metas, n, &len);
	ASSERT_NOT_NULL(d);
	img2_frames_t fr = hd_one(k, d, len);
	meta_seen_t s = { 0 };
	const img2_image_t *img = fr.frames[0];
	if (img->exif != NULL) {
		s.has_exif = 1;
		snprintf(s.make, sizeof(s.make), "%.31s", img->exif->make);
	}
	if (img->xmp != NULL) {
		s.has_xmp = 1;
		snprintf(s.creator, sizeof(s.creator), "%.31s", img->xmp->creator);
		snprintf(s.title, sizeof(s.title), "%.31s", img->xmp->title);
	}
	img2_frames_free(&fr);
	free(d);
	return s;
}

/* "Exif" item with the given prefix in front of a TIFF block carrying the fields in mask */
static int exif_item(uint8_t *out, const void *prefix, size_t plen, unsigned mask)
{
	memcpy(out, prefix, plen);
	return (int)(plen + cov_tiff_build(out + plen, mask, 0));
}

CTEST(cov_c_heif, exif_fields_keep_or_drop)
{
	heif_init(NULL);
	/* each field alone keeps the EXIF block, none of them drops it (EXIF with only a compression tag) */
	static const unsigned fields[] = { COV_T_MAKE, COV_T_MODEL, COV_T_SOFTWARE, COV_T_DESC, COV_T_ISO, COV_T_GPS, COV_T_EXPO, COV_T_FNUM, COV_T_FOCAL, COV_T_ORIENT6 };
	static const uint8_t pre[10] = { 0, 0, 0, 6, 'E', 'x', 'i', 'f', 0, 0 };
	uint8_t buf[1024];
	for (int k = 0; k < 2; k++) {
		for (size_t f = 0; f < sizeof(fields) / sizeof(fields[0]); f++) {
			hb_meta_t m = { "Exif", NULL, buf, exif_item(buf, pre, sizeof(pre), fields[f]) };
			meta_seen_t s = meta_decode(k, &m, 1);
			ASSERT_EQUAL(1, s.has_exif);
			if (fields[f] == COV_T_MAKE) {
				ASSERT_STR("Make1", s.make);
			}
		}
		hb_meta_t none = { "Exif", NULL, buf, exif_item(buf, pre, sizeof(pre), COV_T_ORIENT1) };
		ASSERT_EQUAL(0, meta_decode(k, &none, 1).has_exif);
	}
	heif_deinit();
}

CTEST(cov_c_heif, exif_payload_layouts)
{
	heif_init(NULL);
	uint8_t buf[1024];
	/* offset prefix pointing past the block: the whole block is parsed (no TIFF magic -> dropped) */
	static const uint8_t far_off[10] = { 0xFF, 0xFF, 0xFF, 0x00, 'E', 'x', 'i', 'f', 0, 0 };
	/* "Exif" marker variants that do not match, then a TIFF that is not at offset 0 or 4 */
	static const uint8_t no_e[10] = { 0, 0, 0, 6, 'X', 'x', 'i', 'f', 0, 0 };
	static const uint8_t no_x[10] = { 0, 0, 0, 6, 'E', 'X', 'i', 'f', 0, 0 };
	static const uint8_t no_i[10] = { 0, 0, 0, 6, 'E', 'x', 'X', 'f', 0, 0 };
	static const uint8_t no_f[10] = { 0, 0, 0, 6, 'E', 'x', 'i', 'X', 0, 0 };
	/* legacy: TIFF magic at offset 4 behind a 4-byte prefix */
	static const uint8_t at4[4] = { 0, 0, 0, 0 };
	for (int k = 0; k < 2; k++) {
		hb_meta_t m = { "Exif", NULL, buf, exif_item(buf, far_off, sizeof(far_off), COV_T_MAKE) };
		ASSERT_EQUAL(0, meta_decode(k, &m, 1).has_exif);
		const uint8_t *const bad_markers[] = { no_e, no_x, no_i, no_f };
		for (size_t i = 0; i < 4; i++) {
			m.size = exif_item(buf, bad_markers[i], 10, COV_T_MAKE);
			ASSERT_EQUAL(0, meta_decode(k, &m, 1).has_exif);
		}
		/* TIFF at 0, little and big endian */
		m.size = exif_item(buf, "", 0, COV_T_MAKE);
		ASSERT_EQUAL(1, meta_decode(k, &m, 1).has_exif);
		m.size = (int)cov_tiff_build(buf, COV_T_MAKE, 1);
		meta_seen_t s = meta_decode(k, &m, 1);
		ASSERT_EQUAL(1, s.has_exif);
		ASSERT_STR("Make1", s.make);
		/* TIFF at 4, little and big endian */
		m.size = exif_item(buf, at4, 4, COV_T_MAKE);
		ASSERT_EQUAL(1, meta_decode(k, &m, 1).has_exif);
		memcpy(buf, at4, 4);
		m.size = 4 + (int)cov_tiff_build(buf + 4, COV_T_MAKE, 1);
		ASSERT_EQUAL(1, meta_decode(k, &m, 1).has_exif);
		/* half-matching magics at 0 and at 4: "IX", "MX", nothing at all; blocks of 2 and 6 bytes */
		static const char *const junk[] = { "IXabcdefghijkl", "MXabcdefghijkl", "abcdIXefghijkl", "abcdMXefghijkl", "abcdZZefghijkl" };
		for (size_t i = 0; i < 5; i++) {
			m.data = junk[i];
			m.size = 14;
			ASSERT_EQUAL(0, meta_decode(k, &m, 1).has_exif);
		}
		m.data = "ab";
		m.size = 2;
		ASSERT_EQUAL(0, meta_decode(k, &m, 1).has_exif);
		m.data = "abcdef";
		m.size = 6;
		ASSERT_EQUAL(0, meta_decode(k, &m, 1).has_exif);
		m.data = buf;
		/* an empty "Exif" item is skipped */
		m.size = 0;
		ASSERT_EQUAL(0, meta_decode(k, &m, 1).has_exif);
		/* a failed parse, then a second "Exif" item: still no EXIF */
		hb_meta_t two[2] = { { "Exif", NULL, "abcdefgh", 8 }, { "Exif", NULL, buf, exif_item(buf, "", 0, COV_T_MAKE) } };
		ASSERT_EQUAL(0, meta_decode(k, two, 2).has_exif);
	}
	heif_deinit();
}

CTEST(cov_c_heif, xmp_items)
{
	heif_init(NULL);
	static const char creator[] = "<x:xmpmeta><rdf:Description><dc:creator>Ann</dc:creator></rdf:Description></x:xmpmeta>";
	static const char title[] = "<x:xmpmeta><rdf:Description><dc:title>Tea</dc:title></rdf:Description></x:xmpmeta>";
	static const char other[] = "<x:xmpmeta><rdf:Description><dc:rights>R</dc:rights></rdf:Description></x:xmpmeta>";
	static const char broken[] = "<x:xmpmeta><rdf:Descr";
	for (int k = 0; k < 2; k++) {
		hb_meta_t m = { "mime", "application/rdf+xml", creator, (int)strlen(creator) };
		meta_seen_t s = meta_decode(k, &m, 1);
		ASSERT_EQUAL(1, s.has_xmp);
		ASSERT_STR("Ann", s.creator);
		m.data = title;
		m.size = (int)strlen(title);
		s = meta_decode(k, &m, 1);
		ASSERT_EQUAL(1, s.has_xmp);
		ASSERT_STR("Tea", s.title);
		m.data = other;
		m.size = (int)strlen(other);
		ASSERT_EQUAL(0, meta_decode(k, &m, 1).has_xmp);
		/* empty, unparsable, then a valid one after the failure */
		m.size = 0;
		ASSERT_EQUAL(0, meta_decode(k, &m, 1).has_xmp);
		hb_meta_t two[2] = { { "mime", "application/rdf+xml", broken, (int)strlen(broken) }, { "mime", "application/rdf+xml", creator, (int)strlen(creator) } };
		ASSERT_EQUAL(0, meta_decode(k, two, 2).has_xmp);
		/* other content type, other item type */
		hb_meta_t m2[2] = { { "mime", "text/plain", creator, (int)strlen(creator) }, { "abcd", NULL, "zz", 2 } };
		s = meta_decode(k, m2, 2);
		ASSERT_EQUAL(0, s.has_xmp);
		ASSERT_EQUAL(0, s.has_exif);
	}
	heif_deinit();
}

/* ===== top-level image lists ===== */

CTEST(cov_c_heif, list_limits_and_errors)
{
	heif_init(NULL);
	size_t len = 0;
	uint8_t *d = hb_list(heif_compression_uncompressed, 2, 16, 16, NULL, 0, &len);
	ASSERT_NOT_NULL(d);
	ASSERT_EQUAL(IMG2_HEIF_LAYOUT_LIST, img2_heif_layout(d, len, NULL));
	hd_expect(d, len, IMG2_OK, 2);

	/* [F14] headers claiming 2 x 30000 x 30000 (> IMG2_MAX_TOTAL_PIXELS): LIMIT before anything is decoded */
	uint8_t *c = malloc(len);
	ASSERT_NOT_NULL(c);
	memcpy(c, d, len);
	patch_ispe(c, len, 30000, 30000);
	hd_expect(c, len, IMG2_E_LIMIT, 0);

	/* second item of an unsupported type: its handle cannot be opened (pre-check) */
	memcpy(c, d, len);
	long p = find4_last(c, len, "unci");
	ASSERT_TRUE(p > 0);
	memcpy(c + p, "lhv1", 4);
	hd_expect(c, len, IMG2_E_CODEC, 0);
	free(c);

	/* second image's data cut short: the first frame is decoded, then everything is released */
	hd_expect(d, len - 1, IMG2_E_CODEC, 0);
	free(d);
	heif_deinit();
}

CTEST(cov_c_heif, list_frame_cap)
{
	heif_init(NULL);
	size_t len = 0;
	uint8_t *d = hb_list(heif_compression_uncompressed, 201, 1, 1, NULL, 0, &len);
	ASSERT_NOT_NULL(d);
	/* 201 top-level images: the first 200 */
	hd_expect(d, len, IMG2_OK, 200);
	free(d);
	heif_deinit();
}

/* ===== sequence tracks ===== */

CTEST(cov_c_heif, sequence_track_resolution)
{
	heif_init(NULL);
	hb_seq_t p = { heif_compression_uncompressed, 2, 1000, 1, 100, 0 };
	size_t len = 0;
	uint8_t *d = hb_seq(&p, &len);
	ASSERT_NOT_NULL(d);
	ASSERT_EQUAL(IMG2_HEIF_LAYOUT_SEQUENCE, img2_heif_layout(d, len, NULL));
	hd_expect(d, len, IMG2_OK, 2);

	uint8_t *c = malloc(len);
	ASSERT_NOT_NULL(c);
	/* sample entry claims 65535 x 65535: LIMIT before the first sample is decoded */
	memcpy(c, d, len);
	patch_sample_entry_size(c, len, 65535, 65535);
	hd_expect(c, len, IMG2_E_LIMIT, 0);
	/* zero width / height: the size check is skipped, the sample then fails to decode */
	memcpy(c, d, len);
	patch_sample_entry_size(c, len, 0, 16);
	hd_expect(c, len, IMG2_E_CODEC, 0);
	memcpy(c, d, len);
	patch_sample_entry_size(c, len, 16, 0);
	hd_expect(c, len, IMG2_E_CODEC, 0);
	free(c);

	/* second sample cut short: frame 0 is decoded, then the decode fails as a whole */
	hd_expect(d, len - 1, IMG2_E_CODEC, 0);

	/* cut right after the mdat header: no sample can be read, so it is not a sequence; no primary image */
	long m = find4(d, len, "mdat", 0);
	ASSERT_TRUE(m > 0);
	ASSERT_EQUAL(IMG2_HEIF_LAYOUT_SINGLE, img2_heif_layout(d, (size_t)m + 4, NULL));
	hd_expect(d, (size_t)m + 4, IMG2_E_CODEC, 0);
	free(d);
	heif_deinit();
}

CTEST(cov_c_heif, sequence_timing_edges)
{
	heif_init(NULL);
	size_t len = 0;
	img2_frames_t fr;

	/* track timescale 0: no delays */
	hb_seq_t p = { heif_compression_uncompressed, 2, 0, 1, 100, 0 };
	uint8_t *d = hb_seq(&p, &len);
	ASSERT_NOT_NULL(d);
	for (int k = 0; k < 2; k++) {
		ASSERT_EQUAL(IMG2_OK, k_decoders[k](d, len, NULL, &fr));
		ASSERT_EQUAL(2, fr.count);
		ASSERT_EQUAL_U(0u, fr.frames[0]->delay_ms);
		img2_frames_free(&fr);
	}
	free(d);

	/* track timescale 1 tick/s, 5e6 ticks: clamped to UINT32_MAX ms */
	hb_seq_t big = { heif_compression_uncompressed, 2, 1, 1, 5000000u, 0 };
	d = hb_seq(&big, &len);
	ASSERT_NOT_NULL(d);
	for (int k = 0; k < 2; k++) {
		ASSERT_EQUAL(IMG2_OK, k_decoders[k](d, len, NULL, &fr));
		ASSERT_EQUAL_U(UINT32_MAX, fr.frames[1]->delay_ms);
		img2_frames_free(&fr);
	}
	free(d);

	/* 0x90000000 repetitions (> INT_MAX, not the 'infinite' value): loop INT_MAX */
	hb_seq_t reps = { heif_compression_uncompressed, 2, 1000, 0x90000000u, 10, 0 };
	d = hb_seq(&reps, &len);
	ASSERT_NOT_NULL(d);
	for (int k = 0; k < 2; k++) {
		ASSERT_EQUAL(IMG2_OK, k_decoders[k](d, len, NULL, &fr));
		ASSERT_EQUAL(2147483647, fr.loop_count);
		img2_frames_free(&fr);
	}
	free(d);

	/* a sample duration of 0 ticks (stts delta patched; libheif refuses to write it): no delay */
	hb_seq_t one = { heif_compression_uncompressed, 2, 1000, 1, 100, 0 };
	d = hb_seq(&one, &len);
	ASSERT_NOT_NULL(d);
	long s = find4(d, len, "stts", 0);
	ASSERT_TRUE(s > 0);
	memset(d + s + 16, 0, 4); /* entry 0 sample_delta */
	for (int k = 0; k < 2; k++) {
		ASSERT_EQUAL(IMG2_OK, k_decoders[k](d, len, NULL, &fr));
		ASSERT_EQUAL_U(0u, fr.frames[0]->delay_ms);
		img2_frames_free(&fr);
	}
	free(d);
	heif_deinit();
}

CTEST(cov_c_heif, sequence_sample_probe)
{
	heif_init(NULL);
	size_t len = 0;
	int hint = 0;

	/* one sample played once: the second raw read ends the sequence -> 1 sample, not animated */
	hb_seq_t p = { heif_compression_uncompressed, 1, 1000, 1, 100, 0 };
	uint8_t *d = hb_seq(&p, &len);
	ASSERT_NOT_NULL(d);
	ASSERT_EQUAL(IMG2_HEIF_LAYOUT_SEQUENCE, img2_heif_layout(d, len, &hint));
	ASSERT_EQUAL(1, hint);
	ASSERT_FALSE(img2_heif_is_animated(d, len));
	free(d);

	/* a still plus a one-sample track: the still; a still plus two samples: the track */
	hb_seq_t s1 = { heif_compression_uncompressed, 1, 1000, 1, 100, 1 };
	d = hb_seq(&s1, &len);
	ASSERT_NOT_NULL(d);
	ASSERT_EQUAL(IMG2_HEIF_LAYOUT_SINGLE, img2_heif_layout(d, len, NULL));
	hd_expect(d, len, IMG2_OK, 1);
	free(d);
	hb_seq_t s2 = { heif_compression_uncompressed, 2, 1000, 1, 100, 1 };
	d = hb_seq(&s2, &len);
	ASSERT_NOT_NULL(d);
	ASSERT_EQUAL(IMG2_HEIF_LAYOUT_SEQUENCE, img2_heif_layout(d, len, NULL));
	free(d);

	/* metadata track (not visual): counted by the probe, rejected by the decoder */
	static const char *const same[2] = { "abcd", "abcd" };
	static const char *const longer[2] = { "abcd", "abcde" };
	static const char *const differ[2] = { "abcd", "abce" };
	static const char *const empty[2] = { "", "" };
	static const char *const second_empty[2] = { "abcd", "" };
	static const size_t sz44[2] = { 4, 4 };
	static const size_t sz45[2] = { 4, 5 };
	static const size_t sz00[2] = { 0, 0 };
	static const size_t sz40[2] = { 4, 0 };
	struct {
		const char *const *s;
		const size_t *sz;
		uint32_t timescale;
		uint32_t reps;
		int expect;
	} cases[] = {
		{ same, sz44, 1000, 3, 1 },     /* repeated edit list, identical samples: one sample */
		{ longer, sz45, 1000, 3, 2 },   /* sizes differ */
		{ differ, sz44, 1000, 3, 2 },   /* same size, other bytes */
		{ empty, sz00, 1000, 3, 2 },    /* no data to compare */
		{ second_empty, sz40, 1000, 3, 2 },
		{ same, sz44, 1000, 1, 2 },     /* played once: never collapsed */
		{ same, sz44, 90000, 3, 2 },    /* edit list not interpreted (0 repetitions) */
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		d = hb_meta_track(cases[i].s, cases[i].sz, 2, cases[i].timescale, cases[i].reps, &len);
		ASSERT_NOT_NULL(d);
		ASSERT_EQUAL(IMG2_HEIF_LAYOUT_SEQUENCE, img2_heif_layout(d, len, &hint));
		ASSERT_EQUAL(cases[i].expect, hint);
		hd_expect(d, len, IMG2_E_CODEC, 0);
		free(d);
	}
	heif_deinit();
}

CTEST(cov_c_heif, sequence_codec_error)
{
	heif_init(NULL);
	/* AV1 samples zeroed: dav1d/aom rejects the first one */
	hb_seq_t p = { heif_compression_AV1, 2, 1000, 1, 100, 0 };
	size_t len = 0;
	uint8_t *d = hb_seq(&p, &len);
	if (d == NULL) {
		heif_deinit();
		fprintf(stderr, "skipped: no AV1 encoder\n");
		return;
	}
	long m = find4(d, len, "mdat", 0);
	ASSERT_TRUE(m > 0 && (size_t)m + 4 < len);
	memset(d + m + 4, 0, len - (size_t)m - 4);
	hd_expect(d, len, IMG2_E_CODEC, 0);
	free(d);
	heif_deinit();
}

/* ===== HEIF encoder ===== */

CTEST(cov_c_heif, encoder_quality_clamp_and_plane_error)
{
	img2_image_t *img = img2_image_alloc(16, 16);
	ASSERT_NOT_NULL(img);
	memset(img->pixels, 0x80, (size_t)16 * 16 * 4);
	uint8_t *out = NULL;
	size_t size = 0;
	ASSERT_EQUAL(IMG2_OK, img2_encode_heif(img, -5, &out, &size));
	ASSERT_TRUE(size > 0);
	free(out);
	ASSERT_EQUAL(IMG2_OK, img2_encode_heif(img, 101, &out, &size));
	ASSERT_TRUE(size > 0);
	free(out);

	/* a width libheif refuses to allocate a plane for (0; the pixels are never read) */
	img2_image_t zero = *img;
	zero.width = 0;
	ASSERT_EQUAL(IMG2_E_CODEC, img2_encode_heif(&zero, 50, &out, &size));
	ASSERT_NULL(out);
	img2_image_destroy(img);
}

CTEST(cov_c_heif, encoder_output_over_64k)
{
	/* noise compresses badly: the output grows the 64 KiB write buffer */
	img2_image_t *img = img2_image_alloc(160, 160);
	ASSERT_NOT_NULL(img);
	cov_fill_noise(img->pixels, (size_t)160 * 160 * 4, 12345u);
	uint8_t *out = NULL;
	size_t size = 0;
	ASSERT_EQUAL(IMG2_OK, img2_encode_heif(img, 100, &out, &size));
	ASSERT_TRUE(size > 65536);
	free(out);
	img2_image_destroy(img);
}
