#include <ttak/mem/mem.h>
#include <ttak/mem/owner.h>
#include <ttak/timing/timing.h>
#include <stdio.h>

int main() {
    uint64_t now = ttak_get_tick_count();
    
    // 1. Allocation
    void *ptr = ttak_mem_alloc(1024, TT_SECOND(10), now);
    
    // 2. Access
    ttak_mem_access(ptr, now + 100);
    
    // 3. Ownership and Transfer
    ttak_owner_t *owner1 = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
    ttak_owner_t *owner2 = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
    
    ttak_owner_register_resource(owner1, "data_block", ptr);
    ttak_owner_transfer_resource(owner1, owner2, "data_block");
    
    // 4. Free
    ttak_mem_free(ptr);
    
    ttak_owner_destroy(owner1);
    ttak_owner_destroy(owner2);
    
    return 0;
}
