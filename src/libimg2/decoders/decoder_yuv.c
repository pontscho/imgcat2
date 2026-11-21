/**
 * @file decoder_yuv.c
 * @brief Raw 8-bit YUV decoder (i420, yv12, nv12, nv21, i422, i444, yuyv, uyvy, gray)
 *
 * Raw YUV carries no header, so the geometry comes from yuv_params_t
 * (CLI: --yuv format:w:h[:y_stride][:uv_stride]). See yuv.h for the memory
 * layout contract.
 *
 * Design notes:
 * - Size requirement is lenient: every plane occupies stride * rows bytes,
 *   except the very last row of the last plane, of which only the visible
 *   bytes must be present (e.g. a frame cropped right after the last pixel).
 * - Only the first frame is decoded; trailing bytes are ignored.
 * - All size arithmetic is size_t with overflow checks before use.
 * - Conversion is BT.601 limited range with 16.16 fixed-point coefficients and
 *   nearest (replicated) chroma upsampling. Gray uses the same limited-range
 *   luma expansion so a Y plane looks identical to the color frame's luma.
 */

/* clang-format off */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

#include "yuv.h"
#include "../core/log.h"

/** Maximum accepted length of a --yuv specification string */
#define YUV_SPEC_MAX_LEN 128

/** Maximum number of ':'-separated fields in a specification */
#define YUV_SPEC_MAX_FIELDS 5

/** Expected specification syntax (for error messages) */
#define YUV_SPEC_SYNTAX "format:width:height[:y_stride][:uv_stride]"

/** Supported names (for error messages) */
#define YUV_SPEC_FORMATS "i420/yuv420p, yv12, nv12, nv21, i422/yuv422p, i444/yuv444p, yuyv/yuy2, uyvy, gray/y8/y800"

/**
 * BT.601 limited-range YCbCr -> RGB coefficients in 16.16 fixed point:
 *   R = 1.164383 (Y-16)                    + 1.596027 (V-128)
 *   G = 1.164383 (Y-16) - 0.391762 (U-128) - 0.812968 (V-128)
 *   B = 1.164383 (Y-16) + 2.017232 (U-128)
 */
#define YUV_K_Y  76309 /* 1.164383 * 65536 */
#define YUV_K_RV 104597 /* 1.596027 * 65536 */
#define YUV_K_GU 25675 /* 0.391762 * 65536 */
#define YUV_K_GV 53279 /* 0.812968 * 65536 */
#define YUV_K_BU 132201 /* 2.017232 * 65536 */

/**
 * @brief Name table: canonical name first for each format, then aliases
 */
static const struct {
	const char *name;
	yuv_format_t format;
} s_yuv_names[] = {
	{ "i420",    YUV_FMT_I420 },
	{ "yuv420p", YUV_FMT_I420 },
	{ "yv12",    YUV_FMT_YV12 },
	{ "nv12",    YUV_FMT_NV12 },
	{ "nv21",    YUV_FMT_NV21 },
	{ "i422",    YUV_FMT_I422 },
	{ "yuv422p", YUV_FMT_I422 },
	{ "i444",    YUV_FMT_I444 },
	{ "yuv444p", YUV_FMT_I444 },
	{ "yuyv",    YUV_FMT_YUYV },
	{ "yuy2",    YUV_FMT_YUYV },
	{ "uyvy",    YUV_FMT_UYVY },
	{ "gray",    YUV_FMT_GRAY },
	{ "y8",      YUV_FMT_GRAY },
	{ "y800",    YUV_FMT_GRAY },
};

/**
 * @struct yuv_layout_t
 * @brief Resolved byte layout of one frame
 */
typedef struct {
	size_t y_stride; /**< Bytes per luma / packed row */
	size_t y_row_bytes; /**< Minimal (visible) bytes per luma / packed row */
	size_t c_stride; /**< Bytes per chroma row (0 if no chroma planes) */
	size_t c_row_bytes; /**< Minimal (visible) bytes per chroma row */
	size_t c_rows; /**< Number of chroma rows */
	unsigned c_planes; /**< Number of chroma planes in memory: 0, 1 (NV) or 2 */
	unsigned x_shift; /**< Horizontal chroma subsampling shift */
	unsigned y_shift; /**< Vertical chroma subsampling shift */
	size_t off_c0; /**< Offset of the first chroma plane */
	size_t off_c1; /**< Offset of the second chroma plane (c_planes == 2) */
	size_t required; /**< Bytes required for the frame (lenient last row) */
} yuv_layout_t;

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

