/**
 * @file log.h
 * @brief Library diagnostics: levels, host callback and the internal img2_log()
 *
 * The public part (level enum, callback type, setters/getter) moves verbatim to
 * <img2/log.h> later; img2_log() stays internal.
 */

#ifndef IMG2_INTERNAL_CORE_LOG_H
#define IMG2_INTERNAL_CORE_LOG_H
typedef enum img2_log_level { IMG2_LOG_ERROR = 0, IMG2_LOG_WARN = 1, IMG2_LOG_INFO = 2, IMG2_LOG_DEBUG = 3 } img2_log_level_t;
/* [R20] fn must be thread-safe, must not call any img2_* function, and may run with internal locks held. */
typedef void (*img2_log_fn)(img2_log_level_t level, const char *msg, void *user);
void img2_set_log_callback(img2_log_fn fn, void *user);   /* fn == NULL restores the default stderr sink */
void img2_set_log_level(img2_log_level_t max_level);       /* emit messages with level <= max_level; default IMG2_LOG_ERROR */
img2_log_level_t img2_get_log_level(void);
/* internal */
void img2_log(img2_log_level_t level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#endif
