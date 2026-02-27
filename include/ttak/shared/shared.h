/**
 * @file ttak_shared.h
 * @brief Shared memory resource management with ownership-based access control.
 * @author Gemini
 * @date 2026-02-07
 */

#ifndef TTAK_SHARED_H
#define TTAK_SHARED_H

#include <ttak/mem/owner.h>
#include <ttak/sync/sync.h>
#include <ttak/mask/dynamic_mask.h>
#include <ttak/mem/epoch.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

/**
 * @struct ttak_shard_table_t
 * @brief Lock-free Segmented Shard Array (Page Table).
 * Maps thread logical IDs to unique timestamp slots without locks or reallocations.
 */
#define TTAK_SHARD_PAGE_SHIFT 4 // 16 slots per page
#define TTAK_SHARD_PAGE_SIZE (1 << TTAK_SHARD_PAGE_SHIFT)
#define TTAK_SHARD_PAGE_MASK (TTAK_SHARD_PAGE_SIZE - 1)
#define TTAK_SHARD_DIR_SIZE 64  // 64 pages * 16 slots = 1024 threads max per object

typedef struct {
	atomic_uint_least64_t * _Atomic dir[TTAK_SHARD_DIR_SIZE]; /**< Array of atomic pointers to pages */
	uint32_t _Atomic active_pages; /**< High-water mark of allocated pages for fast iteration */
} ttak_shard_table_t;

/**
 * @brief Bitmask flags representing the current status of the shared resource.
 */
typedef uint32_t ttak_shared_status_t;
#define TTAK_SHARED_READY    (0)       /**< Resource is stable and ready for use */
#define TTAK_SHARED_DIRTY    (1 << 0)  /**< Data has been modified but not synchronized */
#define TTAK_SHARED_EXPIRED  (1 << 1)  /**< Resource timestamp is outdated */
#define TTAK_SHARED_ZOMBIE   (1 << 2)  /**< No active owners; pending deallocation */
#define TTAK_SHARED_READONLY (1 << 3)  /**< Resource is in read-only mode */
#define TTAK_SHARED_USE_EBR  (1 << 4)  /**< EBR protection enabled */
#define TTAK_SHARED_SWAPPING (1 << 5)  /**< Data is currently being swapped */

/**
 * @brief Operational result codes for shared memory actions.
 */
typedef uint32_t ttak_shared_result_t;
#define TTAK_OWNER_VALID        (0)      /**< Success: Access granted and validated */
#define TTAK_OWNER_CORRUPTED    (1 << 0) /**< Safety Error: Timestamp mismatch detected */
#define TTAK_OWNER_INVALID      (1 << 1) /**< Auth Error: Owner information mismatch */
#define TTAK_OWNER_SHARE_DENIED (1 << 2) /**< Resource Error: Failed to register new owner */
#define TTAK_OWNER_CAP_EXHAUSTED (1 << 3) /**< Scalability Error: Thread capacity exceeded */
#define TTAK_OWNER_SUCCESS      (1 << 4) /**< General Success indicator */

/**
 * @brief Security enforcement levels for ownership validation.
 */
typedef enum {
	TTAK_SHARED_LEVEL_3 = 3, /**< Strict: Full ownership and timestamp validation */
	TTAK_SHARED_LEVEL_2 = 2, /**< Moderate: Allows slight timestamp drift */
	TTAK_SHARED_LEVEL_1 = 1, /**< Loose: Basic owner check only; not recommended */
	TTAK_SHARED_NO_LEVEL = 0  /**< None: No ownership check; maximum performance/danger */
} ttak_shared_level_t;

/**
 * @struct ttak_shared_t
 * @brief Core structure for managing shared variables among multiple owners.
 */
