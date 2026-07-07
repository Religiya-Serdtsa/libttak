/**
 * @file test_status.c
 * @brief Tests for the unified ttak_status_t type.
 */

#include <ttak/status.h>
#include <stdbool.h>
#include <string.h>
#include "test_macros.h"

static void test_status_success_macro(void) {
    ASSERT(TTAK_STATUS_OK(TTAK_OK) == true);
    ASSERT(TTAK_STATUS_FAILED(TTAK_OK) == false);
}

static void test_status_failure_macros(void) {
    ASSERT(TTAK_STATUS_OK(TTAK_ERR_INVALID_ARGUMENT) == false);
    ASSERT(TTAK_STATUS_FAILED(TTAK_ERR_INVALID_ARGUMENT) == true);
    ASSERT(TTAK_STATUS_FAILED(TTAK_ERR_OUT_OF_MEMORY) == true);
    ASSERT(TTAK_STATUS_FAILED(TTAK_ERR_INTERNAL) == true);
}

static void test_status_strings(void) {
    ASSERT(strcmp(ttak_status_string(TTAK_OK), "success") == 0);
    ASSERT(strcmp(ttak_status_string(TTAK_ERR_OUT_OF_MEMORY), "out of memory") == 0);
    ASSERT(strcmp(ttak_status_string(TTAK_ERR_SECURITY), "security error") == 0);
    ASSERT(strcmp(ttak_status_string((ttak_status_t)999), "unknown status") == 0);
}

int main(void) {
    RUN_TEST(test_status_success_macro);
    RUN_TEST(test_status_failure_macros);
    RUN_TEST(test_status_strings);
    return 0;
}