/** Overflow-checked size_t multiplication */
static bool size_mul(size_t a, size_t b, size_t *out)
{
	if (a != 0 && b > SIZE_MAX / a) {
		return false;
	}
	*out = a * b;
	return true;
}

/** Overflow-checked size_t addition */
static bool size_add(size_t a, size_t b, size_t *out)
{
	if (b > SIZE_MAX - a) {
		return false;
	}
	*out = a + b;
	return true;
}

/** ASCII case-insensitive string equality */
static bool ascii_iequal(const char *a, const char *b)
{
	for (; *a != '\0' && *b != '\0'; a++, b++) {
		char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a - 'A' + 'a') : *a;
		char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b - 'A' + 'a') : *b;
		if (ca != cb) {
			return false;
		}
	}
	return *a == '\0' && *b == '\0';
}

/** printf into an optional error buffer */
#define YUV_ERR(err, err_len, ...)                    \
	do {                                              \
		if ((err) != NULL && (err_len) > 0) {         \
			snprintf((err), (err_len), __VA_ARGS__);  \
		}                                             \
	} while (0)

yuv_format_t yuv_format_from_name(const char *name)
{
	if (name == NULL) {
		return YUV_FMT_NONE;
	}
	for (size_t i = 0; i < sizeof(s_yuv_names) / sizeof(s_yuv_names[0]); i++) {
		if (ascii_iequal(name, s_yuv_names[i].name)) {
			return s_yuv_names[i].format;
		}
	}
	return YUV_FMT_NONE;
}

const char *yuv_format_name(yuv_format_t fmt)
{
	for (size_t i = 0; i < sizeof(s_yuv_names) / sizeof(s_yuv_names[0]); i++) {
		if (s_yuv_names[i].format == fmt) {
			return s_yuv_names[i].name;
		}
	}
	return "unknown";
}

/**
 * @brief Resolve and validate the byte layout of a frame
 *
 * @return 0 on success, -1 on invalid parameters (message in err)
 */
