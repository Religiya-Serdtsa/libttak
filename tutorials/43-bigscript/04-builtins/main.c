#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ttak/script/bigscript.h>
#include <ttak/timing/timing.h>

void test_perfect(ttak_bigscript_program_t *prog, ttak_bigscript_vm_t *vm, uint64_t val, uint64_t s_val) {
    uint64_t now = ttak_get_tick_count();
    ttak_bigscript_error_t err = {TTAK_BIGSCRIPT_ERR_NONE, NULL};
    ttak_bigint_t seed, sn;
    ttak_bigint_init_u64(&seed, val, now);
    ttak_bigint_init_u64(&sn, s_val, now);

    ttak_bigscript_value_t out;
    memset(&out, 0, sizeof(out));
    if (ttak_bigscript_eval_seed(prog, vm, &seed, &sn, &out, &err, now)) {
        uint64_t res = 0;
        ttak_bigint_export_u64(&out.value.v.i, &res);
        printf("Seed %lu (sn=%lu) -> Result: %lu\\n", val, s_val, res);
    }
    ttak_bigscript_value_free(&out, now);
    ttak_bigint_free(&seed, now);
    ttak_bigint_free(&sn, now);
}

int main() {
    const char *src = "fn main(seed, sn) { if (s(seed) == seed) { return 1; } if (sn == seed) { return 2; } return 0; }";

    uint64_t now = ttak_get_tick_count();
    ttak_bigscript_error_t err = {TTAK_BIGSCRIPT_ERR_NONE, NULL};
    ttak_bigscript_program_t *prog = ttak_bigscript_compile(src, NULL, NULL, &err, now);
    ttak_bigscript_vm_t *vm = ttak_bigscript_vm_create(NULL, now);

    // 6 is perfect: 1+2+3 = 6
    test_perfect(prog, vm, 6, 6);
    // 10 is not perfect: 1+2+5 = 8
    test_perfect(prog, vm, 10, 8);

    ttak_bigscript_vm_free(vm, now);
    ttak_bigscript_program_free(prog, now);
    return 0;
}
