/**
 * @file log.h
 * @brief libimg2 diagnostics: log levels and the host log callback
 */

#ifndef IMG2_LOG_H
#define IMG2_LOG_H

#include <img2/common.h>

IMG2_BEGIN_DECLS

/**
 * @brief Log message severity (lower value = more severe)
 */
typedef enum img2_log_level {
	IMG2_LOG_ERROR = 0,   /**< Errors */
	IMG2_LOG_WARN = 1,    /**< Warnings */
	IMG2_LOG_INFO = 2,    /**< Informational messages (registry lists, decode summaries) */
	IMG2_LOG_DEBUG = 3    /**< Debug messages (API boundary checks) */
} img2_log_level_t;

/**
 * @brief Host log callback
 *
 * msg has no trailing newline; the callback is called on the logging thread.
 * Contract: the callback must be thread-safe, must not call ANY img2_* function (including
 * img2_set_log_callback/img2_set_log_level), and may be invoked while library-internal locks are
 * held (e.g. during img2_init's registry listing, or font cache messages). Re-entry can deadlock.
 *
 * @param level Message severity
 * @param msg   Message text (valid only during the call)
 * @param user  The user pointer passed to img2_set_log_callback()
 */
typedef void (*img2_log_fn)(img2_log_level_t level, const char *msg, void *user);

/**
 * @brief Install the log callback (process-global)
 *
 * After img2_set_log_callback() returns, threads that are already logging may still call the
 * previous fn/user; the host keeps the previous user pointer valid until those calls finish
 * (or until img2_shutdown()).
 *
 * @param fn   Callback, or NULL to restore the default sink (stderr, one line per message)
 * @param user Opaque pointer handed to every fn call
 */
void img2_set_log_callback(img2_log_fn fn, void *user);

/**
 * @brief Set the most verbose level that is emitted (process-global)
 *
 * @param max_level Messages with level <= max_level are emitted; default IMG2_LOG_ERROR
 */
void img2_set_log_level(img2_log_level_t max_level);

/**
 * @brief Current maximum log level
 *
 * @return The level set by img2_set_log_level() (default IMG2_LOG_ERROR)
 */
img2_log_level_t img2_get_log_level(void);

IMG2_END_DECLS

#endif /* IMG2_LOG_H */
