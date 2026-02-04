# DANGEROUS Tutorials

Unsafe components live here. Complete these after mastering the safe stages.

## Scope

- `ttak_context_t` shared-memory bridge (`include/ttak/unsafe/context.h`, `src/unsafe/context.c`)
- Portable stdatomic fallback (`include/stdatomic.h`, `temp_include/stdatomic.h`)
- Any code compiled with `__TTAK_UNSAFE_*__` flags or macros

## Why it is dangerous

1. **Ownership inheritance:** only one side may own locks or free memory at a time. If you skip the lock discipline, shared memory corrupts instantly.
2. **Macro contracts:** macros prefixed with `__TTAK_*__` flip behavior globally. Failing to reset them can break every module.
3. **Bridge lock:** the context bridge forces all calls through a single mutex so the wrong owner never runs `free`. Modifying it casually defeats the safety guarantee.

## Tutorial Outline

1. Study `libttak_unsafe.hlp` with the helper (`./helper ../DANGEROUS/libttak_unsafe.hlp`).
2. Clone `src/unsafe/context.c` and `src/unsafe/region.c`, focusing on lock ordering and the three region move patterns (destructive move, adopt, steal).
3. Re-run the helper and mark any confusing paragraphs; review the output in `marked_explanations.txt`.
4. Write a short note about what actions remain forbidden (e.g., “second owner may not release memory while first owns the lock”).

Once you demonstrate that you can follow the lock/ownership rules, you can continue to contribute to unsafe modules.
