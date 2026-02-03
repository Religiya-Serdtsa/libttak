# TTL Cache Benchmark (libttak)

A high-performance benchmark for `libttak` simulating a TTL-based cache with generational memory management.

## Overview

This benchmark simulates a concurrent cache workload with:
- **Generational Memory:** Values are allocated in time-based epochs.
- **Bulk Cleanup:** When an epoch expires, all its memory is freed at once using `mem_tree`.
- **Sharded Locking:** Hash map is partitioned into shards for concurrency.
- **Async Maintenance:** Background task rotates epochs and cleans up old data.

## Build

```bash
make
```

## Run

```bash
./ttl_cache_bench [options]
```

### Options

- `--threads, -t`: Number of worker threads (default: 4)
- `--duration, -d`: Benchmark duration in seconds (default: 10)
- `--value-size, -v`: Size of value blob in bytes (default: 256)
- `--keyspace, -k`: Number of unique keys (default: 100000)
- `--ttl-ms, -l`: TTL of items in milliseconds (default: 500)
- `--epoch-ms, -e`: Duration of one epoch in milliseconds (default: 250)
- `--shards, -s`: Number of map shards (default: 16)

### Example

```bash
./ttl_cache_bench --threads 8 --duration 20 --value-size 1024 --shards 32
```

## Metrics

- **Ops/s**: Total throughput (Get + Set + Del)
- **HitRate**: Percentage of GETs that found a valid item.
- **AvgClean**: Average time (microseconds) to destroy an expired epoch (bulk free).
