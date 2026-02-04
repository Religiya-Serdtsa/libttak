
| Metric Category   | Metric                        | GCC -O3      | TCC -O3      | Clang -O3    |
|:------------------|:------------------------------|:-------------|:-------------|:-------------|
| Throughput        | Operations per Second (Ops/s) | $5,646,363$  | $2,853,837$  | $2,879,465$  |
| Logic Integrity   | Cache Hit Rate (%)            | $76.91\%$    | $76.61\%$    | $76.58\%$    |
| Resource Usage    | RSS Memory Usage (KB)         | $493,824$    | $259,064$    | $265,944$    |
| GC Performance    | CleanNsAvg (Nanoseconds)      | $60,418,051$ | $39,024,981$ | $34,304,341$ |
| Runtime Control   | Total Epochs Transitioned     | $39$         | $39$         | $39$         |
| Data Retention    | Items in Cache (Final)        | $45,162$     | $41,580$     | $41,630$     |
| Memory Recovery   | Retired Objects Count         | $1,157$      | $1,325$      | $1,219$      |

---


LibTTAK and this benchmark had a same compiler(e.g. GCC LibTTAK/GCC Benchmark) in this experiment.
Benchmark program's optimization was left as `-O0`, while following the same compilker.

