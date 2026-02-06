#define _XOPEN_SOURCE 700
#include <inttypes.h>
#include <stdio.h>
#include <ttak/ht/map.h>
#include <ttak/timing/timing.h>

int main(void) {
    uint64_t now = ttak_get_tick_count();
    tt_map_t *map = ttak_create_map(16, now);
    if (!map) {
        fputs("ttak_create_map failed. Is libttak installed?\n", stderr);
        return 1;
    }

    const uintptr_t key = 0xABCDEFu;
    ttak_insert_to_map(map, key, 99, now);

    size_t value = 0;
    if (ttak_map_get_key(map, key, &value, now)) {
        printf("found 0x%" PRIXPTR " -> %zu\n", key, value);
    } else {
        puts("key missing; mirror the upstream probing rules.");
    }

    ttak_delete_from_map(map, key, now);
    puts("Lesson 04: mirror insert/find/remove invariants exactly.");
    return 0;
}
