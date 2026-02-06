#ifndef TTAK_LOG_LOGGER_H
#define TTAK_LOG_LOGGER_H

#include <stdarg.h>

/**
 * @brief Log levels.
 */
typedef enum {
    TTAK_LOG_DEBUG,
    TTAK_LOG_INFO,
    TTAK_LOG_WARN,
    TTAK_LOG_ERROR
} ttak_log_level_t;

/**
 * @brief Function pointer definition for custom log handlers.
 * 
 * @param level Severity level of the message.
 * @param msg Formatted message string.
 */
typedef void (*ttak_log_func_t)(ttak_log_level_t level, const char *msg);

/**
 * @brief Logger structure.
 * 
 * Encapsulates logging configuration including minimum level and handler.
 */
typedef struct ttak_logger {
    ttak_log_level_t min_level; /**< Minimum severity to log. */
    ttak_log_func_t log_func;   /**< Callback function for processing logs. */
    void (*should_trace)(int enable); /**< Toggle memory tracing for all objects. */
} ttak_logger_t;

typedef ttak_logger_t tt_logger_t;

/**
 * @brief Toggles memory tracing globally and for existing allocations.
 */
void ttak_mem_set_trace(int enable);

/**
 * @brief Initializes the logger.
 * 
 * @param l Pointer to the logger.
 * @param func Custom log function (pass NULL for default stderr).
 * @param level Minimum log level.
 */
void ttak_logger_init(ttak_logger_t *l, ttak_log_func_t func, ttak_log_level_t level);

/**
 * @brief Logs a message.
 * 
 * @param l Pointer to the logger.
 * @param level Severity level.
 * @param fmt Printf-style format string.
 * @param ... Arguments for format string.
 */
void ttak_logger_log(ttak_logger_t *l, ttak_log_level_t level, const char *fmt, ...);

#endif // TTAK_LOG_LOGGER_H