static int yuv_compute_layout(const yuv_params_t *p, yuv_layout_t *L, char *err, size_t err_len)
{
	if (p == NULL || L == NULL) {
		YUV_ERR(err, err_len, "missing parameters");
		return -1;
	}

	memset(L, 0, sizeof(*L));

	if (p->width == 0 || p->height == 0) {
		YUV_ERR(err, err_len, "width and height must be greater than 0 (got %ux%u)", p->width, p->height);
		return -1;
	}
	if (p->width > IMAGE_MAX_DIMENSION || p->height > IMAGE_MAX_DIMENSION) {
		YUV_ERR(err, err_len, "dimensions %ux%u exceed maximum %u per axis", p->width, p->height, (unsigned)IMAGE_MAX_DIMENSION);
		return -1;
	}
	if ((uint64_t)p->width * (uint64_t)p->height > (uint64_t)IMAGE_MAX_PIXELS) {
		YUV_ERR(err, err_len, "dimensions %ux%u exceed maximum of %lu pixels", p->width, p->height, (unsigned long)IMAGE_MAX_PIXELS);
		return -1;
	}

	const size_t w = p->width;
	const size_t h = p->height;
	const size_t half_w = (w + 1) / 2; /* ceil(w/2) */
	const size_t half_h = (h + 1) / 2; /* ceil(h/2) */

	/* Per-format geometry. Values are bounded by IMAGE_MAX_DIMENSION, no overflow. */
	switch (p->format) {
		case YUV_FMT_I420:
		case YUV_FMT_YV12:
			L->y_row_bytes = w;
			L->c_planes = 2;
			L->x_shift = 1;
			L->y_shift = 1;
			L->c_row_bytes = half_w;
			L->c_rows = half_h;
			break;

		case YUV_FMT_NV12:
		case YUV_FMT_NV21:
			L->y_row_bytes = w;
			L->c_planes = 1;
			L->x_shift = 1;
			L->y_shift = 1;
			L->c_row_bytes = 2 * half_w;
			L->c_rows = half_h;
			break;

		case YUV_FMT_I422:
			L->y_row_bytes = w;
			L->c_planes = 2;
			L->x_shift = 1;
			L->c_row_bytes = half_w;
			L->c_rows = h;
			break;

		case YUV_FMT_I444:
			L->y_row_bytes = w;
			L->c_planes = 2;
			L->c_row_bytes = w;
			L->c_rows = h;
			break;

		case YUV_FMT_YUYV:
		case YUV_FMT_UYVY:
			/* 4-byte macropixel per 2 pixels; odd widths need a full last macropixel */
			L->y_row_bytes = 4 * half_w;
			break;

		case YUV_FMT_GRAY: L->y_row_bytes = w; break;

		case YUV_FMT_NONE:
		default: YUV_ERR(err, err_len, "unknown format (supported: %s)", YUV_SPEC_FORMATS); return -1;
	}

	/* Strides */
	L->y_stride = (p->y_stride != 0) ? p->y_stride : L->y_row_bytes;
	if (L->y_stride < L->y_row_bytes) {
		YUV_ERR(err, err_len, "y_stride %zu is smaller than the minimum %zu for %s width %u", L->y_stride, L->y_row_bytes, yuv_format_name(p->format), p->width);
		return -1;
	}

	if (L->c_planes == 0) {
		if (p->uv_stride != 0) {
			YUV_ERR(err, err_len, "uv_stride is not applicable to format %s (it has no chroma planes)", yuv_format_name(p->format));
			return -1;
		}
	} else {
		L->c_stride = (p->uv_stride != 0) ? p->uv_stride : L->c_row_bytes;
		if (L->c_stride < L->c_row_bytes) {
			YUV_ERR(err, err_len, "uv_stride %zu is smaller than the minimum %zu for %s width %u", L->c_stride, L->c_row_bytes, yuv_format_name(p->format), p->width);
			return -1;
		}
	}

	/* Plane offsets and lenient required size: full stride*rows for every
	 * plane except the last row of the last plane (only visible bytes). */
	size_t y_plane = 0;
	size_t c_plane = 0;
	size_t last_rows_off = 0;
	bool ok = size_mul(L->y_stride, h, &y_plane);

	if (ok && L->c_planes == 0) {
		ok = size_mul(L->y_stride, h - 1, &last_rows_off) && size_add(last_rows_off, L->y_row_bytes, &L->required);

	} else if (ok) {
		L->off_c0 = y_plane;
		ok = size_mul(L->c_stride, L->c_rows, &c_plane);
		size_t last_plane_off = L->off_c0;
		if (ok && L->c_planes == 2) {
			ok = size_add(L->off_c0, c_plane, &L->off_c1);
			last_plane_off = L->off_c1;
		}
		ok = ok && size_mul(L->c_stride, L->c_rows - 1, &last_rows_off) && size_add(last_plane_off, last_rows_off, &L->required) && size_add(L->required, L->c_row_bytes, &L->required);
	}

	if (!ok) {
		YUV_ERR(err, err_len, "frame size overflows (strides too large)");
		return -1;
	}

	return 0;
}

bool yuv_frame_size(const yuv_params_t *params, size_t *out_size)
{
	yuv_layout_t L;
	if (out_size == NULL || yuv_compute_layout(params, &L, NULL, 0) < 0) {
		return false;
	}
	*out_size = L.required;
	return true;
}

/* ------------------------------------------------------------------------- */
/* Specification parsing                                                     */
/* ------------------------------------------------------------------------- */

/**
 * @brief Parse a decimal, digits-only unsigned number
 *
 * @return 0 on success, -1 if not a number, -2 if larger than max
 */
static int parse_decimal(const char *s, uint64_t max, uint64_t *out)
{
	if (s == NULL || *s == '\0') {
		return -1;
	}

	uint64_t value = 0;
	for (const char *c = s; *c != '\0'; c++) {
		if (*c < '0' || *c > '9') {
			return -1;
		}
		unsigned digit = (unsigned)(*c - '0');
		if (value > (max - digit) / 10) {
			return -2;
		}
		value = value * 10 + digit;
	}

	*out = value;
	return 0;
}

/**
 * @brief Parse one numeric field with field-specific error messages
 */
static int parse_field(const char *field, const char *what, uint64_t max, uint64_t *out, char *err, size_t err_len)
{
	int rc = parse_decimal(field, max, out);
	if (rc == -1) {
		YUV_ERR(err, err_len, "invalid %s '%s' (expected a non-negative decimal integer)", what, field);
		return -1;
	}
	if (rc == -2) {
		YUV_ERR(err, err_len, "%s '%s' is out of range (maximum %llu)", what, field, (unsigned long long)max);
		return -1;
	}
	return 0;
}

