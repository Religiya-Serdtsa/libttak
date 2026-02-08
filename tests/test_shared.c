#include <ttak/shared/shared.h>
#include <ttak/mem/owner.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

void test_shared_basic(void) {
    ttak_shared_t shared;
    ttak_shared_init(&shared);

    ttak_shared_result_t res = shared.allocate_typed(&shared, sizeof(int), "int", TTAK_SHARED_LEVEL_3);
    assert(res == TTAK_OWNER_SUCCESS);
    assert(strcmp(shared.type_name, "int") == 0);

    ttak_owner_t *owner1 = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
    ttak_owner_t *owner2 = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);

    res = shared.add_owner(&shared, owner1);
    assert(res == TTAK_OWNER_SUCCESS);

    res = shared.add_owner(&shared, owner2);
    assert(res == TTAK_OWNER_SUCCESS);

    /* Test access */
    ttak_shared_result_t access_res;
    int *data = tt_shared_access(int, &shared, owner1, &access_res);
    assert(data != NULL);
    assert(access_res == TTAK_OWNER_SUCCESS);

    *data = 42;
    shared.release(&shared);

    /* Test sync and access from another owner */
    int affected = 0;
    res = shared.sync_all(&shared, owner1, &affected);
    assert(res == TTAK_OWNER_SUCCESS);
    assert(affected == 2);

    const int *data2 = tt_shared_access(const int, &shared, owner2, &access_res);
    assert(data2 != NULL);
    assert(access_res == TTAK_OWNER_SUCCESS);
    assert(*data2 == 42);
    shared.release(&shared);

    /* Cleanup */
    ttak_owner_destroy(owner1);
    ttak_owner_destroy(owner2);
    ttak_shared_destroy(&shared);

    printf("test_shared_basic passed!\n");
}

int main(void) {
    test_shared_basic();
    return 0;
}
