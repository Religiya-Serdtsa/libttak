#include <ttak/log/logger.h>
#include "test_macros.h"
#include <string.h>
#include <stdio.h>

static char last_log[256];

static void mock_sink(ttak_log_level_t level, const char *msg) {
    (void)level;
    snprintf(last_log, sizeof(last_log), "%s", msg);
}

static void test_logger_filters_levels(void) {
    ttak_logger_t logger;
    ttak_logger_init(&logger, mock_sink, TTAK_LOG_WARN);

    last_log[0] = '\0';
    ttak_logger_log(&logger, TTAK_LOG_INFO, "info suppressed");
    ASSERT(strlen(last_log) == 0);

    ttak_logger_log(&logger, TTAK_LOG_ERROR, "Critical Error %d", 404);
    ASSERT(strcmp(last_log, "Critical Error 404") == 0);
}

int main(void) {
    RUN_TEST(test_logger_filters_levels);
    return 0;
}