int yuv_parse_spec(const char *spec, yuv_params_t *out, char *err, size_t err_len)
{
	if (spec == NULL || out == NULL) {
		YUV_ERR(err, err_len, "missing specification");
		return -1;
	}

	size_t spec_len = strlen(spec);
	if (spec_len == 0) {
		YUV_ERR(err, err_len, "empty specification (expected %s)", YUV_SPEC_SYNTAX);
		return -1;
	}
	if (spec_len >= YUV_SPEC_MAX_LEN) {
		YUV_ERR(err, err_len, "specification too long (max %d characters)", YUV_SPEC_MAX_LEN - 1);
		return -1;
	}

	/* Split a private copy on ':' */
	char buf[YUV_SPEC_MAX_LEN];
	memcpy(buf, spec, spec_len + 1);

	char *fields[YUV_SPEC_MAX_FIELDS] = { 0 };
	size_t nfields = 0;
	char *cursor = buf;
	for (;;) {
		if (nfields == YUV_SPEC_MAX_FIELDS) {
			YUV_ERR(err, err_len, "too many fields (expected %s)", YUV_SPEC_SYNTAX);
			return -1;
		}
		fields[nfields++] = cursor;
		char *colon = strchr(cursor, ':');
		if (colon == NULL) {
			break;
		}
		*colon = '\0';
		cursor = colon + 1;
	}

	if (nfields < 3) {
		YUV_ERR(err, err_len, "missing fields (expected %s)", YUV_SPEC_SYNTAX);
		return -1;
	}

	/* Format */
	if (fields[0][0] == '\0') {
		YUV_ERR(err, err_len, "missing format (expected %s)", YUV_SPEC_SYNTAX);
		return -1;
	}
	yuv_params_t p = { 0 };
	p.format = yuv_format_from_name(fields[0]);
	if (p.format == YUV_FMT_NONE) {
		YUV_ERR(err, err_len, "unknown format '%s' (supported: %s)", fields[0], YUV_SPEC_FORMATS);
		return -1;
	}

	/* Dimensions (mandatory) */
	uint64_t v = 0;
	if (parse_field(fields[1], "width", UINT32_MAX, &v, err, err_len) < 0) {
		return -1;
	}
	p.width = (uint32_t)v;
	if (parse_field(fields[2], "height", UINT32_MAX, &v, err, err_len) < 0) {
		return -1;
	}
	p.height = (uint32_t)v;

	/* Strides (optional, empty = default) */
	if (nfields > 3 && fields[3][0] != '\0') {
		if (parse_field(fields[3], "y_stride", SIZE_MAX, &v, err, err_len) < 0) {
			return -1;
		}
		if (v == 0) {
			YUV_ERR(err, err_len, "y_stride must be greater than 0");
			return -1;
		}
		p.y_stride = (size_t)v;
	}
	if (nfields > 4 && fields[4][0] != '\0') {
		if (parse_field(fields[4], "uv_stride", SIZE_MAX, &v, err, err_len) < 0) {
			return -1;
		}
		if (v == 0) {
			YUV_ERR(err, err_len, "uv_stride must be greater than 0");
			return -1;
		}
		p.uv_stride = (size_t)v;
	}

	/* Validate geometry and resolve default strides */
	yuv_layout_t L;
	if (yuv_compute_layout(&p, &L, err, err_len) < 0) {
		return -1;
	}
	p.y_stride = L.y_stride;
	p.uv_stride = L.c_stride;

	*out = p;
	return 0;
}

/* ------------------------------------------------------------------------- */
/* Conversion                                                                */
/* ------------------------------------------------------------------------- */

/** Round a 16.16 fixed-point value and clamp to 0..255 */
static inline uint8_t fixed_to_u8(int32_t v)
{
	v += 1 << 15;
	if (v < 0) {
		return 0;
	}
	v >>= 16;
	return (v > 255) ? 255 : (uint8_t)v;
}

/** Convert one BT.601 limited-range YCbCr sample to RGBA */
static inline void yuv_to_rgba(int32_t y, int32_t u, int32_t v, uint8_t *dst)
{
	const int32_t c = (y - 16) * YUV_K_Y;
	const int32_t d = u - 128;
	const int32_t e = v - 128;

	dst[0] = fixed_to_u8(c + YUV_K_RV * e);
	dst[1] = fixed_to_u8(c - YUV_K_GU * d - YUV_K_GV * e);
	dst[2] = fixed_to_u8(c + YUV_K_BU * d);
	dst[3] = 255;
}

