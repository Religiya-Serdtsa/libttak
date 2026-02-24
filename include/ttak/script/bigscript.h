#ifndef TTAK_SCRIPT_BIGSCRIPT_H
#define TTAK_SCRIPT_BIGSCRIPT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <ttak/math/bigint.h>
#include <ttak/math/bigreal.h>
#include <ttak/math/bigcomplex.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Loader interface for resolving file paths to contents at compile time.
 */
typedef struct {
    /** 
     * @brief Reads all bytes from a path.
     * @param ctx User-provided context.
     * @param path The path or module to resolve.
     * @param out_bytes Pointer to receive allocated bytes.
     * @param out_len Pointer to receive byte count.
     * @return true on success, false on failure.
     */
    bool (*read_all)(void *ctx, const char *path, uint8_t **out_bytes, size_t *out_len);
    /**
     * @brief Frees bytes allocated by read_all.
     * @param ctx User-provided context.
     */
    void (*free_bytes)(void *ctx, uint8_t *bytes);
    void *ctx;
} ttak_bigscript_loader_t;

/** @brief Opaque bigscript program object. */
typedef struct ttak_bigscript_program_t ttak_bigscript_program_t;

/** @brief Opaque thread-local VM context. */
typedef struct ttak_bigscript_vm_t ttak_bigscript_vm_t;

/**
 * @brief Limits applied to the script engine for safety and budget enforcement.
 */
typedef struct {
    uint32_t max_tokens;
    uint32_t max_ast_nodes;
    uint32_t max_macro_expansions;
    uint32_t max_call_depth;
    uint32_t max_steps_per_seed;
    uint32_t max_stack_words;
    uint32_t max_bigint_bits;
} ttak_bigscript_limits_t;

/** @brief Bigscript value types. */
typedef enum {
    TTAK_BIGSCRIPT_VAL_INT = 0,
    TTAK_BIGSCRIPT_VAL_REAL,
    TTAK_BIGSCRIPT_VAL_COMPLEX
} ttak_bigscript_val_type_t;

/** @brief Variant type that can hold any bigscript value. */
typedef struct {
    ttak_bigscript_val_type_t type;
    union {
        ttak_bigint_t     i;
        ttak_bigreal_t    r;
        ttak_bigcomplex_t c;
    } v;
} ttak_bigscript_variant_t;

/** @brief Error codes returned by compilation and execution. */
typedef enum {
    TTAK_BIGSCRIPT_ERR_NONE = 0,
    TTAK_BIGSCRIPT_ERR_SYNTAX,
    TTAK_BIGSCRIPT_ERR_LIMIT,
    TTAK_BIGSCRIPT_ERR_RUNTIME,
    TTAK_BIGSCRIPT_ERR_OOM,
    TTAK_BIGSCRIPT_ERR_MATH
} ttak_bigscript_error_code_t;

/** @brief Detailed error structure. */
typedef struct {
    ttak_bigscript_error_code_t code;
    const char *message;
} ttak_bigscript_error_t;

/** @brief Output value resulting from evaluating a seed. */
typedef struct {
    ttak_bigscript_variant_t value;
    bool is_found;
} ttak_bigscript_value_t;

/**
 * @brief Compiles source code into an immutable program.
 * @param source Null-terminated source string.
 * @param loader Optional loader for include/import.
 * @param limits Safety limits to apply.
 * @param err Error output structure.
 * @param now Monotonic timestamp.
 * @return Compiled program, or NULL on error.
 */
ttak_bigscript_program_t *ttak_bigscript_compile(
    const char *source,
    ttak_bigscript_loader_t *loader,
    const ttak_bigscript_limits_t *limits,
    ttak_bigscript_error_t *err,
    uint64_t now);

/**
 * @brief Frees a compiled program.
 * @param prog Program to free.
 * @param now Monotonic timestamp.
 */
void ttak_bigscript_program_free(ttak_bigscript_program_t *prog, uint64_t now);

/**
 * @brief Creates a new thread-local VM for executing a program.
 * @param limits Safety limits (mostly runtime limits).
 * @param now Monotonic timestamp.
 * @return Created VM context, or NULL on error.
 */
ttak_bigscript_vm_t *ttak_bigscript_vm_create(
    const ttak_bigscript_limits_t *limits,
    uint64_t now);

/**
 * @brief Frees a thread-local VM context.
 * @param vm VM context to free.
 * @param now Monotonic timestamp.
 */
void ttak_bigscript_vm_free(ttak_bigscript_vm_t *vm, uint64_t now);

/**
 * @brief Evaluates a script against a specific seed.
 * 
 * The worker must provide the pre-computed sum-of-proper-divisors (sn).
 * 
 * @param prog Compiled program.
 * @param vm Thread-local VM.
 * @param seed The target seed value.
 * @param sn The pre-computed s(seed) value.
 * @param out Evaluation result (is_found flag and optional output value).
 * @param err Output runtime error, if any.
 * @param now Monotonic timestamp.
 * @return true on success, false on runtime or budget error.
 */
bool ttak_bigscript_eval_seed(
    ttak_bigscript_program_t *prog,
    ttak_bigscript_vm_t *vm,
    const ttak_bigint_t *seed,
    const ttak_bigint_t *sn,
    ttak_bigscript_value_t *out,
    ttak_bigscript_error_t *err,
    uint64_t now);

/**
 * @brief Computes the SHA256 identity hash of a compiled program.
 * @param prog Compiled program.
 * @param out_hex 65-byte output buffer for hex string.
 */
void ttak_bigscript_hash_program(ttak_bigscript_program_t *prog, char out_hex[65]);

/**
 * @brief Frees resources held by a bigscript value.
 * @param val Value to free.
 * @param now Monotonic timestamp.
 */
void ttak_bigscript_value_free(ttak_bigscript_value_t *val, uint64_t now);

#ifdef __cplusplus
}
#endif

#endif
