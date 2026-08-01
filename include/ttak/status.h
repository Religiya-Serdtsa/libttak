/**
 * @file status.h
 * @brief Unified library-wide status type for libttak.
 *
 * Replaces ad-hoc error representations (bool, int, NULL) and the misuse of
 * domain-specific enums such as @c ttak_io_status_t for non-I/O subsystems.
 */

#ifndef TTAK_STATUS_H
#define TTAK_STATUS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Common result code used across libttak public APIs.
 */
typedef enum ttak_status {
    TTAK_OK = 0,                    /**< Operation succeeded. */
    TTAK_ERR_INVALID_ARGUMENT = 1,  /**< NULL or out-of-range argument. */
    TTAK_ERR_OUT_OF_MEMORY = 2,     /**< Memory allocation failed. */
    TTAK_ERR_RANGE = 3,             /**< Length, offset, or capacity out of range. */
    TTAK_ERR_NOT_FOUND = 4,         /**< Requested item does not exist. */
    TTAK_ERR_ALREADY_EXISTS = 5,    /**< Conflicting duplicate entry. */
    TTAK_ERR_NOT_SUPPORTED = 6,     /**< Operation not supported in current build. */
    TTAK_ERR_INVALID_STATE = 7,     /**< Object is not in a state that allows the call. */
    TTAK_ERR_TIMEOUT = 8,           /**< Deadline expired before completion. */
    TTAK_ERR_IO = 9,                /**< Underlying I/O failure. */
    TTAK_ERR_SECURITY = 10,         /**< Cryptographic or security check failure. */
    TTAK_ERR_INTERNAL = 11,         /**< Unexpected internal library error. */
} ttak_status_t;

/** @brief True if @p s represents success. */
#define TTAK_STATUS_OK(s) ((s) == TTAK_OK)

/** @brief True if @p s represents failure. */
#define TTAK_STATUS_FAILED(s) ((s) != TTAK_OK)

/**
 * @brief Returns a human-readable description of a status code.
 *
 * @param status Status code to describe.
 * @return Static string describing the status.  Never NULL.
 */
const char *ttak_status_string(ttak_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_STATUS_H */
