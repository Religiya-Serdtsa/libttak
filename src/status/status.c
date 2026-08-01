/**
 * @file status.c
 * @brief Unified status code implementation.
 */

#include <ttak/status.h>

const char *ttak_status_string(ttak_status_t status) {
    switch (status) {
        case TTAK_OK:
            return "success";
        case TTAK_ERR_INVALID_ARGUMENT:
            return "invalid argument";
        case TTAK_ERR_OUT_OF_MEMORY:
            return "out of memory";
        case TTAK_ERR_RANGE:
            return "out of range";
        case TTAK_ERR_NOT_FOUND:
            return "not found";
        case TTAK_ERR_ALREADY_EXISTS:
            return "already exists";
        case TTAK_ERR_NOT_SUPPORTED:
            return "not supported";
        case TTAK_ERR_INVALID_STATE:
            return "invalid state";
        case TTAK_ERR_TIMEOUT:
            return "timeout";
        case TTAK_ERR_IO:
            return "I/O error";
        case TTAK_ERR_SECURITY:
            return "security error";
        case TTAK_ERR_INTERNAL:
            return "internal error";
        default:
            return "unknown status";
    }
}
