/**
 * @file lesson37_shared_ownership.c
 * @brief Tutorial 37: Shared Memory with Bitmap Ownership validation.
 */

#include <ttak/shared/shared.h>
#include <ttak/mem/owner.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	int counter;
	char message[64];
} my_data_t;

int main() {
	printf("--- Tutorial 37: Shared Memory Ownership ---\n\n");

	ttak_shared_t shared;
	ttak_shared_init(&shared);

	ttak_shared_result_t res = shared.allocate_typed(&shared, sizeof(my_data_t), "my_data_t", TTAK_SHARED_LEVEL_3);
	if (res != TTAK_OWNER_SUCCESS) {
		fprintf(stderr, "Failed to allocate shared memory\n");
		return 1;
	}
	printf("[+] Shared resource allocated (Level 3 Safety)\n");

	ttak_owner_t *alice = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
	ttak_owner_t *bob = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
	ttak_owner_t *intruder = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);

	printf("[+] Created owners: Alice (ID:%u), Bob (ID:%u), Intruder (ID:%u)\n", 
	       alice->id, bob->id, intruder->id);

	shared.add_owner(&shared, alice);
	shared.add_owner(&shared, bob);
	printf("[+] Alice and Bob registered as owners\n");

	ttak_shared_result_t access_res;
	my_data_t *data = (my_data_t *)shared.access(&shared, alice, &access_res);

	if (data && (access_res == TTAK_OWNER_SUCCESS || access_res == TTAK_OWNER_VALID)) {
		printf("[Alice] Access GRANTED. Writing data...\n");
		data->counter = 100;
		strncpy(data->message, "Hello from Alice!", sizeof(data->message));
		shared.release(&shared);
	} else {
		printf("[Alice] Access DENIED (res: %u)\n", access_res);
	}

	data = (my_data_t *)shared.access(&shared, bob, &access_res);
	if (data && (access_res == TTAK_OWNER_SUCCESS || access_res == TTAK_OWNER_VALID)) {
		printf("[Bob] Access GRANTED. Counter: %d, Msg: %s\n", data->counter, data->message);
		shared.release(&shared);
	}

	data = (my_data_t *)shared.access(&shared, intruder, &access_res);
	if (!data) {
		printf("[Intruder] Access DENIED as expected (Result: %u)\n", access_res);
		if (access_res & TTAK_OWNER_INVALID) {
			printf("    -> Reason: OWNER_INVALID (Not in bitmap)\n");
		}
	}

	ttak_owner_destroy(alice);
	ttak_owner_destroy(bob);
	ttak_owner_destroy(intruder);
	ttak_shared_destroy(&shared);

	printf("\n[+] Tutorial 37 completed successfully.\n");
	return 0;
}
