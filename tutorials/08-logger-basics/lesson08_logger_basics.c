#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <ttak/log/logger.h>

static void stdout_logger(ttak_log_level_t level, const char *msg) {
    static const char *names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    printf("[%s] %s\n", names[level], msg);
}

int main(void) {
    ttak_logger_t logger;
    ttak_logger_init(&logger, stdout_logger, TTAK_LOG_DEBUG);
    ttak_logger_log(&logger, TTAK_LOG_INFO, "Lesson 08 logger online");
    ttak_logger_log(&logger, TTAK_LOG_WARN, "Remember to wire custom sinks");
    return 0;
}
