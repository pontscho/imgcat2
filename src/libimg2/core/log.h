/**
 * @file log.h
 * @brief Library diagnostics: the internal img2_log() (levels, callback, setters: <img2/log.h>)
 */

#ifndef IMG2_INTERNAL_CORE_LOG_H
#define IMG2_INTERNAL_CORE_LOG_H
#include <img2/log.h>
/* internal */
void img2_log(img2_log_level_t level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#endif
