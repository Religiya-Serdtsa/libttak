#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>

#include <ttak/mem/mem.h>
#include <ttak/math/bigint.h>
#include <ttak/math/ntt.h>
#include <ttak/timing/timing.h>
#include <ttak/atomic/atomic.h>
#include "../internal/app_types.h"
#include "hwinfo.h"

#define LOCAL_STATE_DIR "/home/yjlee/Documents"
#define LOCAL_STATE_FILE LOCAL_STATE_DIR "/found_mersenne.json"
#define MAX_WORKERS 8

static volatile atomic_bool shutdown_requested = false;
static atomic_uint g_next_p;
static ttak_hw_spec_t g_hw_spec;
static app_state_t *g_app_state = NULL;

typedef struct {
    alignas(64) atomic_uint_fast64_t ops_count;
    uint32_t id;
} worker_ctx_t;

static worker_ctx_t g_workers[MAX_WORKERS];
static int g_num_workers = 4;

extern void save_current_progress(const char *filename, const void *data, size_t size);
extern int report_to_gimps(app_state_t *state, const gimps_result_t *res, const ttak_node_telemetry_t *telemetry);
extern void generate_computer_id(char *buf, size_t len);

static void handle_signal(int sig) { (void)sig; atomic_store(&shutdown_requested, true); }

/**
 * @brief LL test core with residue extraction for GIMPS reporting.
 */
static int ttak_ll_test_core(uint32_t p, uint64_t *out_residue) {
    if (p == 2) { *out_residue = 0; return 1; }

    size_t n = (p + 63) / 64;
    size_t ntt_size = ttak_next_power_of_two(n * 2);

    uint64_t *s_words = (uint64_t *)ttak_mem_alloc_safe(ntt_size * sizeof(uint64_t), 0, 0, false, false, true, TTAK_MEM_CACHE_ALIGNED);
    uint64_t *tmp_res[TTAK_NTT_PRIME_COUNT];
    for (int i = 0; i < TTAK_NTT_PRIME_COUNT; i++) {
    	tmp_res[i] = (uint64_t *)ttak_mem_alloc_safe(ntt_size * sizeof(uint64_t), 0, 0, false, false, true, TTAK_MEM_CACHE_ALIGNED);
    }

    memset(s_words, 0, ntt_size * sizeof(uint64_t));
    s_words[0] = 4;

    for (uint32_t i = 0; i < p - 2; i++) {
    	if (atomic_load(&shutdown_requested)) goto cancel;

    	for (int k = 0; k < TTAK_NTT_PRIME_COUNT; k++) {
    		memcpy(tmp_res[k], s_words, n * sizeof(uint64_t));
    		if (ntt_size > n) memset(tmp_res[k] + n, 0, (ntt_size - n) * sizeof(uint64_t));

    		ttak_ntt_transform(tmp_res[k], ntt_size, &ttak_ntt_primes[k], false);
    		ttak_ntt_pointwise_square(tmp_res[k], tmp_res[k], ntt_size, &ttak_ntt_primes[k]);
    		ttak_ntt_transform(tmp_res[k], ntt_size, &ttak_ntt_primes[k], true);
    	}

    	unsigned __int128 carry = 0;
    	for (size_t j = 0; j < ntt_size; j++) {
    		ttak_crt_term_t terms[TTAK_NTT_PRIME_COUNT];
    		for (int k = 0; k < TTAK_NTT_PRIME_COUNT; k++) {
    			terms[k].modulus = ttak_ntt_primes[k].modulus;
    			terms[k].residue = tmp_res[k][j];
    		}
    		ttak_u128_t res128, mod128;
    		ttak_crt_combine(terms, TTAK_NTT_PRIME_COUNT, &res128, &mod128);
    		unsigned __int128 full_val = ((unsigned __int128)res128.hi << 64) | res128.lo;
    		full_val += carry;
    		s_words[j] = (uint64_t)full_val;
    		carry = full_val >> 64;
    	}

    	if (s_words[0] >= 2) s_words[0] -= 2;
    	else s_words[0] = s_words[0] + (uint64_t)-1 - 2;
    }

    *out_residue = s_words[0];
    int is_prime = (s_words[0] == 0);

    cleanup:
    ttak_mem_free(s_words);
    for (int i = 0; i < TTAK_NTT_PRIME_COUNT; i++) ttak_mem_free(tmp_res[i]);
    return is_prime;
    cancel:
    is_prime = -1;
    goto cleanup;
}

