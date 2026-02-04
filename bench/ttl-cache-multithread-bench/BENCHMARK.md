| Metric | TCC -O3 | Clang -O3 | GCC -O3 | Unit |
| --- | --- | --- | --- | --- |
| **Avg Throughput** | 2,810,592 | 2,848,220 | **3,702,664** | Ops/s |
| **Throughput Jitter** | 22,662 | 18,340 | **14,842** | Std Dev (lower is better) |
| **CleanNsAvg** | 38,544,310 | **32,226,716** | 37,282,107 | ns (lower is better) |
| **Memory RSS** | **250,320** | 259,896 | 334,300 | KB (lower is better) |
| **Efficiency** | **11,228** | 10,959 | 11,075 | Ops/MB |
| **Hit Rate** | 76.42 | 76.47 | **76.53** | % |

---

LibTTAK and this benchmark had a same compiler(e.g. GCC LibTTAK/GCC Benchmark) in this experiment.
Benchmark program left as `-O0`.
