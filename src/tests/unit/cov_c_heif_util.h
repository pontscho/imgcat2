/**
 * @file cov_c_heif_util.h
 * @brief Coverage lane C: in-test HEIF/AVIF fixture builders on the vendored libheif (header-only, test code)
 *
 * hb_list(): top-level images (optionally with metadata items on the first one); hb_seq(): one visual sequence
 * track; hb_meta_track(): one metadata ('urim') sequence track. The uncompressed ('unci') codec makes them fast.
 */

#ifndef IMG2_TESTS_COV_C_HEIF_UTIL_H
#define IMG2_TESTS_COV_C_HEIF_UTIL_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libheif/heif.h>
#include <libheif/heif_sequences.h>

typedef struct {
	uint8_t *data;
	size_t len;
	size_t cap;
	int failed;
} hb_buf_t;

static inline struct heif_error hb_write(struct heif_context *ctx, const void *data, size_t size, void *userdata)
{
	(void)ctx;
	hb_buf_t *b = userdata;
	if (b->len + size > b->cap) {
		size_t cap = b->cap ? b->cap : 4096;
		while (cap < b->len + size) {
			cap *= 2;
		}
		uint8_t *p = realloc(b->data, cap);
		if (p == NULL) {
			b->failed = 1;
			struct heif_error e = { heif_error_Memory_allocation_error, heif_suberror_Unspecified, "oom" };
			return e;
		}
		b->data = p;
		b->cap = cap;
	}
	memcpy(b->data + b->len, data, size);
	b->len += size;
	struct heif_error ok = { heif_error_Ok, heif_suberror_Unspecified, "" };
	return ok;
}

/* w x h RGB image filled with grey */
static inline struct heif_image *hb_image(int w, int h, int grey)
{
	struct heif_image *img = NULL;
	if (heif_image_create(w, h, heif_colorspace_RGB, heif_chroma_interleaved_RGB, &img).code != heif_error_Ok) {
		return NULL;
	}
	if (heif_image_add_plane(img, heif_channel_interleaved, w, h, 8).code != heif_error_Ok) {
		heif_image_release(img);
		return NULL;
	}
	int stride = 0;
	uint8_t *p = heif_image_get_plane(img, heif_channel_interleaved, &stride);
	for (int y = 0; y < h; y++) {
		memset(p + (size_t)y * (size_t)stride, grey, (size_t)w * 3);
	}
	return img;
}

static inline uint8_t *hb_finish(struct heif_context *ctx, size_t *len)
{
	hb_buf_t b = { 0 };
	struct heif_writer writer;
	writer.writer_api_version = 1;
	writer.write = hb_write;
	struct heif_error err = heif_context_write(ctx, &writer, &b);
	if (err.code != heif_error_Ok || b.failed) {
		free(b.data);
		return NULL;
	}
	*len = b.len;
	return b.data;
}

static inline struct heif_encoder *hb_encoder(struct heif_context *ctx, enum heif_compression_format comp)
{
	struct heif_encoder *enc = NULL;
	if (heif_context_get_encoder_for_format(ctx, comp, &enc).code != heif_error_Ok) {
		return NULL;
	}
	if (comp == heif_compression_AV1) {
		heif_encoder_set_parameter(enc, "speed", "9");
	}
	heif_encoder_set_lossy_quality(enc, 50);
	return enc;
}

/* Metadata item attached to the first image of hb_list() */
typedef struct {
	const char *type; /* item type ("Exif", "mime", ...) */
	const char *content_type;
	const void *data;
	int size;
} hb_meta_t;

/*
 * count top-level w x h images (image i grey 40 + 80 * i) with `comp`; metas[0..nmeta) attached to the first
 * image. Returns a malloc'ed file or NULL.
 */