typedef struct ttak_shared_s {
	void * _Atomic  shared;         /**< Pointer to the actual data payload (Atomic for lock-free access) */
	size_t size;                    /**< Total byte size of the payload */
	const char *type_name;          /**< String representation of the payload type */
	uint32_t type_id;               /**< Optional numeric ID for the type */

	ttak_dynamic_mask_t owners_mask; /**< Thread-safe dynamic mask for ownership */
	ttak_shard_table_t shards;      /**< Lock-free Segmented Shards for sync */
	uint64_t ts;                    /**< Timestamp to track its lifetime */

	ttak_rwlock_t rwlock;           /**< R/W lock for metadata (swap, status) */
	ttak_shared_status_t status;    /**< Current status flags (DIRTY, EXPIRED, etc.) */
	ttak_shared_level_t level;      /**< Enforced security level for this resource */
	bool is_atomic_read;            /**< Flag to enable/disable atomic read operations */

	void * _Atomic retired_ptr;    /**< Pointer currently being retired (internal use) */

	/**
	 * @brief Custom destructor for the shared payload.
	 * @param data Pointer to the payload to be cleaned up.
	 */
	void (*cleanup)(void *data);

	/**
	 * @brief Initializes and allocates the shared resource.
	 * @param self Pointer to the ttak_shared_t instance.
	 * @param size Size of the data to allocate.
	 * @param level Security level to be applied.
	 * @return Result code of the allocation.
	 */
	ttak_shared_result_t (*allocate)(struct ttak_shared_s *self, size_t size, ttak_shared_level_t level);

	/**
	 * @brief Initializes and allocates the shared resource with a type name.
	 * @param self Pointer to the ttak_shared_t instance.
	 * @param size Size of the data to allocate.
	 * @param type_name Name of the type being allocated.
	 * @param level Security level to be applied.
	 * @return Result code of the allocation.
	 */
	ttak_shared_result_t (*allocate_typed)(struct ttak_shared_s *self, size_t size, const char *type_name, ttak_shared_level_t level);

	/**
	 * @brief Adds a new owner to the shared resource.
	 * @param self Pointer to the ttak_shared_t instance.
	 * @param owner The owner to add.
	 * @return Result code of the operation.
	 */
	ttak_shared_result_t (*add_owner)(struct ttak_shared_s *self, ttak_owner_t *owner);

	/**
	 * @brief Validates and grants access to the shared data.
	 * @param self Pointer to the ttak_shared_t instance.
	 * @param claimant The owner requesting access.
	 * @param result Pointer to store the detailed validation result.
	 * @return Const pointer to the shared data, or NULL if denied.
	 */
	const void *(*access)(struct ttak_shared_s *self, ttak_owner_t *claimant, ttak_shared_result_t *result);

	/**
	 * @brief Validates and grants access with optional EBR protection.
	 * @param self Pointer to the ttak_shared_t instance.
	 * @param claimant The owner requesting access.
	 * @param use_ebr_protection If true, enters EBR critical section.
	 * @param result Pointer to store the detailed validation result.
	 * @return Const pointer to the shared data, or NULL if denied.
	 */
	const void *(*access_ebr)(struct ttak_shared_s *self, ttak_owner_t *claimant, bool use_ebr_protection, ttak_shared_result_t *result);

	/**
	 * @brief Releases the access acquired via access().
	 * @param self Pointer to the ttak_shared_t instance.
	 */
	void (*release)(struct ttak_shared_s *self);

	/**
	 * @brief Releases access acquired via access_ebr().
	 * @param self Pointer to the ttak_shared_t instance.
	 */
	void (*release_ebr)(struct ttak_shared_s *self);

	/**
	 * @brief Synchronizes changes across all registered owners.
	 * @param self Pointer to the ttak_shared_t instance.
	 * @param claimant The owner initiating the sync.
	 * @param affected Pointer to store the number of owners updated.
	 * @return Result code of the synchronization.
	 */
	ttak_shared_result_t (*sync_all)(struct ttak_shared_s *self, ttak_owner_t *claimant, int *affected);

	/**
	 * @brief Sets the resource to read-only mode.
	 */
	ttak_shared_result_t (*set_ro)(struct ttak_shared_s *self);

	/**
	 * @brief Sets the resource to read-write mode.
	 */
	ttak_shared_result_t (*set_rw)(struct ttak_shared_s *self);

	/**
	 * @brief Toggles atomic read mode.
	 * @param self Pointer to the ttak_shared_t instance.
	 * @param enable Boolean value to set the atomic state.
	 */
	ttak_shared_result_t (*set_atomic_read)(struct ttak_shared_s *self, bool enable);

	/**
	 * @brief Retires the entire container safely using EBR.
	 * @param self Pointer to the ttak_shared_t instance.
	 */
	void (*retire)(struct ttak_shared_s *self);

} ttak_shared_t;

/**
 * @brief Swaps the internal data pointer using EBR for safety.
 * @param self Pointer to the ttak_shared_t instance.
 * @param new_shared Pointer to the new data.
 * @param new_size Size of the new data.
 * @return Result code of the operation.
 */
ttak_shared_result_t ttak_shared_swap_ebr(ttak_shared_t *self, void *new_shared, size_t new_size);

/**
 * @brief Retrieves the size of the payload from the implicit header.
 * @param ptr Pointer returned by access() or access_ebr().
 * @return Size of the payload in bytes.
 */
size_t ttak_shared_get_payload_size(const void *ptr);

/**
 * @brief Retrieves the timestamp of the payload from the implicit header.
 * @param ptr Pointer returned by access() or access_ebr().
 * @return Timestamp of the payload.
 */
uint64_t ttak_shared_get_payload_ts(const void *ptr);

/**
 * @brief Helper macro for typed access to shared data.
 * @param type The type to cast the shared data to.
 * @param shared_ptr Pointer to the ttak_shared_t instance.
 * @param owner The owner requesting access.
 * @param res_ptr Pointer to store the result code.
 */
#define tt_shared_access(type, shared_ptr, owner, res_ptr) \
    ((type *)((shared_ptr)->access(shared_ptr, owner, res_ptr)))

/**
 * @brief Macro to define a typed wrapper for ttak_shared_t.
 * 
 * This creates a new struct type and helper functions for a specific payload type,
 * making the fields and type information explicit in the code.
 * 
 * @param name The suffix for the generated type and functions.
 * @param struct_type The actual C type of the payload.
 */
#define TTAK_SHARED_DEFINE_WRAPPER(name, struct_type) \
    typedef struct { \
        ttak_shared_t base; \
    } ttak_shared_##name##_t; \
    static inline void ttak_shared_##name##_init(ttak_shared_##name##_t *s) { \
        ttak_shared_init(&s->base); \
    } \
    static inline ttak_shared_result_t ttak_shared_##name##_allocate(ttak_shared_##name##_t *s, ttak_shared_level_t level) { \
        return s->base.allocate_typed(&s->base, sizeof(struct_type), #struct_type, level); \
    } \
    static inline struct_type *ttak_shared_##name##_access(ttak_shared_##name##_t *s, ttak_owner_t *owner, ttak_shared_result_t *res) { \
        return (struct_type *)s->base.access(&s->base, owner, res); \
    } \
    static inline void ttak_shared_##name##_release(ttak_shared_##name##_t *s) { \
        s->base.release(&s->base); \
    }

/**
 * @brief Constructor for ttak_shared_t.
 * @param self Pointer to the ttak_shared_t instance.
 */
void ttak_shared_init(ttak_shared_t *self);

/**
 * @brief Destructor for ttak_shared_t.
 * @param self Pointer to the ttak_shared_t instance.
 */
void ttak_shared_destroy(ttak_shared_t *self);

#endif /* TTAK_SHARED_H */
