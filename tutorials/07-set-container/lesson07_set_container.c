#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <ttak/container/set.h>
#include <ttak/timing/timing.h>

static int cmp_keys(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

int main(void) {
    uint64_t now = ttak_get_tick_count();
    ttak_set_t set;
    ttak_set_init(&set, 16, NULL, cmp_keys, NULL);

    const char *keys[] = {"alpha", "beta", "gamma"};
    for (size_t i = 0; i < 3; ++i) {
        ttak_set_add(&set, (void *)keys[i], strlen(keys[i]) + 1, now);
    }

    printf("contains beta? %s\n",
           ttak_set_contains(&set, "beta", strlen("beta") + 1, now) ? "yes" : "no");
    ttak_set_remove(&set, "alpha", strlen("alpha") + 1, now);
    printf("contains alpha? %s\n",
           ttak_set_contains(&set, "alpha", strlen("alpha") + 1, now) ? "yes" : "no");

    ttak_set_destroy(&set, now);
    return 0;
}
