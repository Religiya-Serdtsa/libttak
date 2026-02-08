# Lesson 37: Shared Memory with Bitmap Ownership

This tutorial introduces `ttak_shared_t` and `ttak_dynamic_mask_t`, which provide a high-performance, $O(1)$ ownership validation system for shared resources.

## Concepts

1.  **Unique Owner IDs**: Every `ttak_owner_t` is assigned a unique, monotonically increasing ID upon creation.
2.  **Bitmap-Based Validation**: `ttak_shared_t` uses a dynamic bitmap (`ttak_dynamic_mask_t`) to track which owners have permission to access a shared resource. This allows for constant-time ($O(1)$) validation.
3.  **Synchronization Levels**:
    - **LEVEL 3**: Strict validation of ownership and timestamps.
    - **LEVEL 2**: Moderate validation (allows some drift).
    - **LEVEL 1**: Basic ownership check.
    - **NO_LEVEL**: Maximum performance with no safety checks.
4.  **Atomic Masking**: The `ttak_dynamic_mask_t` uses an internal `ttak_rwlock_t` to ensure that setting, clearing, and testing bits are thread-safe operations.

## How to Run

1.  **Compile**:
    ```bash
    make
    ```
2.  **Run**:
    ```bash
    make run
    ```

## Example Code Sneak Peek

```c
ttak_shared_t shared;
ttak_shared_init(&shared);

// Allocate 1KB of shared memory
shared.allocate(&shared, 1024, TTAK_SHARED_LEVEL_3);

ttak_owner_t *alice = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
shared.add_owner(&shared, alice);

// Alice can access
ttak_shared_result_t res;
void *data = shared.access(&shared, alice, &res);
if (data) {
    // Perform operations
    shared.release(&shared);
}
```

## Why $O(1)$?

In complex systems with hundreds of owners, iterating through a list to check permissions is slow. By using a bitmap indexed by the Owner's ID, we can check access permissions with a single bitwise operation, regardless of the number of registered owners.
