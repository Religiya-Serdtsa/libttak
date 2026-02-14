## Performance Comparison Report

(Linux x64, Ryzen 5600X, 64GB DDR4 3200MHz)

| Metric Category | Metric | GCC -O3 | TCC -O3 | Clang -O3 |
| --- | --- | --- | --- | --- |
| Throughput | Operations per Second (Ops/s) | 13,821,147 | 2,826,011 | 3,939,376 |
| Logic Integrity | Cache Hit Rate (%) | 77.15 | 76.15 | 76.74 |
| Resource Usage | RSS Memory Usage (KB) | 1,176,200 | 259,080 | 357,172 |
| GC Performance | CleanNsAvg (Nanoseconds) | 112,175,986 | 17,943,407 | 32,841,367 |
| Runtime Control | Total Epochs Transitioned | 38 | 7 | 39 |
| Data Retention | Items in Cache (Final) | 53,802 | 36,504 | 43,358 |
| Memory Recovery | Retired Objects Count | 1,270 | 106 | 1,024 |

---

LibTTAK and this benchmark had a same compiler (e.g. GCC LibTTAK/GCC Benchmark) in this experiment.
Benchmark program's optimization was left as `-O0`, while following the same compiler.
