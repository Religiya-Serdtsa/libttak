# Tutorial 09 – Mutex Primitives

This workspace mirrors [`modules/09-mutex-primitives`](../modules/09-mutex-primitives/README.md) where you re-implement the thin wrappers in `src/sync/sync.c`.

`lesson09_mutex_primitives.c` is a micro app that takes the mutex through its lifecycle—build/run it as you verify lock/unlock/teardown paths.

## Checklist

1. Capture the invariants for `ttak_mutex_t` (zeroed struct, pthread backing, error handling) in a note under this folder.
2. Rebuild `ttak_mutex_init`, `ttak_mutex_lock`, `ttak_mutex_unlock`, and `ttak_mutex_destroy`, paying attention to how failures are reported upstream.
3. Use `make tests/test_sync` to run the synchronization regression suite after every major change.
4. Log any scheduler interactions or priority-inversion considerations you want to revisit during the worker/thread lessons.
