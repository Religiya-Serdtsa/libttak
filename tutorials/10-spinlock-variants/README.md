# Tutorial 10 – Spinlock Variants

[`modules/10-spinlock-variants`](../modules/10-spinlock-variants/README.md) focuses on the fast-path locks built in `src/sync/spinlock.c`.

`lesson10_spinlock_variants.c` lets you profile lock latency quickly; recompile it as you tune the backoff or memory-ordering rules.

## Checklist

1. List the constraints for `ttak_spin_t` (initial state, atomic flag usage, fairness knobs) so you have a target while coding.
2. Recreate `ttak_spin_init`, `ttak_spin_lock`, `ttak_spin_unlock`, and any variant helpers described in the lesson.
3. Validate the implementation with `make tests/test_sync` plus any custom stress harness you keep in this folder.
4. Capture measurements or flame graphs if you benchmark different backoff strategies so you can justify your choices later.
