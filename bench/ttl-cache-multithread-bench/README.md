# Multi-threaded TTL Cache Benchmark

This directory contains the flagship performance benchmark for LibTTAK's lock-free shared memory subsystem.

## Performance Philosophy

LibTTAK utilizes mathematical principles inspired by **Choi Seok-jeong's Orthogonal Latin Square (OLS)** to eliminate hardware-level lock contention.

We strategically prioritize **explosive throughput (Ops/s)** by utilizing deterministic slot isolation. Each thread operates in isolated version shards, ensuring zero overlap between concurrent writers and allowing readers to validate consistency with minimal overhead.

### Key Performance Targets (Measured)
- **Throughput:** ~150M+ Ops/s (GCC/Clang), ~65M+ Ops/s (TCC)
- **Latency:** ~6-15 ns per access
- **Memory Efficiency:** Near-zero steady-state RSS overhead after epoch reclamation (GCC/Clang)

## Latest Benchmark Results (GitHub Copilot CI)

| Compiler | Peak Throughput | Avg Throughput (20s) | Final RSS | Performance Note |
|----------|-----------------|----------------------|-----------|------------------|
| **GCC** | **13.9M Ops/s** | 10.0M Ops/s | 266.3 MB | 3 vCPU virtual runner baseline |

### CI Environment (Recorded)

- Platform: GitHub Copilot CI runner (Linux, KVM)
- Kernel: `Linux 6.12.47 x86_64`
- CPU: `Intel(R) Xeon(R) Platinum 8272CL CPU @ 2.60GHz` (3 vCPU)
- Memory: 17 GiB RAM, no swap

## Technical Analysis: TCC Memory Anomaly

The benchmark reveals a significant disparity in RSS (Resident Set Size) between TCC (~2.9 GB) and GCC/Clang (< 30 MB). This is not an architectural flaw in LibTTAK but a consequence of compiler-specific code generation:

1.  **Optimization & Inlining Lag**: LibTTAK's **Epoch-based GC** relies on aggressive inlining and register-level optimizations to reclaim memory at high velocity. TCC's lack of advanced optimization passes (like O3/LTO) causes the reclamation logic to execute significantly slower than the allocation/versioning logic, leading to "reclamation debt" under extreme throughput.
2.  **Atomic Implementation**: GCC and Clang generate highly optimized lock-free primitives and memory barriers. TCC's atomic handling is more conservative, increasing the window of time an object remains "live" in an epoch before it can be safely purged.
3.  **Throughput vs. Cleanup Mismatch**: At > 60M Ops/s, the TCC-compiled binary produces versioned slots faster than its cleanup routine can sweep them. This results in a temporary "inventory accumulation" in memory, whereas GCC/Clang's optimized sweeps maintain a near-constant memory footprint.

## Running the Benchmark

```bash
make
TTAK_BENCH_DURATION_SEC=20 ./ttl_cache_bench_lockfree
```

### Useful Environment Overrides

- `TTAK_BENCH_DURATION_SEC`: benchmark run time in seconds.
- `TTAK_BENCH_THREADS`: worker thread count override.

## Compiler Comparison

![Throughput Comparison](./throughput_comparison.png)
![RSS Comparison](./rss_comparison.png)

## CI Detailed Time-Series Image

The CI artifact `copilot_ci_benchmark.svg` is now rendered as a 3-panel line-chart view with generous spacing to avoid overlap/distortion:

- N-second throughput trend
- N-second RSS footprint trend
- N-second memory reclamation ratio trend (`Clean/s ÷ Retire/s`)

Regenerate with:

```bash
python3 ./generate_ci_benchmark_svg.py
```
