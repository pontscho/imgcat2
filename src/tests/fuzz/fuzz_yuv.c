/**
 * @file fuzz_yuv.c
 * @brief libFuzzer target: raw YUV input (img2_yuv_parse_spec, img2_yuv_frame_size, img2_yuv_format_from_name
 *        and img2_decode with force_mime = IMG2_MIME_YUV) with fuzzed geometry
 *
 * Input layout, byte 0 = mode:
 *   bit 7 set   spec mode: bytes 1.. up to the first '\n' (at most 160 bytes) are a "format:w:h[:ys][:uvs]"
 *               spec (also looked up as a bare format name); the bytes after the '\n' are the frame.
 *   bit 7 clear binary mode: byte 1 = format (% 10, 0 = NONE), then width/height as u16 LE and the y/uv stride
 *               as u16 LE (bit 0 of byte 0 set: width/height as u32 LE and the strides as u64 LE instead, the
 *               full range including overflow edges); the rest is the frame.
 */

#include "fuzz_common.h"

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void)argc;
	(void)argv;
	fuzz_setup_logging();
	return 0;
}

static uint64_t rd_le(const uint8_t *p, size_t n)
{
	uint64_t v = 0;
	for (size_t i = 0; i < n; i++) {
		v |= (uint64_t)p[i] << (8 * i);
	}
	return v;
}

static void decode_frame(const img2_yuv_params_t *p, const uint8_t *frame, size_t len)
{
	img2_decode_opts_t opts = IMG2_DECODE_OPTS_INIT;
	img2_frames_t frames;
	size_t need = 0;
	bool sized = img2_yuv_frame_size(p, &need);

	(void)img2_yuv_format_name(p->format);
	opts.force_mime = IMG2_MIME_YUV;
	opts.yuv = p;
	img2_status_t st = img2_decode(frame, len, &opts, &frames);
	if (st == IMG2_OK) {
		if (!sized || len < need || frames.count != 1 || frames.frames[0]->width != p->width ||
			frames.frames[0]->height != p->height) {
			abort(); /* decoded what img2_yuv_frame_size() rejects or says is too short */
		}
		fuzz_touch_frames(&frames);
		img2_frames_free(&frames);
	} else if (frames.frames != NULL || frames.count != 0) {
		abort();
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	img2_yuv_params_t p;

	if (size < 1) {
		return 0;
	}
	memset(&p, 0, sizeof(p));
	if (data[0] & 0x80) {
		char spec[161];
		size_t n = 0;
		while (1 + n < size && n < sizeof(spec) - 1 && data[1 + n] != '\n') {
			n++;
		}
		if (1 + n >= size || data[1 + n] != '\n') {
			return 0;
		}
		memcpy(spec, data + 1, n);
		spec[n] = '\0';
		(void)img2_yuv_format_from_name(spec);

		char err[96];
		char tiny[1];
		(void)img2_yuv_parse_spec(spec, &p, tiny, sizeof(tiny));
		(void)img2_yuv_parse_spec(spec, &p, NULL, 0);
		memset(&p, 0, sizeof(p));
		if (img2_yuv_parse_spec(spec, &p, err, sizeof(err)) != IMG2_OK) {
			FUZZ_CHECK_CSTR(err);
			return 0;
		}
		decode_frame(&p, data + 2 + n, size - 2 - n);
		return 0;
	}

	bool wide = (data[0] & 1) != 0;
	size_t hdr = wide ? 2 + 4 + 4 + 8 + 8 : 2 + 2 + 2 + 2 + 2;
	if (size < hdr) {
		return 0;
	}
	p.format = (img2_yuv_format_t)(data[1] % 10);
	if (wide) {
		p.width = (uint32_t)rd_le(data + 2, 4);
		p.height = (uint32_t)rd_le(data + 6, 4);
		p.y_stride = (size_t)rd_le(data + 10, 8);
		p.uv_stride = (size_t)rd_le(data + 18, 8);
	} else {
		p.width = (uint32_t)rd_le(data + 2, 2);
		p.height = (uint32_t)rd_le(data + 4, 2);
		p.y_stride = (size_t)rd_le(data + 6, 2);
		p.uv_stride = (size_t)rd_le(data + 8, 2);
	}
	decode_frame(&p, data + hdr, size - hdr);
	return 0;
}
