# Tutorial 33 – Bench Suite

[`modules/33-bench-suite`](../modules/33-bench-suite/README.md) is an optional performance deep dive that mirrors the `bench/` utilities.

`lesson33_bench_suite.c` records microbench timings; build it along with any CLI harness you create under `bench/`.

## Checklist

1. Itemize which benchmarks you plan to port (allocators, schedulers, math) and write their goals at the top of this folder.
2. Clone the measurement helpers (`ttak_stats_t`, timer hooks) exactly so results stay comparable to upstream data.
3. Use `lesson33_bench_suite.c` plus the `bench/ttl-cache-multithread-bench` sources as launchpads for your own tests.
4. Archive raw benchmark output here so progress is easy to audit later.
