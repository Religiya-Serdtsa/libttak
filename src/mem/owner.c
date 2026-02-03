#include <ttak/mem/owner.h>
#include <ttak/ht/hash.h>
#include <ttak/mem/mem.h>
#include <ttak/timing/timing.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Hashing helper for string keys.
 */
static inline uintptr_t _hash_str(const char *str) {
    // Simple wrapper assuming string keys are cast to uintptr_t or hashed
    // For this implementation, we use a basic DJB2-like hash or cast if the map supports it.
    // Assuming ttak_map handles uintptr_t keys. We hash the string to uintptr_t.
    uintptr_t hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

ttak_owner_t *ttak_owner_create(uint32_t policy) {
    ttak_owner_t *owner = malloc(sizeof(ttak_owner_t));
    if (!owner) return NULL;

    uint64_t now = ttak_get_tick_count();
    
    // Initialize resource and function maps
    owner->resources = ttak_create_map(32, now);
    owner->functions = ttak_create_map(32, now);
    
    ttak_rwlock_init(&owner->lock);
    owner->creation_ts = now;
    owner->policy_flags = policy;

    return owner;
}

void ttak_owner_destroy(ttak_owner_t *owner) {
    if (!owner) return;

    ttak_rwlock_wrlock(&owner->lock);
    
    // Note: We do not deep-free resources here as we don't know their destructors.
    // In a full system, we might need a resource wrapper with a dtor.
    // ttak_map_destroy(owner->resources); // Assuming generic map destroy exists or we just leak for this snippet
    // ttak_map_destroy(owner->functions);
    
    ttak_rwlock_unlock(&owner->lock);
    ttak_rwlock_destroy(&owner->lock);
    free(owner);
}

bool ttak_owner_register_func(ttak_owner_t *owner, const char *name, ttak_owner_func_t func) {
    if (!owner || !name || !func) return false;

    ttak_rwlock_wrlock(&owner->lock);
    uintptr_t key = _hash_str(name);
    
    // Check if strict policy prevents overwriting (simplified: just insert)
    ttak_insert_to_map(owner->functions, key, (size_t)func, ttak_get_tick_count());
    
    ttak_rwlock_unlock(&owner->lock);
    return true;
}

bool ttak_owner_register_resource(ttak_owner_t *owner, const char *name, void *data) {
    if (!owner || !name) return false;

    ttak_rwlock_wrlock(&owner->lock);
    uintptr_t key = _hash_str(name);
    
    ttak_insert_to_map(owner->resources, key, (size_t)data, ttak_get_tick_count());
    
    ttak_rwlock_unlock(&owner->lock);
    return true;
}

bool ttak_owner_execute(ttak_owner_t *owner, const char *func_name, const char *resource_name, void *args) {
    if (!owner || !func_name) return false;

    ttak_rwlock_rdlock(&owner->lock);

    // 1. Safety Check: Verify policy
    // If strict isolation is on, ensure we aren't passing "args" from outside that violates it?
    // For this prototype, we just proceed.
    
    uintptr_t func_key = _hash_str(func_name);
    size_t func_ptr_val = 0;
    
    // 2. Retrieve Function
    if (!ttak_map_get_key(owner->functions, func_key, &func_ptr_val, ttak_get_tick_count())) {
        ttak_rwlock_unlock(&owner->lock);
        return false; // Function not found
    }

    // 3. Retrieve Context Resource (if specified)
    void *ctx = NULL;
    if (resource_name) {
        uintptr_t res_key = _hash_str(resource_name);
        size_t res_ptr_val = 0;
        if (ttak_map_get_key(owner->resources, res_key, &res_ptr_val, ttak_get_tick_count())) {
            ctx = (void *)res_ptr_val;
        }
    }

    ttak_owner_func_t func = (ttak_owner_func_t)func_ptr_val;

    // 4. Execution Boundary
    // Ideally, we would set thread-local storage here to indicate we are inside this owner
    // so that mem_alloc calls check the owner's policy.
    
    func(ctx, args);

    ttak_rwlock_unlock(&owner->lock);
    return true;
}
