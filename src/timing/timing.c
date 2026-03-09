#include <ttak/timing/timing.h>
#include <time.h>
#include <pthread.h>
#ifdef _WIN32
    #include <windows.h>
    #include <stdbool.h>
#endif

#if defined(__x86_64__) || defined(_M_X64)
uint64_t g_tsc_freq_ghz = 0;
uint64_t g_tsc_scale = 0; // Fixed point 32.32
static pthread_mutex_t g_tsc_mutex = PTHREAD_MUTEX_INITIALIZER;

void calibrate_tsc(void) {
    if (g_tsc_freq_ghz != 0) return;

    pthread_mutex_lock(&g_tsc_mutex);
    if (g_tsc_freq_ghz == 0) {
        uint64_t tsc_start, tsc_end;
        double elapsed;

#ifdef _WIN32
        LARGE_INTEGER qpf, qpc_start, qpc_end;
        QueryPerformanceFrequency(&qpf);
        QueryPerformanceCounter(&qpc_start);
        tsc_start = __rdtsc();

        Sleep(10); // 10ms

        QueryPerformanceCounter(&qpc_end);
        tsc_end = __rdtsc();

        elapsed = (double)(qpc_end.QuadPart - qpc_start.QuadPart) / (double)qpf.QuadPart;
#else
        struct timespec ts_start, ts_end;
        clock_gettime(CLOCK_MONOTONIC, &ts_start);
        tsc_start = __rdtsc();

        struct timespec sleep_ts = {0, 10000000}; // 10ms
        nanosleep(&sleep_ts, NULL);

        clock_gettime(CLOCK_MONOTONIC, &ts_end);
        tsc_end = __rdtsc();

        elapsed = (double)(ts_end.tv_sec - ts_start.tv_sec) + (double)(ts_end.tv_nsec - ts_start.tv_nsec) * 1e-9;
#endif

        if (elapsed > 0) {
            double freq_hz = (double)(tsc_end - tsc_start) / elapsed;
            uint64_t freq_ghz = (uint64_t)(freq_hz / 1e9);
            if (freq_ghz == 0) freq_ghz = 2;
            g_tsc_freq_ghz = freq_ghz;
            g_tsc_scale = (uint64_t)((1e9 * (1ULL << 32)) / freq_hz);
        } else {
            g_tsc_freq_ghz = 2;
            g_tsc_scale = (1ULL << 32) / 2;
        }
    }
    pthread_mutex_unlock(&g_tsc_mutex);
}
#endif

#ifdef _WIN32
uint64_t ttak_get_tick_count_ns_win32(void) {
    static LARGE_INTEGER freq;
    static bool init = false;
    if (!init) {
        QueryPerformanceFrequency(&freq);
        init = true;
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000000000LL) / freq.QuadPart);
}
#endif
