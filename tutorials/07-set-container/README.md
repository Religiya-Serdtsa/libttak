# Tutorial 07 – Set Container

[`modules/07-set-container`](../modules/07-set-container/README.md) asks you to wrap the generic hash table into a key-only set stored in `src/container/set.c`.

`lesson07_set_container.c` (build with `make`/`make run`) gives you a scratch driver for experimenting with `ttak_set_add`/`contains`/`remove` as you sort out placeholder values and ownership.

## Checklist

1. Sketch how `ttak_set_t` delegates to `ttak_table_t`, including when the wrapper must free keys or simply forward `now` to the table.
2. Decide what sentinel value you will store in the table to distinguish "present" from "missing" even when the logical value is `NULL`.
3. Re-implement the init/add/contains/remove/destroy helpers and validate them with the driver (or bespoke unit tests) before wiring the code into the library.
4. Document any ownership conventions you chose so later lessons know how to push heap-allocated keys through the set safely.