/**
 * @brief Convert one row of planar / semi-planar data
 *
 * @param y_row   Luma row
 * @param u_row   First U sample of the chroma row
 * @param v_row   First V sample of the chroma row
 * @param c_step  Distance between consecutive U (or V) samples: 1 planar, 2 NV
 * @param x_shift Horizontal subsampling shift (nearest chroma = x >> x_shift)
 */
static void convert_row_planar(const uint8_t *y_row, const uint8_t *u_row, const uint8_t *v_row, size_t c_step, unsigned x_shift, uint32_t width, uint8_t *dst)
{
	for (uint32_t x = 0; x < width; x++) {
		size_t cx = (size_t)(x >> x_shift) * c_step;
		yuv_to_rgba(y_row[x], u_row[cx], v_row[cx], dst + (size_t)x * 4);
	}
}

/**
 * @brief Convert one row of packed 4:2:2 data (2 pixels per 4-byte macropixel)
 *
 * @param y0, y1, u, v Byte positions of the components inside a macropixel
 */
static void convert_row_packed(const uint8_t *row, unsigned y0, unsigned y1, unsigned u, unsigned v, uint32_t width, uint8_t *dst)
{
	for (uint32_t x = 0; x < width; x++) {
		const uint8_t *m = row + (size_t)(x >> 1) * 4;
		yuv_to_rgba(m[(x & 1) ? y1 : y0], m[u], m[v], dst + (size_t)x * 4);
	}
}

/** Convert one luma-only row (limited range luma, neutral chroma) */
static void convert_row_gray(const uint8_t *y_row, uint32_t width, uint8_t *dst)
{
	for (uint32_t x = 0; x < width; x++) {
		yuv_to_rgba(y_row[x], 128, 128, dst + (size_t)x * 4);
	}
}

image_t **decode_yuv(const uint8_t *data, size_t len, const yuv_params_t *params, int *frame_count)
{
	if (frame_count != NULL) {
		*frame_count = 0;
	}
	if (data == NULL || len == 0 || params == NULL || frame_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_yuv");
		return NULL;
	}

	char err[160];
	yuv_layout_t L;
	if (yuv_compute_layout(params, &L, err, sizeof(err)) < 0) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid YUV parameters: %s", err);
		return NULL;
	}

	if (len < L.required) {
		img2_log(IMG2_LOG_ERROR, "Error: Raw YUV input too short for %s %ux%u: need %zu bytes, have %zu", yuv_format_name(params->format), params->width, params->height, L.required, len);
		return NULL;
	}

	image_t *img = image_create(params->width, params->height);
	if (img == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create image_t structure");
		return NULL;
	}

	const uint32_t w = params->width;
	for (uint32_t y = 0; y < params->height; y++) {
		const uint8_t *y_row = data + (size_t)y * L.y_stride;
		const size_t c_off = (size_t)(y >> L.y_shift) * L.c_stride;
		uint8_t *dst = img->pixels + (size_t)y * w * 4;

		switch (params->format) {
			case YUV_FMT_I420:
			case YUV_FMT_I422:
			case YUV_FMT_I444: convert_row_planar(y_row, data + L.off_c0 + c_off, data + L.off_c1 + c_off, 1, L.x_shift, w, dst); break;

			case YUV_FMT_YV12: convert_row_planar(y_row, data + L.off_c1 + c_off, data + L.off_c0 + c_off, 1, L.x_shift, w, dst); break;

			case YUV_FMT_NV12: convert_row_planar(y_row, data + L.off_c0 + c_off, data + L.off_c0 + c_off + 1, 2, L.x_shift, w, dst); break;

			case YUV_FMT_NV21: convert_row_planar(y_row, data + L.off_c0 + c_off + 1, data + L.off_c0 + c_off, 2, L.x_shift, w, dst); break;

			case YUV_FMT_YUYV: convert_row_packed(y_row, 0, 2, 1, 3, w, dst); break;

			case YUV_FMT_UYVY: convert_row_packed(y_row, 1, 3, 0, 2, w, dst); break;

			case YUV_FMT_GRAY: convert_row_gray(y_row, w, dst); break;

			case YUV_FMT_NONE:
			default:
				/* Unreachable: rejected by yuv_compute_layout() */
				image_destroy(img);
				return NULL;
		}
	}

	image_t **frames = (image_t **)malloc(sizeof(image_t *));
	if (frames == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate frames array");
		image_destroy(img);
		return NULL;
	}

	frames[0] = img;
	*frame_count = 1;
	return frames;
}
