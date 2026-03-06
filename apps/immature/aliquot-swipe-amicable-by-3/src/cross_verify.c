#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <ttak/math/bigint.h>
#include <ttak/math/sum_divisors.h>
#include <ttak/mem/mem.h>
#include <ttak/timing/timing.h>

#define EXPECT_SKIP UINT64_MAX

typedef struct {
    uint64_t seed_val;
    const char *description;
    uint64_t expected_values[3];
    bool expect_cycle_len3;
} cross_case_t;

static cross_case_t g_cases[] = {
    {1264460ULL, "Sociable length-4 cycle (node 1)",   {1547860ULL, 1727636ULL, 1305184ULL},   false},
    {1547860ULL, "Sociable length-4 cycle (node 2)",   {1727636ULL, 1305184ULL, 1264460ULL},   false},
    {1727636ULL, "Sociable length-4 cycle (node 3)",   {1305184ULL, 1264460ULL, 1547860ULL},   false},
    {1305184ULL, "Sociable length-4 cycle (node 4)",   {1264460ULL, 1547860ULL, 1727636ULL},   false},
    {1184ULL,    "Classic amicable pair entry",        {1210ULL, 1184ULL, 1210ULL},            false},
    {8128ULL,    "Perfect number sanity check",        {8128ULL, 8128ULL, 8128ULL},            false},
    {12496ULL,   "Sociable length-5 member",           {14288ULL, 15472ULL, 14536ULL},         false},
    {10ULL,      "Terminating chain (-> 0 after 4)",   {8ULL, 7ULL, 1ULL},                     false},
    {12ULL,      "Terminating chain (-> 0)",           {16ULL, 15ULL, 9ULL},                   false},
};

static void free_state_array(ttak_bigint_t *states, size_t count, uint64_t now) {
    if (!states) return;
    for (size_t i = 0; i < count; ++i) {
        ttak_bigint_free(&states[i], now);
    }
}

static void print_state_line(const char *label, const ttak_bigint_t *value, uint64_t now) {
    uint64_t as_u64 = 0;
    if (ttak_bigint_export_u64(value, &as_u64)) {
        printf("    %s: %" PRIu64 "\n", label, as_u64);
        return;
    }
    char *repr = ttak_bigint_to_string(value, now);
    if (repr) {
        printf("    %s: %s\n", label, repr);
        ttak_mem_free(repr);
    } else {
        printf("    %s: <conversion failed>\n", label);
    }
}

static bool run_case_logic(const cross_case_t *tc, size_t idx) {
    uint64_t now = ttak_get_tick_count();
    printf("Case %zu: %s (seed=%" PRIu64 ")\n", idx + 1, tc->description, tc->seed_val);

    ttak_bigint_t states[4];
    ttak_bigint_init_u64(&states[0], tc->seed_val, now);
    for (size_t i = 1; i < 4; ++i) {
        ttak_bigint_init(&states[i], now);
    }

    bool fatal = false;
    for (size_t step = 0; step < 3; ++step) {
        ttak_bigint_t tmp, input;
        ttak_bigint_init(&tmp, now);
        ttak_bigint_init_copy(&input, &states[step], now);
        if (!ttak_sum_proper_divisors_big(&input, &tmp, now)) {
            fprintf(stderr, "  [error] failed to compute s^%zu(n)\n", step + 1);
            fatal = true;
            ttak_bigint_free(&input, now);
            ttak_bigint_free(&tmp, now);
            break;
        }
        ttak_bigint_free(&input, now);
        ttak_bigint_copy(&states[step + 1], &tmp, now);
        ttak_bigint_free(&tmp, now);
    }

    bool cycle3 = false;
    bool steps_match = !fatal;
    if (!fatal) {
        bool returns_to_seed = (ttak_bigint_cmp(&states[3], &states[0]) == 0);
        bool s1_diff = (ttak_bigint_cmp(&states[1], &states[0]) != 0);
        bool s2_diff = (ttak_bigint_cmp(&states[2], &states[0]) != 0);
        cycle3 = returns_to_seed && s1_diff && s2_diff;

        for (size_t step = 0; step < 3 && steps_match; ++step) {
            uint64_t expected = tc->expected_values[step];
            if (expected == EXPECT_SKIP) continue;
            if (ttak_bigint_cmp_u64(&states[step + 1], expected) != 0) {
                steps_match = false;
            }
        }
    }

    print_state_line("n", &states[0], now);
    print_state_line("s(n)", &states[1], now);
    print_state_line("s^2(n)", &states[2], now);
    print_state_line("s^3(n)", &states[3], now);
    printf("    expect cycle len 3: %s | observed: %s\n",
           tc->expect_cycle_len3 ? "yes" : "no",
           cycle3 ? "yes" : "no");

    bool ok = steps_match && (cycle3 == tc->expect_cycle_len3);
    printf("    STATUS: %s\n\n", ok ? "PASS" : "FAIL");
    free_state_array(states, 4, now);
    fflush(stdout);
    fflush(stderr);
    return ok;
}

int main(void) {
    size_t total = sizeof(g_cases) / sizeof(g_cases[0]);
    size_t passed = 0;

    printf("[cross-verify] Aliquot sociable-3 sanity suite\n");
    printf("[cross-verify] Compiled: %s\n\n", __TIMESTAMP__);
    fflush(stdout);

    for (size_t idx = 0; idx < total; ++idx) {
        const cross_case_t *tc = &g_cases[idx];
        fflush(stdout);
        fflush(stderr);
        pid_t child = fork();
        if (child == 0) {
            bool ok = run_case_logic(tc, idx);
            _exit(ok ? EXIT_SUCCESS : EXIT_FAILURE);
        } else if (child < 0) {
            fprintf(stderr, "[cross-verify] fork() failed (errno=%d). Falling back to inline run.\n", errno);
            if (run_case_logic(tc, idx)) {
                passed++;
            }
            continue;
        }

        int status = 0;
        if (waitpid(child, &status, 0) < 0) {
            fprintf(stderr, "[cross-verify] waitpid failed (errno=%d). Treating case as failed.\n", errno);
            continue;
        }
        if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS) {
            passed++;
        }
    }

    printf("[cross-verify] Result: %zu/%zu cases passed\n", passed, total);
    return (passed == total) ? EXIT_SUCCESS : EXIT_FAILURE;
}
