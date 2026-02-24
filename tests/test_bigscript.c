#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ttak/script/bigscript.h>
#include <ttak/timing/timing.h>

void test_basic_arithmetic(void) {
    printf("[TEST] Basic Arithmetic\n");
    const char *src = 
        "fn main(seed, sn) {\n"
        "  let x = 10;\n"
        "  let y = 20;\n"
        "  return x + y * 2;\n"
        "}\n";
    
    uint64_t now = ttak_get_tick_count();
    ttak_bigscript_error_t err = {TTAK_BIGSCRIPT_ERR_NONE, NULL};
    ttak_bigscript_program_t *prog = ttak_bigscript_compile(src, NULL, NULL, &err, now);
    if (!prog) {
        printf("Compile error: %s\n", err.message ? err.message : "Unknown");
        exit(1);
    }
    
    ttak_bigscript_vm_t *vm = ttak_bigscript_vm_create(NULL, now);
    ttak_bigint_t seed, sn;
    ttak_bigint_init_u64(&seed, 100, now);
    ttak_bigint_init_u64(&sn, 200, now);
    
    ttak_bigscript_value_t out;
    memset(&out, 0, sizeof(out));
    bool ok = ttak_bigscript_eval_seed(prog, vm, &seed, &sn, &out, &err, now);
    assert(ok);
    
    uint64_t res = 0;
    if (out.value.type == TTAK_BIGSCRIPT_VAL_INT) {
        ttak_bigint_export_u64(&out.value.v.i, &res);
    }
    printf("Result: %lu (Expected: 50)\n", res);
    assert(res == 50);
    
    ttak_bigscript_value_free(&out, now);
    ttak_bigint_free(&seed, now);
    ttak_bigint_free(&sn, now);
    ttak_bigscript_vm_free(vm, now);
    ttak_bigscript_program_free(prog, now);
}

void test_builtin_s(void) {
    printf("[TEST] Builtin s(n)\n");
    const char *src = 
        "fn main(seed, sn) {\n"
        "  if (s(seed) == sn) {\n"
        "    return 1;\n"
        "  }\n"
        "  return 0;\n"
        "}\n";
    
    uint64_t now = ttak_get_tick_count();
    ttak_bigscript_error_t err = {TTAK_BIGSCRIPT_ERR_NONE, NULL};
    ttak_bigscript_program_t *prog = ttak_bigscript_compile(src, NULL, NULL, &err, now);
    assert(prog);
    
    ttak_bigscript_vm_t *vm = ttak_bigscript_vm_create(NULL, now);
    ttak_bigint_t seed, sn;
    // seed = 6, s(6) = 1+2+3 = 6
    ttak_bigint_init_u64(&seed, 6, now);
    ttak_bigint_init_u64(&sn, 6, now);
    
    ttak_bigscript_value_t out;
    memset(&out, 0, sizeof(out));
    bool ok = ttak_bigscript_eval_seed(prog, vm, &seed, &sn, &out, &err, now);
    assert(ok);
    assert(out.is_found == true);
    
    ttak_bigscript_value_free(&out, now);
    ttak_bigint_free(&seed, now);
    ttak_bigint_free(&sn, now);
    ttak_bigscript_vm_free(vm, now);
    ttak_bigscript_program_free(prog, now);
}

void test_constant_return(void) {
    printf("[TEST] Constant Return\n");
    const char *src = "fn main(seed, sn) { return 50; }\n";
    uint64_t now = ttak_get_tick_count();
    ttak_bigscript_error_t err = {TTAK_BIGSCRIPT_ERR_NONE, NULL};
    ttak_bigscript_program_t *prog = ttak_bigscript_compile(src, NULL, NULL, &err, now);
    assert(prog);
    ttak_bigscript_vm_t *vm = ttak_bigscript_vm_create(NULL, now);
    ttak_bigint_t seed, sn;
    ttak_bigint_init_u64(&seed, 0, now); ttak_bigint_init_u64(&sn, 0, now);
    
    ttak_bigscript_value_t out;
    memset(&out, 0, sizeof(out));
    ttak_bigscript_eval_seed(prog, vm, &seed, &sn, &out, &err, now);
    uint64_t res = 0;
    if (out.value.type == TTAK_BIGSCRIPT_VAL_INT) {
        ttak_bigint_export_u64(&out.value.v.i, &res);
    }
    printf("Result: %lu (Expected: 50)\n", res);
    assert(res == 50);
    ttak_bigscript_value_free(&out, now); ttak_bigint_free(&seed, now); ttak_bigint_free(&sn, now);
    ttak_bigscript_vm_free(vm, now); ttak_bigscript_program_free(prog, now);
}

void test_real_complex(void) {
    printf("[TEST] Real and Complex\n");
    const char *src = 
        "fn main(seed, sn) {\n"
        "  let r = real(seed);\n"
        "  let c = complex(r, 10);\n"
        "  return c;\n"
        "}\n";
    
    uint64_t now = ttak_get_tick_count();
    ttak_bigscript_error_t err = {TTAK_BIGSCRIPT_ERR_NONE, NULL};
    ttak_bigscript_program_t *prog = ttak_bigscript_compile(src, NULL, NULL, &err, now);
    assert(prog);
    
    ttak_bigscript_vm_t *vm = ttak_bigscript_vm_create(NULL, now);
    ttak_bigint_t seed, sn;
    ttak_bigint_init_u64(&seed, 5, now);
    ttak_bigint_init_u64(&sn, 0, now);
    
    ttak_bigscript_value_t out;
    memset(&out, 0, sizeof(out));
    bool ok = ttak_bigscript_eval_seed(prog, vm, &seed, &sn, &out, &err, now);
    assert(ok);
    assert(out.value.type == TTAK_BIGSCRIPT_VAL_COMPLEX);
    
    uint64_t re = 0, im = 0;
    ttak_bigint_export_u64(&out.value.v.c.real.mantissa, &re);
    ttak_bigint_export_u64(&out.value.v.c.imag.mantissa, &im);
    printf("Result: %lu + %lui\n", re, im);
    assert(re == 5);
    assert(im == 10);
    
    ttak_bigscript_value_free(&out, now);
    ttak_bigint_free(&seed, now);
    ttak_bigint_free(&sn, now);
    ttak_bigscript_vm_free(vm, now);
    ttak_bigscript_program_free(prog, now);
}

int main(void) {
    test_constant_return();
    test_basic_arithmetic();
    test_builtin_s();
    test_real_complex();
    printf("All bigscript tests passed!\n");
    return 0;
}
