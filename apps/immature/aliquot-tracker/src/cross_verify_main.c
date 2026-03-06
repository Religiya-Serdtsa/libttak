#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include <ttak/mem/mem.h>
#include <ttak/timing/timing.h>
#include <ttak/math/bigint.h>
#include <ttak/math/sum_divisors.h>

// Define simplified outcome structure for cross-verification
typedef struct {
    ttak_bigint_t seed;
    bool perfect;
    bool amicable;
    uint32_t cycle_length; // For sociable numbers, 0 if not cycle
    bool terminated;
    uint64_t final_val_u64; // Only for small terminated values
} simple_outcome_t;

// Structure for a known test case
typedef struct {
    uint64_t seed_val;
    bool expected_perfect;
    bool expected_amicable;
    uint32_t expected_cycle_length;
    bool expected_terminated;
    uint64_t expected_final_val_u64; // Use 0 if not terminated or too large
    const char *description;
} known_test_case_t;

// Simplified aliquot sequence runner for cross-verification
static void run_aliquot_sequence_simple(const ttak_bigint_t *seed, simple_outcome_t *out) {
    memset(out, 0, sizeof(*out));
    uint64_t now = ttak_get_tick_count();
    ttak_bigint_init_copy(&out->seed, seed, now);

    ttak_bigint_t current;
    ttak_bigint_init_copy(&current, seed, now);

    ttak_bigint_t history_vals[200]; // Keep a limited history for cycle detection
    int history_idx = 0;
    int max_history = sizeof(history_vals) / sizeof(history_vals[0]);
    for(int i = 0; i < max_history; i++) ttak_bigint_init(&history_vals[i], now);

    ttak_bigint_copy(&history_vals[history_idx++], &current, now);

    int steps = 0;
    while (steps < 1000) { // Limit steps to prevent infinite loops for unknown cases
        ttak_bigint_t next;
        ttak_bigint_init(&next, now);
        
        if (!ttak_sum_proper_divisors_big(&current, &next, now)) {
            // Overflow or error
            ttak_bigint_free(&next, now);
            break;
        }

        if (ttak_bigint_is_zero(&next) || ttak_bigint_cmp_u64(&next, 1) == 0) {
            out->terminated = true;
            uint64_t final_val;
            if (ttak_bigint_export_u64(&next, &final_val)) {
                out->final_val_u64 = final_val;
            } else {
                out->final_val_u64 = (uint64_t)-1; // Indicate large value
            }
            ttak_bigint_free(&next, now);
            break;
        }

        // Check for cycle in history
        for (int i = 0; i < history_idx; ++i) {
            if (ttak_bigint_cmp(&history_vals[i], &next) == 0) {
                out->cycle_length = history_idx - i;
                if (out->cycle_length == 1) out->perfect = true;
                else if (out->cycle_length == 2) out->amicable = true;
                break;
            }
        }
        if (out->cycle_length > 0) {
            ttak_bigint_free(&next, now);
            break;
        }

        steps++;
        if (history_idx < max_history) {
            ttak_bigint_copy(&history_vals[history_idx++], &next, now);
        } else {
            // History full, assume it's a long sequence, not terminating soon or small cycle
            // For cross-verification, we expect small cycles/terminations
        }

        ttak_bigint_free(&current, now);
        current = next;
    }

    ttak_bigint_free(&current, now);
    for(int i = 0; i < max_history; i++) ttak_bigint_free(&history_vals[i], now);
}

