#include <ttak/sync/sync.h>
#include <ttak/sync/spinlock.h>
#include "test_macros.h"

static void test_mutex_basic(void) {
    ttak_mutex_t mutex;
    ASSERT(ttak_mutex_init(&mutex) == 0);
    ASSERT(ttak_mutex_lock(&mutex) == 0);
    ASSERT(ttak_mutex_unlock(&mutex) == 0);
    ASSERT(ttak_mutex_destroy(&mutex) == 0);
}

static void test_spinlock_paths(void) {
    ttak_spin_t lock;
    ttak_spin_init(&lock);
    ttak_spin_lock(&lock);
    ASSERT(ttak_spin_trylock(&lock) == false);
    ttak_spin_unlock(&lock);
    ASSERT(ttak_spin_trylock(&lock) == true);
    ttak_spin_unlock(&lock);
}

int main(void) {
    RUN_TEST(test_mutex_basic);
    RUN_TEST(test_spinlock_paths);
    return 0;
}