static inline uint8_t *hb_list(enum heif_compression_format comp, int count, int w, int h, const hb_meta_t *metas, int nmeta, size_t *len)
{
	struct heif_context *ctx = heif_context_alloc();
	struct heif_encoder *enc = hb_encoder(ctx, comp);
	if (enc == NULL) {
		heif_context_free(ctx);
		return NULL;
	}
	int ok = 1;
	for (int i = 0; i < count && ok; i++) {
		struct heif_image *img = hb_image(w, h, (40 + 80 * i) & 0xFF);
		struct heif_image_handle *handle = NULL;
		ok = img != NULL && heif_context_encode_image(ctx, img, enc, NULL, &handle).code == heif_error_Ok;
		heif_image_release(img);
		for (int m = 0; ok && i == 0 && m < nmeta; m++) {
			ok = heif_context_add_generic_metadata(ctx, handle, metas[m].data, metas[m].size, metas[m].type, metas[m].content_type).code == heif_error_Ok;
		}
		heif_image_handle_release(handle);
	}
	uint8_t *d = ok ? hb_finish(ctx, len) : NULL;
	heif_encoder_release(enc);
	heif_context_free(ctx);
	return d;
}

typedef struct {
	enum heif_compression_format comp;
	int count;          /* samples */
	uint32_t timescale; /* track timescale */
	uint32_t reps;      /* heif_context_set_number_of_sequence_repetitions */
	uint32_t duration;  /* per sample, track ticks */
	int still;          /* also one top-level image */
} hb_seq_t;

/* One visual sequence track of 16 x 16 samples (grey 40 + 80 * i), movie timescale 1000 */
static inline uint8_t *hb_seq(const hb_seq_t *p, size_t *len)
{
	struct heif_context *ctx = heif_context_alloc();
	struct heif_encoder *enc = hb_encoder(ctx, p->comp);
	if (enc == NULL) {
		heif_context_free(ctx);
		return NULL;
	}
	int ok = 1;
	if (p->still) {
		struct heif_image *img = hb_image(16, 16, 200);
		ok = heif_context_encode_image(ctx, img, enc, NULL, NULL).code == heif_error_Ok;
		heif_image_release(img);
	}
	heif_context_set_sequence_timescale(ctx, 1000);
	heif_context_set_number_of_sequence_repetitions(ctx, p->reps);
	heif_track_options *topts = heif_track_options_alloc();
	heif_track_options_set_timescale(topts, p->timescale);
	heif_track *track = NULL;
	ok = ok && heif_context_add_visual_sequence_track(ctx, 16, 16, heif_track_type_image_sequence, topts, NULL, &track).code == heif_error_Ok;
	heif_track_options_release(topts);
	for (int i = 0; i < p->count && ok; i++) {
		struct heif_image *img = hb_image(16, 16, (40 + 80 * i) & 0xFF);
		heif_image_set_duration(img, p->duration);
		ok = heif_track_encode_sequence_image(track, img, enc, NULL).code == heif_error_Ok;
		heif_image_release(img);
	}
	/* write before releasing the encoder (libheif 1.23.7 flushes the sequence through it in heif_context_write) */
	uint8_t *d = ok ? hb_finish(ctx, len) : NULL;
	heif_track_release(track);
	heif_encoder_release(enc);
	heif_context_free(ctx);
	return d;
}

/* A metadata ('urim') sequence track only, n raw samples, track timescale `timescale` */
static inline uint8_t *hb_meta_track(const char *const *samples, const size_t *sizes, int n, uint32_t timescale, uint32_t reps, size_t *len)
{
	struct heif_context *ctx = heif_context_alloc();
	heif_context_set_sequence_timescale(ctx, 1000);
	heif_context_set_number_of_sequence_repetitions(ctx, reps);
	heif_track_options *topts = heif_track_options_alloc();
	heif_track_options_set_timescale(topts, timescale);
	heif_track *track = NULL;
	int ok = heif_context_add_uri_metadata_sequence_track(ctx, "urn:x-img2-test", topts, &track).code == heif_error_Ok;
	heif_track_options_release(topts);
	for (int i = 0; i < n && ok; i++) {
		heif_raw_sequence_sample *s = heif_raw_sequence_sample_alloc();
		heif_raw_sequence_sample_set_data(s, (const uint8_t *)samples[i], sizes[i]);
		heif_raw_sequence_sample_set_duration(s, 100);
		ok = heif_track_add_raw_sequence_sample(track, s).code == heif_error_Ok;
		heif_raw_sequence_sample_release(s);
	}
	uint8_t *d = ok ? hb_finish(ctx, len) : NULL;
	heif_track_release(track);
	heif_context_free(ctx);
	return d;
}

#endif /* IMG2_TESTS_COV_C_HEIF_UTIL_H */
