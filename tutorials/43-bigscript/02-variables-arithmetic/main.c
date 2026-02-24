#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ttak/script/bigscript.h>
#include <ttak/timing/timing.h>

int main() {
    const char *src = "fn main(seed, sn) { let x = 10; let y = 20; let z = (x + y) * 2 - 5; let quotient = z / 3; let remainder = z % 3; return quotient + remainder; }";

    uint64_t now = ttak_get_tick_count();
    ttak_bigscript_error_t err = {TTAK_BIGSCRIPT_ERR_NONE, NULL};

    ttak_bigscript_program_t *prog = ttak_bigscript_compile(src, NULL, NULL, &err, now);
    if (!prog) {
        printf("Compile error: %s\\n", err.message);
        return 1;
    }

    ttak_bigscript_vm_t *vm = ttak_bigscript_vm_create(NULL, now);
    ttak_bigint_t seed, sn;
    ttak_bigint_init_u64(&seed, 0, now);
    ttak_bigint_init_u64(&sn, 0, now);

    ttak_bigscript_value_t out;
    memset(&out, 0, sizeof(out));
    if (ttak_bigscript_eval_seed(prog, vm, &seed, &sn, &out, &err, now)) {
        if (out.value.type == TTAK_BIGSCRIPT_VAL_INT) {
            uint64_t res = 0;
            ttak_bigint_export_u64(&out.value.v.i, &res);
            printf("Result: %lu\\n", res);
        }
    }

    ttak_bigscript_value_free(&out, now);
    ttak_bigint_free(&seed, now);
    ttak_bigint_free(&sn, now);
    ttak_bigscript_vm_free(vm, now);
    ttak_bigscript_program_free(prog, now);

    return 0;
}
