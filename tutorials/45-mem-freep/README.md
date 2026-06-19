# Tutorial 45 – Memory Free with Pointer Zeroing

This folder demonstrates `ttak_mem_freep()` from `src/mem/mem.c`.

`ttak_mem_free(ptr)` behaves like the standard C `free()`: the memory is released, but the caller's variable still holds the old address. Dereferencing it after the call is a use-after-free bug.

`ttak_mem_freep(&ptr)` adds one extra safety step: after freeing the block, it sets `ptr` to `NULL`. This makes accidental reuse much easier to catch (a NULL dereference is usually immediate and obvious) and makes double-free safe.

## What `lesson45_mem_freep.c` shows

The program:

1. Allocates two buffers.
2. Frees the first with `ttak_mem_free()`: the variable keeps its old address.
3. Frees the second with `ttak_mem_freep()`: the variable becomes `NULL`.
4. Shows that calling `ttak_mem_freep()` again on the same variable is harmless.
5. Shows that `ttak_mem_freep(NULL)` and `ttak_mem_freep(&NULL_pointer)` are no-ops.

## Build and run

```bash
make
./lesson45_mem_freep
```

Typical output:

```text
initial values:
  buffer_a = 'hello' (0x...)
  buffer_b = 'world' (0x...)

1. ttak_mem_free(buffer_a): pointer keeps its old address
  buffer_a = 0x...  (dangling - do not dereference!)

2. ttak_mem_freep(&buffer_b): pointer is freed and zeroed
  buffer_b = (nil)  (NULL, safe to double-free)

3. double free through ttak_mem_freep is a no-op
  still safe, buffer_b remains NULL

4. ttak_mem_freep(NULL) and ttak_mem_freep(&NULL_ptr) are also safe
  no crash
```

## Key takeaway

Use `ttak_mem_free()` when you need the raw address for bookkeeping right after freeing, or when you are following the standard C convention. Use `ttak_mem_freep(&ptr)` when you want the caller's pointer zeroed automatically to reduce the risk of dangling-pointer bugs.

Because `ttak_mem_freep()` takes `void **`, calling it with a typed pointer requires a cast:

```c
char *p = ttak_mem_alloc_raw(64, ...);
ttak_mem_freep((void **)&p);  /* p is now NULL */
```
