# Tutorial 34 – Dangerous Primer

[`modules/34-dangerous-primer`](../modules/34-dangerous-primer/README.md) is the opt-in introduction to the unsafe portion of the library.

`lesson34_dangerous_primer.c` touches `ttak_unsafe_region_t`; build it only after you have read `tutorials/DANGEROUS/README.md` and paged through `libttak_unsafe.hlp`.

## Checklist

1. Re-read the DANGEROUS README plus the helper manual, then jot down the ownership/locking rules in this folder.
2. Clone the unsafe region helpers carefully (init, adopt, pin/unpin, reset) and document every macro you toggle so you can revert if needed.
3. Extend the lesson driver with extra asserts so you can prove (to yourself) that pins are balanced and buffers are released.
4. Keep a risk log here noting anything you still find confusing before you advance to the context bridge.
