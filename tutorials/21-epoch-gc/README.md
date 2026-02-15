# Tutorial 21 – Epoch GC

[`modules/21-epoch-gc`](../modules/21-epoch-gc/README.md) covers the deferred-free system inside `src/mem/epoch_gc.c`.

`lesson21_epoch_gc.c` is a tiny harness for registering allocations and rotating epochs—use it every time you modify reclamation logic.

## Checklist

1. Capture the invariants around epochs, pin counts, and batch freeing so you have a checklist ready before touching code.
2. Clone init/register/rotate/destroy and ensure they integrate with `ttak_mem_alloc`'s expectations.
3. Execute `make tests/test_epoch_gc` to run the epoch GC regression plus any stress harness you add here.
4. Write down how you confirmed memory is reclaimed only after all participants advance past the epoch.
