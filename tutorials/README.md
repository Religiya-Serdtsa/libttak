# LibTTAK Tutorials

These tutorials are designed for clone-coding: every step rebuilds an existing subsystem from scratch so that, after completing the list, you will have covered **every module and public facility in `libttak`, unsafe contexts included**. The guide now ships as **dozens of bite-sized lessons** (`modules/01-*/README.md`, `modules/02-*/README.md`, …) so that you can focus on one commitment-sized topic at a time instead of scrolling a giant README. Start with Lesson 01 and walk forward; the ordering is intentional so that you literally re-lay the foundation of the library, stack the intermediate subsystems, and finish with the unsafe bridges—by the end you should be able to think and ship like the library’s original designer.

Key ideas:

1. **One lesson = one win.** Every lesson fits in a short session (setup, tiny scope, checklist). Stop after each one to record “What I learned.”
2. **Stage map via `CLONE_PATH.md`.** The clone path still groups lessons into stages; use it as the macro-level syllabus while the lessons provide the micro steps. The combination guarantees every subsystem—including the unsafe context bridge—gets attention exactly once.
3. **Use the helper.** Run `tutorials/helper/helper libttak.hlp` to read the manual while you work. If you need the unsafe addendum, run the helper with `tutorials/DANGEROUS/libttak_unsafe.hlp`.
4. **Design-mindset prompts.** Several lessons remind you to write down intent and invariants so an amateur can gradually reason like the architect who authored `libttak`.

## Lesson Groups

- **Orientation (Lessons 01–02).** Prep the repo and learn the helper workflow before touching code.
- **Core Data + Logging (Lessons 03–08).** Hash tables, containers, and the logger.
- **Concurrency Primitives (Lessons 09–11).** Mutexes, spinlocks, portable atomics.
- **Async + Scheduling (Lessons 12–16).** Thread pool, workers, heaps, promises/futures, scheduler.
- **Timing + Limits + Stats (Lessons 17–19).** Timers, deadlines, token buckets, histogram resets.
- **Memory Systems (Lessons 20–22).** Owners, epoch GC, memory tree integration.
- **Math + Security (Lessons 23–28).** BigInt stack all the way through SHA-256.
- **Trees + AST (Lessons 29–31).** AST walker and both tree variants.
- **Application Layer (Lessons 32–33).** `apps/aliquot-tracker` and the optional bench suite.
- **Dangerous (Lessons 34–35).** Unsafe reading plus the context bridge once you opt in.
- **Detachable Memory (Lesson 39).** Optional final pass that layers detachable arenas and signal guards on top of epochs.
- **Arena Memory (Lesson 40).** Demonstrates how tracked arena rows plug into the mem tree + epoch GC combo that powers the lock-free cache bench.
- **IO & Network Guards (Lesson 41).** Builds the new `ttak_io_guard_t` + zero-copy layer, shared net endpoints, and the session manager so you can reason about descriptor lifetimes that ride on owners and epoch reclamation.
- **Guarded IO Streams (Lesson 42).** Separates the IO side from networking so you can practice `ttak_io_guard_t`, synchronous read/write helpers, manual TTL refresh, `ttak_io_async_read`, and direct `ttak_io_poll_wait` flows before layering net endpoints on top.
- **BigScript Language (Lesson 43).** [Introduction to BigScript](./bigscript/README.md), the custom mathematical scripting language for `libttak`. Covers syntax, built-ins, and execution.

## Coverage Guarantees

Every stage in `CLONE_PATH.md` points at one or more lessons, and each lesson references the exact source files you must clone. When you check every box you will have:

- Recreated the base memory/model scaffolding before touching higher-level modules.
- Exercised every subsystem once (hash tables, pools, async runtime, timing, math, apps, unsafe).
- Captured “What I learned” entries so you can cross-link design notes to commits.
- Mirrored visually by `blueprints/09_tutorial_curriculum.puml`, which diagrams the required flow from foundation to unsafe contexts.

## Files & Tools

- `CLONE_PATH.md` – ordered checklist that maps every tutorial stage to the lesson files.
- `modules/*/README.md` – numbered lessons (01–35) with scope, steps, and verification tasks.
- `DANGEROUS/` – isolated guidance for unsafe facilities (`ttak_context_t`, raw shared memory, etc.).
- `libttak.hlp` – concise manual fed into the helper program.
- `helper/` – source for the helper tool that lets you page through `.hlp` files and mark confusing sections for later review.

## Per-lesson folders

Each lesson now has a sibling workspace under `tutorials/NN-lesson-name/` where you can keep the code and notes that the module references. Start with:

- `tutorials/01-getting-started/` – ships a buildable sample program (`getting_started.c`) plus a `Makefile` that assumes you already ran `make && sudo make install` for `libttak`. Use it to confirm your compiler can include `<ttak/...>` headers and link against `-lttak`.
- `tutorials/02-helper-workflow/` – quick checklist for practicing the helper tool interface (build, run against `libttak.hlp`, jot down shortcuts).
- `tutorials/03-hash-table-buckets/` – scratchpad for the first data-structure clone; keep your sketches and hash-table test results here before moving on.
- `tutorials/bigscript/` – source for the BigScript language course, covering syntax, arithmetic, and mathematical built-ins.

Continue creating folders that match the remaining lesson numbers whenever you want to capture scratch code or logs separate from the main source tree.

To build the helper:

```bash
cd tutorials/helper
make
```

Usage:

```bash
./helper                             # browse tutorial table of contents + READMEs
./helper --manual ../libttak.hlp     # legacy helper manual
./helper --manual ../DANGEROUS/libttak_unsafe.hlp
```

Tutorial mode controls: `i`/`k` move, `Enter` opens the selected README in a `less` pager, `Tab` opens that lesson's sample code (press `Tab` again to jump back to the table), `Backspace` returns to the table, `Esc` exits. Manual mode keeps the legacy controls and marking workflow, including `Enter` saving the current section to `marked_explanations.txt`.

Once you finish all clone steps (and optional DANGEROUS exercises), you will have reproduced the full library—with every module touched at least once—and gained familiarity with the helper/manual workflows needed for ongoing maintenance.
