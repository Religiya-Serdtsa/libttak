#include <ttak/log/logger.h>
#include <stdio.h>
#include <stdarg.h>

/**
 * @brief Default logging function writing to stderr.
 */
static void default_log_func(ttak_log_level_t level, const char *msg) {
    const char *lvl_str = "INFO";
    switch (level) {
        case TTAK_LOG_DEBUG: lvl_str = "DEBUG"; break;
        case TTAK_LOG_INFO:  lvl_str = "INFO "; break;
        case TTAK_LOG_WARN:  lvl_str = "WARN "; break;
        case TTAK_LOG_ERROR: lvl_str = "ERROR"; break;
    }
    fprintf(stderr, "[%s] %s\n", lvl_str, msg);
}

/**
 * @brief Initializes the logger.
 */
void ttak_logger_init(ttak_logger_t *l, ttak_log_func_t func, ttak_log_level_t level) {
    l->log_func = func ? func : default_log_func;
    l->min_level = level;
    l->should_trace = ttak_mem_set_trace;
}

/**
 * @brief Logs a formatted message if the level check passes.
 */
void ttak_logger_log(ttak_logger_t *l, ttak_log_level_t level, const char *fmt, ...) {
    if (level < l->min_level) return;
    
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    if (l->log_func) {
        l->log_func(level, buffer);
    }
}