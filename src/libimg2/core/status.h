/**
 * @file status.h
 * @brief Library status codes (moves into the public <img2/common.h> later)
 */

#ifndef IMG2_INTERNAL_CORE_STATUS_H
#define IMG2_INTERNAL_CORE_STATUS_H

typedef enum img2_status {
	IMG2_OK = 0, IMG2_E_NOMEM = -1, IMG2_E_ARG = -2, IMG2_E_FORMAT = -3, IMG2_E_UNSUPPORTED = -4,
	IMG2_E_LIMIT = -5, IMG2_E_IO = -6, IMG2_E_CODEC = -7, IMG2_E_INTERNAL = -8,
	IMG2_E_NOT_FOUND = -9                        /* [H1] img2_font_load*: no matching font */
} img2_status_t;                                 /* values stable; append only */
const char *img2_strerror(img2_status_t st);     /* never NULL; "unknown status" fallback */

/* Process-global setup (recommended, not required: tables are const, fonts load lazily).
 * Reference counted; the final img2_shutdown() frees the font cache. */
img2_status_t img2_init(void);
void img2_shutdown(void);

#endif /* IMG2_INTERNAL_CORE_STATUS_H */
