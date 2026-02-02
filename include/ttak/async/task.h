#ifndef TTAK_ASYNC_TASK_H
#define TTAK_ASYNC_TASK_H

#include <stdint.h>


typedef struct ttak_task ttak_task_t;

typedef void *(*ttak_task_func_t)(void *arg);

struct ttak_promise;

typedef struct ttak_promise ttak_promise_t;

/**
 * @brief Creates a new task.
 *
 * @param func Function to execute.
 * @param arg Argument to pass to the function.
 * @param promise Promise to fulfill when the task is complete.
 * @return Pointer to the created task.
 */
ttak_task_t *ttak_task_create(ttak_task_func_t func, void *arg, ttak_promise_t *promise, uint64_t now);

/**
 * @brief Executes the task.
 *
 * @param task Task to execute.
 */
void ttak_task_execute(ttak_task_t *task, uint64_t now);

/**
 * @brief Destroys the task and frees resources.
 *
 * @param task Task to destroy.
 */
void ttak_task_destroy(ttak_task_t *task, uint64_t now);

/**
 * @brief Creates a duplicate of an existing task.
 *
 * @param task Task to clone.
 * @param now Current timestamp for memory tracking.
 * @return Pointer to the cloned task, or NULL on failure.
 */
ttak_task_t *ttak_task_clone(const ttak_task_t *task, uint64_t now);

#endif // TTAK_ASYNC_TASK_H
