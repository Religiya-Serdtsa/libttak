# Lesson 42 – Guarded IO Streams

Lesson 41 shipped the network stack on top of detachable arenas. This follow-up focuses purely on the IO half: `ttak_io_guard_t`, the synchronous read/write helpers, staged buffers, and the polling shim that lets you safely wait on descriptors while TTL + ownership rules stay enforced.

## Objectives

1. Wrap raw descriptors with `ttak_io_guard_t` and observe when guards refresh/expire.
2. Copy user buffers through `ttak_io_buffer_t` by calling `ttak_io_sync_write` / `ttak_io_sync_read`.
3. Wait for readiness with `ttak_io_poll_wait`, then manually extend the TTL via `ttak_io_guard_refresh` so long-lived streams stay valid.

## Repo Workspace

```
tutorials/42-io-guarded-streams/
├── Makefile
├── README.md  ← you are here
└── lesson42_io_guarded_streams.c
```

`lesson42_io_guarded_streams.c` creates a POSIX pipe, duplicates each end, and uses the duplicates as guarded descriptors. The sample:

1. Writes a payload with `ttak_io_sync_write`, waits for `POLLIN`, and reads it via `ttak_io_sync_read`.
2. Sleeps past the guard TTL to demonstrate how `ttak_io_sync_read` returns `TTAK_IO_ERR_EXPIRED_GUARD` and automatically closes the stale descriptor.
3. Rehydrates the guard by duplicating the original pipe endpoint, refreshes TTLs, and shows how `ttak_io_guard_refresh` keeps the descriptor alive while waiting for the next chunk.
4. Initializes the async scheduler, calls `ttak_io_async_read`, and writes the final payload so you can watch the callback fire once the poll worker drains `POLLIN`.

> `ttak_get_tick_count()` returns milliseconds, so the TTL values in the sample are expressed directly in milliseconds (`300` → 300 ms, `2000` → 2 s).

## Build & Run

```bash
make
./lesson42_io_guarded_streams
```

Expected log excerpt (trimmed):

```
[lesson42] booting lesson42_io_guarded_streams
[lesson42] allocated 256-byte IO staging buffers
[lesson42] pipe + guard setup complete
[lesson42] sending first payload via ttak_io_sync_write
[lesson42] poll(chunk1) fd=5 events=0x1
[lesson42] read 19 bytes: "guarded io chunk #1"
[lesson42] sleeping 350ms to force guard expiry
[lesson42] guard expired as expected (status=2)
[lesson42] read 19 bytes after rehydrating guard: "guarded io chunk #2"
[lesson42] guard TTL manually extended
[lesson42] manual refresh kept guard alive long enough for: "chunk #3 after refresh"
[lesson42] starting async read demo via ttak_io_async_read
[lesson42] async read completed with 24 bytes: "chunk #4 via async read"
```

Exact wording may differ, but you should see at least one guard expiry, a re-initialisation using `dup(2)`, and a manual refresh prolonging the guard TTL before the third read.

## Verification Checklist

- [ ] `ttak_io_guard_init` registers the descriptor with an owner (watch the resource tag logs or breakpoints).
- [ ] `ttak_io_sync_write` and `ttak_io_sync_read` both stage memory via `ttak_io_buffer_t` (step through the code to see sync-in/out).
- [ ] Allowing the TTL to pass triggers `TTAK_IO_ERR_EXPIRED_GUARD` and closes the duplicated descriptor.
- [ ] Calling `ttak_io_guard_refresh` before the TTL elapses extends the lifetime so another poll + read succeed without re-duplication.
- [ ] `ttak_io_async_read` + `ttak_async_init` show how poll workers trigger callbacks without blocking the main thread (watch for the async completion log).
