/* baremetal_pthread.c - In-house POSIX thread shim for EMBEDDED_BAREMETAL */
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

/* --- Cortex-M critical section --- */
static inline void __ttak_disable_irq(void) {
    __asm volatile ("cpsid i" ::: "memory");
}
static inline void __ttak_enable_irq(void) {
    __asm volatile ("cpsie i" ::: "memory");
}

/* --- mutex --- */
int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *attr) {
    (void)attr;
    *m = 0U;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *m) {
    __ttak_disable_irq();
    while (*m) {
        __ttak_enable_irq();
        __ttak_disable_irq();
    }
    *m = 1U;
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *m) {
    __ttak_disable_irq();
    if (*m) {
        __ttak_enable_irq();
        return 16; /* EBUSY */
    }
    *m = 1U;
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *m) {
    *m = 0U;
    __ttak_enable_irq();
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *m) {
    (void)m;
    return 0;
}

/* --- condition variable (no-op in bare-metal single thread) --- */
int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *attr) {
    (void)c; (void)attr;
    return 0;
}
int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) {
    (void)c; (void)m;
    return 0;
}
int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *abstime) {
    (void)c; (void)m; (void)abstime;
    return 0;
}
int pthread_cond_signal(pthread_cond_t *c) {
    (void)c;
    return 0;
}
int pthread_cond_broadcast(pthread_cond_t *c) {
    (void)c;
    return 0;
}
int pthread_cond_destroy(pthread_cond_t *c) {
    (void)c;
    return 0;
}

/* --- read-write lock (same as mutex in bare-metal) --- */
int pthread_rwlock_init(pthread_rwlock_t *rw, const pthread_rwlockattr_t *attr) {
    (void)attr;
    *rw = 0U;
    return 0;
}
int pthread_rwlock_rdlock(pthread_rwlock_t *rw) {
    return pthread_mutex_lock((pthread_mutex_t *)rw);
}
int pthread_rwlock_wrlock(pthread_rwlock_t *rw) {
    return pthread_mutex_lock((pthread_mutex_t *)rw);
}
int pthread_rwlock_unlock(pthread_rwlock_t *rw) {
    return pthread_mutex_unlock((pthread_mutex_t *)rw);
}
int pthread_rwlock_destroy(pthread_rwlock_t *rw) {
    (void)rw;
    return 0;
}

/* --- one-time init --- */
int pthread_once(pthread_once_t *once_ctrl, void (*init_routine)(void)) {
    __ttak_disable_irq();
    if (*once_ctrl == 0U) {
        *once_ctrl = 1U;
        __ttak_enable_irq();
        init_routine();
    } else {
        __ttak_enable_irq();
    }
    return 0;
}

/* --- thread stubs --- */
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg) {
    (void)attr; (void)start_routine; (void)arg;
    *thread = 1U;
    return 0;
}

int pthread_join(pthread_t thread, void **retval) {
    (void)thread; (void)retval;
    return 0;
}

int pthread_detach(pthread_t thread) {
    (void)thread;
    return 0;
}

pthread_t pthread_self(void) {
    return 1U;
}

/* --- TLS (small static key array) --- */
#define TTAK_MAX_TLS_KEYS 16
static void *g_tls_slots[TTAK_MAX_TLS_KEYS] = {0};

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *)) {
    (void)destructor;
    static uint32_t next_key = 0;
    if (next_key >= TTAK_MAX_TLS_KEYS) return 12; /* ENOMEM */
    *key = next_key++;
    return 0;
}

int pthread_setspecific(pthread_key_t key, const void *value) {
    if (key >= TTAK_MAX_TLS_KEYS) return 22; /* EINVAL */
    g_tls_slots[key] = (void *)value;
    return 0;
}

void *pthread_getspecific(pthread_key_t key) {
    if (key >= TTAK_MAX_TLS_KEYS) return NULL;
    return g_tls_slots[key];
}

/* --- attribute stubs --- */
int pthread_attr_init(pthread_attr_t *attr) { (void)attr; return 0; }
int pthread_attr_destroy(pthread_attr_t *attr) { (void)attr; return 0; }
int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize) {
    (void)attr; (void)stacksize; return 0;
}

/* --- rwlock attr stubs --- */
int pthread_rwlockattr_init(pthread_rwlockattr_t *a) { (void)a; return 0; }
int pthread_rwlockattr_destroy(pthread_rwlockattr_t *a) { (void)a; return 0; }
int pthread_rwlockattr_setkind_np(pthread_rwlockattr_t *a, int pref) {
    (void)a; (void)pref; return 0;
}
