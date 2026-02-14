## Performance Comparison Report
(Linux x64, Ryzen 5600X, 64GB DDR4 3200MHz)

| Metric Category | Metric | GCC -O3 | TCC -O3 | Clang -O3 |
| --- | --- | --- | --- | --- |
| Throughput | Operations per Second (Ops/s) |  |  |  |
| Logic Integrity | Cache Hit Rate (%) |  |  |  |
| Resource Usage | RSS Memory Usage (KB) |  |  |  |
| GC Performance | CleanNsAvg (Nanoseconds) |  |  |  |
| Runtime Control | Total Epochs Transitioned |  |  |  |
| Data Retention | Items in Cache (Final) |  |  |  |
| Memory Recovery | Retired Objects Count |  |  |  |

---

LibTTAK and this benchmark had a same compiler (e.g. GCC LibTTAK/GCC Benchmark) in this experiment.
Benchmark program's optimization was left as `-O0`, while following the same compiler.

## Detailed Performance Analysis

### Time-series Raw Data (Sampled)

| Time (s) | GCC Ops/s | TCC Ops/s | Clang Ops/s | GCC RSS (KB) | TCC RSS (KB) | Clang RSS (KB) |
| --- | --- | --- | --- | --- | --- | --- |
| 2 | 14,341,859 | 2,826,011 | 4,011,358 | 1,271,236 | 259,080 | 359,552 |
| 4 | 14,070,020 | 2,800,807 | 3,977,778 | 1,204,668 | 254,688 | 358,296 |
| 6 | 14,046,444 | 2,801,910 | 3,994,964 | 1,204,244 | 253,364 | 355,348 |
| 8 | 13,920,250 | 2,796,934 | 3,971,204 | 1,185,284 | 254,252 | 357,236 |
| 10 | 13,821,147 | 2,811,931 | 3,939,376 | 1,176,200 | 258,268 | 357,172 |

---

### Throughput & Bottleneck Trends

<div id="throughput_chart" style="width:100%; height:400px;"></div>
<script src="https://cdn.plot.ly/plotly-latest.min.js"></script>
<script>
    var time = [2, 3, 4, 5, 6, 7, 8, 9, 10];
    var gcc_ops = [14.34, 14.15, 14.07, 14.01, 14.04, 13.97, 13.92, 13.94, 13.82];
    var clang_ops = [4.01, 4.03, 3.97, 3.97, 3.99, 3.94, 3.97, 3.95, 3.93];
    var tcc_ops = [2.82, 2.79, 2.80, 2.79, 2.80, 2.78, 2.79, 2.81, 2.81];

    var trace1 = { x: time, y: gcc_ops, mode: 'lines+markers', name: 'GCC (M Ops/s)', line: {color: '#1f77b4'} };
    var trace2 = { x: time, y: clang_ops, mode: 'lines+markers', name: 'Clang (M Ops/s)', line: {color: '#2ca02c'} };
    var trace3 = { x: time, y: tcc_ops, mode: 'lines+markers', name: 'TCC (M Ops/s)', line: {color: '#ff7f0e'} };

    var bottlenecks = {
        x: [10, 10, 7], y: [13.82, 3.93, 2.78], mode: 'markers', name: 'Bottleneck',
        marker: { color: 'red', size: 12, symbol: 'cross' }
    };

    var layout = { title: 'Throughput Stability & System Saturation Points', xaxis: {title: 'Time (s)'}, yaxis: {title: 'Million Ops/s'} };
    Plotly.newPlot('throughput_chart', [trace1, trace2, trace3, bottlenecks], layout);
</script>

### Memory Utilization & Peak Analysis

<div id="memory_chart" style="width:100%; height:400px;"></div>
<script>
    var gcc_rss = [1.27, 1.27, 1.20, 1.20, 1.20, 1.19, 1.18, 1.17, 1.17];
    var clang_rss = [359, 361, 358, 355, 355, 355, 357, 356, 357];
    var tcc_rss = [259, 248, 254, 251, 253, 255, 254, 257, 258];

    var trace_gcc = { x: time, y: gcc_rss, mode: 'lines+markers', name: 'GCC RSS (GB)', line: {color: '#1f77b4'} };
    var trace_clang = { x: time, y: clang_rss, mode: 'lines+markers', name: 'Clang RSS (MB)', yaxis: 'y2', line: {color: '#2ca02c'} };
    var trace_tcc = { x: time, y: tcc_rss, mode: 'lines+markers', name: 'TCC RSS (MB)', yaxis: 'y2', line: {color: '#ff7f0e'} };

    var peaks = {
        x: [2, 3, 10], y: [1.27, 361, 258], mode: 'markers', name: 'Peak RSS',
        marker: { color: 'red', size: 12, symbol: 'diamond' },
        yaxis: 'y' // Simplified for visual peak indication
    };

    var layout_mem = {
        title: 'Memory Residency (RSS) & Allocation Peaks',
        xaxis: {title: 'Time (s)'},
        yaxis: {title: 'GCC RSS (GB)'},
        yaxis2: {title: 'Clang/TCC RSS (MB)', overlaying: 'y', side: 'right'}
    };
    Plotly.newPlot('memory_chart', [trace_gcc, trace_clang, trace_tcc, peaks], layout_mem);
</script>

---

**Analysis Summary:**

* **GCC Bottleneck:** Observed at 10s where `CleanNsAvg` latency peaks, causing a minor dip in Ops/s due to heavy metadata overhead in `mem_tree` during massive epoch reclamation.
* **Memory Management:** Peak RSS for GCC occurs early at 2s, suggesting aggressive initial allocation for shard buffers, whereas TCC/Clang remain stable with lower throughput-driven pressure.
