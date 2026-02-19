# Blueprints

This directory houses the PlantUML (`.puml`) blueprints for `libttak`. Each file now lives in a role-oriented folder so it is easier to navigate the architecture layers.

## Directory Structure

- `application/`: Product-entry diagrams such as `00_overview.puml`.
- `logic/`: Specialized logic (math kernels, tree structures).
- `execution/`: Runtime and scheduling blueprints.
- `resource/`: Ownership, epoch GC, and detachable/arena allocators.
- `foundation/`: Threading, synchronization, and utility services.
- `io_net/`: Guarded I/O, buffers, and network topology.
- `embedded/`: Bare-metal specific allocators and drivers.
- `integration/`: Cross-cutting diagrams like `all.puml`.
- `png/`: Rendered PNG files that mirror the same folder hierarchy.
- `scripts/`: Rendering helpers (currently `render_blueprints.py`).

### Notable Blueprints

- `resource/09_epoch_reclamation.puml`: Shows how epoch registration, retirement queues, and shared-pointer integration cooperate (Scope [09]).
- `resource/10_detachable_memory.puml`: Describes the detachable memory allocator, cache, signal hooks, and graceful termination path (Scope [10]).
- `resource/11_arena_memory.puml`: Captures the cache-aligned arena row lifecycle, mem-tree bookkeeping, and epoch GC flow referenced by Lesson 40 + the TTL cache bench (Scope [11]).
- `io_net/12_io_net.puml`: Documents the guarded I/O layer (guards, zero-copy buffers, polling) and its integration with shared endpoints plus the session manager built on EBR + Epoch GC (Scope [12]).
- `embedded/13_embedded_baremetal.puml`: Shows how the buddy allocator underpins embedded builds and how the net driver abstraction bridges POSIX/Windows/Baremetal targets (Scope [13]).
- `integration/all.puml`: Integrated architecture map that stitches the role folders into one overview.

## Rendering Blueprints

To update the PNG files from the `.puml` sources, run the following command from the project root:

```bash
make blueprints
```

Or run the script directly:

```bash
python3 blueprints/scripts/render_blueprints.py
```

The script will automatically check for changes and only re-render files that have been modified. It attempts to use a local `plantuml` installation first, and falls back to the PlantUML online server if necessary.

## Adding New Blueprints

1. Create a new `.puml` file in the folder that matches the role (or create a new folder if a new role emerges).
2. Run `make blueprints`.
3. The resulting PNG will be stored in the mirrored path under `blueprints/png/`.