// Known test cases
// For expected_final_val_u64: 0 for terminated to 0, 1 for terminated to 1. 0 if cycle.
static known_test_case_t known_cases[] = {
    // Perfect Numbers (cycle length 1)
    {6,      true, false, 1, false, 0, "First Perfect Number"},
    {28,     true, false, 1, false, 0, "Second Perfect Number"},
    {496,    true, false, 1, false, 0, "Third Perfect Number"},
    {8128,   true, false, 1, false, 0, "Fourth Perfect Number"},

    // Amicable Pairs (cycle length 2) - only test one from each pair
    {220,    false, true,  2, false, 0, "First Amicable Pair (220)"},
    {1184,   false, true,  2, false, 0, "Second Amicable Pair (1184)"},
    {2620,   false, true,  2, false, 0, "Third Amicable Pair (2620)"},

    // Sociable Number (cycle length 5)
    {12496,  false, false, 5, false, 0, "Sociable Cycle of length 5 (12496)"},

    // Terminated Sequences (ending in 0 or 1)
    {10,     false, false, 0, true,  0, "Terminates to 0 (10, 8, 7, 1, 0)"},
    {12,     false, false, 0, true,  0, "Terminates to 0 (12, 16, 15, 9, 4, 3, 1, 0)"},
    {95,     false, false, 0, true,  0, "Terminates to 0 (95, 25, 6, 6, 6, 6 -> perfect 6)"}, // This one will eventually hit 6, then loop at 6.

    // Sequences leading to 1
    {2,      false, false, 0, true,  1, "Terminates to 1 (2, 1)"},
    {3,      false, false, 0, true,  1, "Terminates to 1 (3, 1)"},
    {4,      false, false, 0, true,  1, "Terminates to 1 (4, 3, 1)"},

    // Example of a longer sequence that should terminate eventually but not immediately
    // 138: 138, 138, ... (This one is expected to diverge according to Wikipedia, but my simplified logic will stop at 1000 steps or detect an overflow)
    // For now, let's stick to smaller, well-defined cases.

    // Add a few examples that are "long" but not cycles for simple runner
    {276,    false, false, 0, false, 0, "Long sequence (Guy-Selfridge counter-conjecture)"}
};


int main(void) {
    printf("--- Aliquot Tracker Cross-Verification Report ---
");
    printf("Generated: %s
", __TIMESTAMP__);
    printf("-------------------------------------------------

");

    int passed_tests = 0;
    int total_tests = sizeof(known_cases) / sizeof(known_test_case_t);

    for (int i = 0; i < total_tests; ++i) {
        known_test_case_t tc = known_cases[i];
        uint64_t now = ttak_get_tick_count();
        ttak_bigint_t seed_bi;
        ttak_bigint_init_u64(&seed_bi, tc.seed_val, now);
        
        simple_outcome_t outcome;
        run_aliquot_sequence_simple(&seed_bi, &outcome);

        bool test_passed = true;
        char *s_seed = ttak_bigint_to_string(&seed_bi, now);

        printf("Test Case: %s (Seed: %s)
", tc.description, s_seed);
        printf("  Expected: Perfect=%d, Amicable=%d, CycleLen=%u, Terminated=%d, FinalVal=%llu
",
               tc.expected_perfect, tc.expected_amicable, tc.expected_cycle_length, tc.expected_terminated, tc.expected_final_val_u64);
        printf("  Actual:   Perfect=%d, Amicable=%d, CycleLen=%u, Terminated=%d, FinalVal=%llu
",
               outcome.perfect, outcome.amicable, outcome.cycle_length, outcome.terminated, outcome.final_val_u64);

        if (outcome.perfect != tc.expected_perfect) test_passed = false;
        if (outcome.amicable != tc.expected_amicable) test_passed = false;
        if (outcome.cycle_length != tc.expected_cycle_length) test_passed = false;
        if (outcome.terminated != tc.expected_terminated) test_passed = false;
        if (outcome.terminated && outcome.final_val_u64 != tc.expected_final_val_u64) test_passed = false;

        if (test_passed) {
            printf("  STATUS: PASSED

");
            passed_tests++;
        } else {
            printf("  STATUS: FAILED

");
        }
        
        ttak_bigint_free(&seed_bi, now);
        ttak_bigint_free(&outcome.seed, now); // Free the copy in outcome
        if (s_seed) ttak_mem_free(s_seed);
    }

    printf("-------------------------------------------------
");
    printf("Summary: %d/%d tests passed.
", passed_tests, total_tests);
    printf("-------------------------------------------------
");

    return (passed_tests == total_tests) ? 0 : 1;
}