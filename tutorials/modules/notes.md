# Lesson Notes

Quick hints grouped by the lesson ranges listed in `CLONE_PATH.md`. Glance at these before you start a cluster of numbered lessons so you know common pitfalls.

## Lessons 01–02 (Orientation)
- Run the helper on both `.hlp` files early so you do not discover missing key bindings midway through the unsafe track.
- Keep a running text file for “What I learned” so each lesson can end with a snapshot + diff link.

## Lessons 03–08 (Core Data + Logging)
- Rebuild hash-table chaining exactly: buckets live in `ttak_ht_entry`.
- Container clone tip: re-implement push/pop with wrap-around arithmetic first, then add error checks.
- Logger: keep formatting tiny—`snprintf` + `fputs` is enough.

## Lessons 09–11 (Concurrency Primitives)
- Mutex/RWLock wrappers simply forward to pthread. The goal is to recognize which thread-safe blocks you must surround.
- For portable atomics, follow the fallback macro layout. Ensure `__TTAK_NEEDS_PORTABLE_STDATOMIC__` gets undefined at the end.

## Lessons 12–16 (Thread Pool & Async)
- Thread pool jobs carry `priority`; practice writing a min-heap to enforce it.
- Async scheduler uses `ttak_future_t`; draw a diagram that shows how promises resolve tasks.

## Lessons 17–19 (Timing + Limiters + Stats)
- Token bucket math: call `ttak_get_tick_count()` once per refill and clamp to capacity.
- Stats module uses running means—replicate accumulation formulas exactly.

## Lessons 20–22 (Memory Systems)
- Owners guard resources with RW locks. While clone-coding, write down which functions grab read vs write locks.
- Memory tree GC references `ttak_mem_tree_perform_cleanup`; treat it like a sweep pass triggered by epochs.

## Lessons 23–28 (Math & Security)
- Big-int: start with SSO copies, then add dynamic limbs. Validate `ttak_bigint_set_u256` by round-tripping.
- Sum-of-divisors: follow the new safe-mode policy; log the stage as you flip from fast to generic paths.

## Lessons 29–31 (Trees + AST)
- For b+trees, practise node split diagrams on paper—then translate into code.
- AST walker uses recursive descent plus a stack; rebuild in two passes to keep the function list small.

## Lessons 32–33 (Application Layer)
- Aliquot tracker integrates everything: start with queues & workers, then add ledger persistence, finally wire the logging/timing changes.

## Lessons 34–35 (Dangerous)
- `ttak_context_t` is only safe when one side owns the lock. Never assume both sides can free memory simultaneously.
- Read `tutorials/DANGEROUS/README.md` and the unsafe HLP file before clone-coding the bridge.
