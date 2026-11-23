/**
 * @file meta_raw.c
 * @brief Internal: raw EXIF / XMP blocks of an image (see meta_raw.h)
 */

#include <stdlib.h>
#include <string.h>

#include "meta_raw.h"
#include "../core/log.h"

#define TIFF_TAG_ORIENTATION 0x0112
#define TIFF_TYPE_SHORT      3

bool img2_meta_is_tiff(const uint8_t *p, size_t len)
{
	if (p == NULL || len < 8) {
		return false;
	}
	return (p[0] == 'I' && p[1] == 'I' && p[2] == 42 && p[3] == 0) || (p[0] == 'M' && p[1] == 'M' && p[2] == 0 && p[3] == 42);
}

void img2_meta_keep_exif(img2_image_t *img, const char *fmt, const uint8_t *tiff, size_t len)
{
	if (!img2_meta_is_tiff(tiff, len)) {
		return;
	}
	if (len > IMG2_META_RAW_CAP) {
		img2_log(IMG2_LOG_WARN, "%s: EXIF block of %zu bytes exceeds the 1 MiB metadata cap (raw block not kept)", fmt, len);
		return;
	}
	uint8_t *copy = malloc(len);
	if (copy == NULL) {
		return;
	}
	memcpy(copy, tiff, len);
	free(img->exif_raw);
	img->exif_raw = copy;
	img->exif_raw_len = len;
}

void img2_meta_keep_xmp(img2_image_t *img, const char *fmt, const uint8_t *xml, size_t len)
{
	if (len == 0) {
		return;
	}
	if (len > IMG2_META_RAW_CAP) {
		img2_log(IMG2_LOG_WARN, "%s: XMP packet of %zu bytes exceeds the 1 MiB metadata cap (raw block not kept)", fmt, len);
		return;
	}
	char *copy = malloc(len + 1);
	if (copy == NULL) {
		return;
	}
	memcpy(copy, xml, len);
	copy[len] = '\0';
	free(img->xmp_raw);
	img->xmp_raw = copy;
	img->xmp_raw_len = len;
}

void img2_meta_clear(img2_image_t *img)
{
	free(img->exif_raw);
	img->exif_raw = NULL;
	img->exif_raw_len = 0;
	free(img->xmp_raw);
	img->xmp_raw = NULL;
	img->xmp_raw_len = 0;
}

uint8_t img2_meta_orientation(const img2_exif_t *exif)
{
	if (exif == NULL || exif->orientation < 1 || exif->orientation > 8) {
		return 1;
	}
	return (uint8_t)exif->orientation;
}

img2_status_t img2_meta_copy(img2_image_t *dst, const img2_image_t *src)
{
	uint8_t *exif = NULL;
	char *xmp = NULL;

	if (src->exif_raw != NULL && src->exif_raw_len > 0) {
		exif = malloc(src->exif_raw_len);
		if (exif == NULL) {
			return IMG2_E_NOMEM;
		}
		memcpy(exif, src->exif_raw, src->exif_raw_len);
	}
	if (src->xmp_raw != NULL && src->xmp_raw_len > 0) {
		xmp = malloc(src->xmp_raw_len + 1);
		if (xmp == NULL) {
			free(exif);
			return IMG2_E_NOMEM;
		}
		memcpy(xmp, src->xmp_raw, src->xmp_raw_len);
		xmp[src->xmp_raw_len] = '\0';
	}
	img2_meta_clear(dst);
	dst->exif_raw = exif;
	dst->exif_raw_len = exif != NULL ? src->exif_raw_len : 0;
	dst->xmp_raw = xmp;
	dst->xmp_raw_len = xmp != NULL ? src->xmp_raw_len : 0;
	dst->orientation = src->orientation;
	return IMG2_OK;
}

static uint16_t rd16(const uint8_t *p, bool le)
{
	return le ? (uint16_t)(p[0] | (p[1] << 8)) : (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t rd32(const uint8_t *p, bool le)
{
	return le ? ((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24))
	          : (((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3]);
}

bool img2_meta_patch_orientation(uint8_t *tiff, size_t len, uint16_t orientation)
{
	if (!img2_meta_is_tiff(tiff, len)) {
		return false;
	}
	const bool le = tiff[0] == 'I';
	size_t pos = rd32(tiff + 4, le);
	if (pos > len || len - pos < 2) {
		return false;
	}
	const uint16_t n = rd16(tiff + pos, le);
	pos += 2;
	for (uint16_t i = 0; i < n; i++, pos += 12) {
		if (len - pos < 12) {
			return false;
		}
		if (rd16(tiff + pos, le) != TIFF_TAG_ORIENTATION) {
			continue;
		}
		/* SHORT, count 1: the value sits in the first two bytes of the value field (a larger count would make the
		 * field an offset: not touched) */
		if (rd16(tiff + pos + 2, le) != TIFF_TYPE_SHORT || rd32(tiff + pos + 4, le) != 1) {
			return false;
		}
		uint8_t *v = tiff + pos + 8;
		if (le) {
			v[0] = (uint8_t)orientation;
			v[1] = (uint8_t)(orientation >> 8);
		} else {
			v[0] = (uint8_t)(orientation >> 8);
			v[1] = (uint8_t)orientation;
		}
		return true;
	}
	return false;
}

img2_status_t img2_meta_exif_for_write(const img2_image_t *img, uint8_t **out, size_t *len)
{
	*out = NULL;
	*len = 0;
	if (img->exif_raw == NULL || img->exif_raw_len == 0) {
		return IMG2_OK;
	}
	uint8_t *copy = malloc(img->exif_raw_len);
	if (copy == NULL) {
		return IMG2_E_NOMEM;
	}
	memcpy(copy, img->exif_raw, img->exif_raw_len);
	const uint16_t o = (img->orientation >= 1 && img->orientation <= 8) ? img->orientation : 1;
	(void)img2_meta_patch_orientation(copy, img->exif_raw_len, o);
	*out = copy;
	*len = img->exif_raw_len;
	return IMG2_OK;
}
