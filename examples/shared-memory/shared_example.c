/**
 * @file shared_example.c
 * @brief Demonstrating ttak_shared_t with multiple owners and bitmap validation.
 */

#include <ttak/shared/shared.h>
#include <ttak/mem/owner.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int counter;
    char message[64];
} shared_data_t;

int main(void) {
    printf("Starting ttak_shared_t example...\n");

    /* 1. Initialize the shared resource */
    ttak_shared_t shared;
    ttak_shared_init(&shared);
    shared.allocate_typed(&shared, sizeof(shared_data_t), "shared_data_t", TTAK_SHARED_LEVEL_3);

    printf("Shared Resource Type: %s, Size: %zu\n", shared.type_name, shared.size);

    /* 2. Create owners (Alice and Bob) */
    ttak_owner_t *alice = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
    ttak_owner_t *bob = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
    ttak_owner_t *charlie = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);

    printf("Alice ID: %u, Bob ID: %u, Charlie ID: %u\n", alice->id, bob->id, charlie->id);

    /* 3. Register Alice and Bob */
    shared.add_owner(&shared, alice);
    shared.add_owner(&shared, bob);

    /* 4. Alice modifies the data */
    ttak_shared_result_t res;
    shared_data_t *data = tt_shared_access(shared_data_t, &shared, alice, &res);
    if (data) {
        data->counter = 100;
        snprintf(data->message, sizeof(data->message), "Hello from Alice!");
        printf("Alice updated the data.\n");
        shared.release(&shared);
    }

    /* 5. Sync changes to everyone */
    int affected = 0;
    shared.sync_all(&shared, alice, &affected);
    printf("Sync complete. Owners updated: %d\n", affected);

    /* 6. Bob reads the data */
    const shared_data_t *bob_view = tt_shared_access(const shared_data_t, &shared, bob, &res);
    if (bob_view) {
        printf("Bob reads: [%d] %s\n", bob_view->counter, bob_view->message);
        shared.release(&shared);
    }

    /* 7. Charlie tries to access (should fail because not registered) */
    const void *denied = shared.access(&shared, charlie, &res);
    if (!denied) {
        printf("Charlie access denied as expected (Result code: %u)\n", res);
    }

    /* 8. Cleanup */
    ttak_owner_destroy(alice);
    ttak_owner_destroy(bob);
    ttak_owner_destroy(charlie);
    ttak_shared_destroy(&shared);

    printf("Example finished successfully.\n");
    return 0;
}