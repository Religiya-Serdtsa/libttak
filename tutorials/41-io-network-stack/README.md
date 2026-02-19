# Lesson 41 – Guarded IO & Network Sessions

This lesson introduces the `io/` + `net/` stack that now sits on top of detachable arenas and ownership enforcement. You will learn how to:

1. Wrap a raw file descriptor with `ttak_io_guard_t` so expiry + ownership are enforced before any kernel call.
2. Bind guards into `ttak_net_endpoint_t` objects that ride inside the shared subsystem.
3. Expose readonly `ttak_net_view_t` snapshots via zero-copy receives.
4. Chain endpoints inside `ttak_net_session_mgr_t`, using Epoch GC + EBR to retire entire trees of sockets safely.

## Steps

1. Build `lesson41_io_network_stack.c` with the provided `Makefile`.
2. Run the sample; it creates a UNIX `socketpair`, registers one end as a shared endpoint, sends data through the peer, and prints the contents that arrived via zero-copy view.
3. Inspect the code paths:
   - `ttak_net_endpoint_bind_fd` wires the descriptor into a guard whose TTL is enforced before polling/recv.
   - `ttak_net_view_from_endpoint` bridges the guard into a `ttak_io_zerocopy_region_t`, so you can inspect payloads without copying back into user memory.
   - `ttak_net_session_mgr_*` calls demonstrate how the session tree protects parent/child sockets and retires them via EBR + Epoch GC.
4. Modify `MAX_TTL_NS` or the payload helpers to observe how the guard expires and how the session-manager cleanup tears down descendants.

## Build & Run

```bash
make
./lesson41_io_network_stack
```

The tutorial uses `socketpair(2)` on POSIX platforms. On Windows, use the MSYS2/WSL environment or replace the transport with a loopback TCP socket before experimenting further.
