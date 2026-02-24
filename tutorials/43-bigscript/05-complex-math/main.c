#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ttak/script/bigscript.h>
#include <ttak/timing/timing.h>

int main() {
    const char *src = "fn main(seed, sn) { let r = real(seed); let c = complex(r, 10); return c; }";

    uint64_t now = ttak_get_tick_count();
    ttak_bigscript_error_t err = {TTAK_BIGSCRIPT_ERR_NONE, NULL};
    ttak_bigscript_program_t *prog = ttak_bigscript_compile(src, NULL, NULL, &err, now);
    ttak_bigscript_vm_t *vm = ttak_bigscript_vm_create(NULL, now);

    ttak_bigint_t seed, sn;
    ttak_bigint_init_u64(&seed, 5, now);
    ttak_bigint_init_u64(&sn, 0, now);

    ttak_bigscript_value_t out;
    memset(&out, 0, sizeof(out));
    if (ttak_bigscript_eval_seed(prog, vm, &seed, &sn, &out, &err, now)) {
        if (out.value.type == TTAK_BIGSCRIPT_VAL_COMPLEX) {
            uint64_t re = 0, im = 0;
            ttak_bigint_export_u64(&out.value.v.c.real.mantissa, &re);
            ttak_bigint_export_u64(&out.value.v.c.imag.mantissa, &im);
            printf("Result: %lu + %lui\\n", re, im);
        }
    }

    ttak_bigscript_value_free(&out, now);
    ttak_bigint_free(&seed, now);
    ttak_bigint_free(&sn, now);
    ttak_bigscript_vm_free(vm, now);
    ttak_bigscript_program_free(prog, now);
    return 0;
}
