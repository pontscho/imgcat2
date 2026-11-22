/**
 * @file test_cov_c_encoders.c
 * @brief Coverage lane C: WebP encoder quality clamps (native scale, called directly)
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../libimg2/core/image.h"
#include "../../libimg2/encoders/encoder.h"
#include "../ctest.h"

CTEST(cov_c_encoders, webp_quality_clamps)
{
	img2_image_t *img = img2_image_alloc(8, 8);
	ASSERT_NOT_NULL(img);
	memset(img->pixels, 0x7F, 8 * 8 * 4);
	uint8_t *lossy = NULL;
	uint8_t *lossless = NULL;
	size_t lossy_size = 0;
	size_t lossless_size = 0;
	/* below 0: quality 0 (lossy); above 100: 100 (lossless) */
	ASSERT_EQUAL(IMG2_OK, img2_encode_webp(img, -20, &lossy, &lossy_size));
	ASSERT_EQUAL(IMG2_OK, img2_encode_webp(img, 400, &lossless, &lossless_size));
	ASSERT_TRUE(lossy_size > 12 && lossless_size > 12);
	/* translucent pixels: the lossy file is extended (VP8X + ALPH + VP8), the lossless one a plain VP8L */
	ASSERT_DATA((const unsigned char *)"VP8X", 4, lossy + 12, 4);
	ASSERT_DATA((const unsigned char *)"VP8L", 4, lossless + 12, 4);
	free(lossy);
	free(lossless);
	img2_image_destroy(img);
}
