#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <ttak/unsafe/region.h>

int main(void) {
    ttak_unsafe_region_t region;
    ttak_unsafe_region_init(&region, __TTAK_REGION_CANONICAL_CTX__, __TTAK_REGION_CANONICAL_ALLOC__);
    printf("region empty? %s\n", ttak_unsafe_region_is_empty(&region) ? "yes" : "no");
    int buffer = 0;
    ttak_unsafe_region_adopt(&region, &buffer, sizeof(buffer), sizeof(buffer), "demo", 7);
    ttak_unsafe_region_pin(&region);
    ttak_unsafe_region_unpin(&region);
    ttak_unsafe_region_reset(&region);
    return 0;
}
