# Tutorial 44 – Memory Unuse

This folder demonstrates `ttak_mem_unuse()` from `src/mem/mem.c`.

`ttak_mem_unuse(ptr, owner)` does **two** things:

1. **Detaches the pointer from the owner.** The resource entry is removed from the owner's resource map, so subsequent `ttak_owner_execute()` calls that look it up by name will no longer find it.
2. **Releases the GC reference.** The memory block remains valid for direct access, but the epoch GC is allowed to reclaim it once its lifetime expires and a rotation occurs.

## What `lesson44_mem_unuse.c` shows

The program:

1. Creates an owner and an epoch GC context.
2. Allocates a block with `ttak_fastalloc()` and registers it as `"name"` under the owner.
3. Executes a registered function through the owner while the resource is still tracked.
4. Calls `ttak_mem_unuse(owner_name, owner)`.
5. Shows that the owner no longer sees `"name"`, while the direct pointer is still readable.
6. Rotates the epoch GC so the released block can be collected.
7. Cleans up the owner and GC.

## Build and run

```bash
make
# then type a name when prompted
./lesson44_mem_unuse
```

Typical output (when you type `test123`):

```text
Enter a name: test123

1. BEFORE ttak_mem_unuse:
  owner sees resource: 'test123'
  greet: test123 -> sandbox hello

2. CALLING ttak_mem_unuse(owner_name, owner):

3. AFTER ttak_mem_unuse:
  owner does NOT see the resource (it was unuse'd)
  greet: owner -> still there?

4. Direct pointer is still valid (unuse does not free immediately):
  direct access: 'test123'

5. Rotating epoch GC to collect the released block...
  GC rotation done.

6. Cleanup owner + GC.
```

## Key takeaway

`ttak_mem_unuse()` is **not** `free()`. The caller can still touch the raw pointer until the GC reclaims it. It is a lifecycle hint: the owner stops managing the resource, and the GC may collect it on the next rotation.
