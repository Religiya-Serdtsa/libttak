#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <ttak/sync/sync.h>

int main(void) {
    ttak_mutex_t mutex;
    ttak_mutex_init(&mutex);
    ttak_mutex_lock(&mutex);
    puts("critical section protected by ttak_mutex_t");
    ttak_mutex_unlock(&mutex);
    ttak_mutex_destroy(&mutex);
    return 0;
}
