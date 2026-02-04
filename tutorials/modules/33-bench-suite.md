# 33 – Bench Suite

**Focus:** Optionally rebuild the benchmarking harness to profile your clones.

**Source material:**
- `apps/bench`
- `bench`

## Steps
1. Clone the harness entry point and job registration macros.
1. Add benchmarks for the modules you've rebuilt (HT, math, async).
1. Record results to compare against upstream performance goals.
1. Decide which modules need another tuning pass.

## Checks
- Bench harness compiles and runs at least one benchmark per subsystem.
- Results stored so you can compare after optimizations.
