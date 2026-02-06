# Lesson 36: Memory Tracing & LRU Caching

In this tutorial, we demonstrate how to trace memory allocations and ownership hierarchy using an LRU (Least Recently Used) cache implementation.

## Key Features

1.  **Allocation Timing**: Trace when memory is allocated and freed.
2.  **Access Auditing**: Record every time `ttak_mem_access` is called.
3.  **Ownership Hierarchy**: Use `ttak_owner_t` to group resources (cache entries) under a parent container.
4.  **Visual Analysis**: Generate a timeline and hierarchy graph from the trace logs.

## The Deterministic LRU & Synchronous Eviction

Lesson 36 has been upgraded to demonstrate a **Synchronous Eviction** model, ensuring absolute control over memory density:

1.  **Synchronous Management**: Instead of relying on a periodic GC loop, the `lru_put_ex` function now calls `tt_autoclean_dirty_pointers(now)` immediately during the eviction process. This ensures that the visualizer reflects the "FREED" state (gray) the exact moment capacity is exceeded.
2.  **Fixed Tick Alignment**: The demo uses a "Virtual Tick" approach (incrementing `now` by fixed intervals like 10ms) rather than real system time. This creates a perfectly deterministic trace that is easy to analyze in the visualizer.
3.  **Aggressive Memory Policy**:
    - Creates an LRU cache with a strict capacity of **5**.
    - Fills the cache with **20 items** using deterministic 10ms steps.
    - Demonstrates that the `LIVE` object count never exceeds the capacity threshold.
    - Uses `TTAK_MEM_STRICT_CHECK` to validate state transitions at every allocation.
    - Proves causality: New allocation directly triggers the eviction and immediate cleanup of the oldest node.

## How to Run

1.  **Compile and Run**:
    ```bash
    make
    ./lesson36_trace_memory 2> trace.log
    ```
    (We redirect `stderr` to `trace.log` to capture the JSON logs).

2.  **Visualize**:
    ```bash
    python3 ../../scripts/visualize_memory_calls.py trace.log
    ```

## Expected Outputs

-   `memory_analysis.png`: Contains two subplots:
    1.  **Ownership Hierarchy**: A graph showing how the `ttak_owner_t` (the cache) owns its data resources.
    2.  **Lifetime Timeline**: A Gantt chart showing allocation/free timings. Blue bars are global, orange bars are owned resources. Red dots indicate `ttak_mem_access` events.

## Why this matters?

Memory tracing is crucial for:
-   **Leak Detection**: Visualizing blocks that are never freed.
-   **Fragmentation Analysis**: Seeing the pattern of allocations and frees.
-   **Security Auditing**: Tracking which owners have access to which resources and when.