void* worker_thread(void* arg) {
    worker_ctx_t *ctx = (worker_ctx_t*)arg;
    while (!atomic_load(&shutdown_requested)) {
    	uint32_t p = atomic_fetch_add(&g_next_p, 2);

    	/* Primality check for exponent p */
    	bool p_is_prime = true;
    	if (p < 2) p_is_prime = false;
    	else if (p % 2 == 0) p_is_prime = (p == 2);
    	else {
    		for (uint32_t i = 3; i * i <= p; i += 2) {
    			if (p % i == 0) { p_is_prime = false; break; }
    		}
    	}
    	if (!p_is_prime) continue;

    	uint64_t res_val = 0;
    	int ll_res = ttak_ll_test_core(p, &res_val);

    	if (ll_res != -1) {
    		gimps_result_t report = { .p = p, .residue = res_val, .is_prime = (ll_res == 1), .status = 0 };
    		ttak_node_telemetry_t tel = { .exponent_in_progress = p, .spec = g_hw_spec };

    		/* Integrated PrimeNet reporting using the actual gateway signature */
    		report_to_gimps(g_app_state, &report, &tel);
    	}
    	atomic_fetch_add_explicit(&ctx->ops_count, 1, memory_order_relaxed);
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc > 1) g_num_workers = atoi(argv[1]);
    struct sigaction sa = {.sa_handler = handle_signal};
    sigaction(SIGINT, &sa, NULL); sigaction(SIGTERM, &sa, NULL);

    ttak_collect_hw_spec(&g_hw_spec);
    mkdir(LOCAL_STATE_DIR, 0755);

    /* Initialize global app state and ID */
    g_app_state = (app_state_t *)ttak_mem_alloc_safe(sizeof(app_state_t), 0, 0, false, false, true, TTAK_MEM_CACHE_ALIGNED);
    generate_computer_id(g_app_state->computerid, sizeof(g_app_state->computerid));
    strncpy(g_app_state->userid, "yjlee", sizeof(g_app_state->userid) - 1);

    /* Progress recovery logic */
    uint32_t resume_p = 3;
    FILE *rf = fopen(LOCAL_STATE_FILE, "r");
    if (rf) {
    	char line[256];
    	while (fgets(line, sizeof(line), rf)) {
    		char *ptr = strstr(line, "\"last_p\"");
    		if (ptr && (ptr = strchr(ptr, ':'))) {
    			resume_p = (uint32_t)atoi(ptr + 1);
    			resume_p = (resume_p % 2 == 0) ? resume_p + 1 : resume_p + 2;
    		}
    	}
    	fclose(rf);
    }

    atomic_store(&g_next_p, resume_p);
    pthread_t threads[MAX_WORKERS];
    for (int i = 0; i < g_num_workers; i++) {
    	g_workers[i].id = (uint32_t)i;
    	atomic_init(&g_workers[i].ops_count, 0);
    	pthread_create(&threads[i], NULL, worker_thread, &g_workers[i]);
    }

    while (!atomic_load(&shutdown_requested)) {
    	usleep(1000000);
    	uint64_t total_ops = 0;
    	for (int i = 0; i < g_num_workers; i++) total_ops += atomic_load_explicit(&g_workers[i].ops_count, memory_order_relaxed);

    	uint32_t current_p = atomic_load(&g_next_p);
    	printf("[libttak] p: %u | total_ops: %lu\n", current_p, total_ops);
    	fflush(stdout);

    	char json_buf[256];
    	int len = snprintf(json_buf, sizeof(json_buf), "{\n  \"last_p\": %u,\n  \"total_ops\": %lu\n}\n", current_p, total_ops);
    	if (len > 0 && len < (int)sizeof(json_buf)) save_current_progress(LOCAL_STATE_FILE, json_buf, (size_t)len);
    }

    for (int i = 0; i < g_num_workers; i++) pthread_join(threads[i], NULL);
    ttak_mem_free(g_app_state);
    return 0;
}
