#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <ttak/sync/spinlock.h>

int main(void) {
    ttak_spin_t lock;
    ttak_spin_init(&lock);
    ttak_spin_lock(&lock);
    puts("fast critical section under spinlock");
    ttak_spin_unlock(&lock);
    return 0;
}
