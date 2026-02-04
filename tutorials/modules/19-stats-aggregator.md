# 19 – Stats Aggregator

**Focus:** Rebuild the stats module that keeps running means and histograms.

**Source material:**
- `src/stats/stats.c`

## Steps
1. Clone the aggregator struct plus zeroing/rolling reset logic.
1. Reimplement update functions paying attention to overflow-safe arithmetic.
1. Generate sample histograms to ensure bucket math lines up.
1. Document when resets should be triggered.

## Checks
- Stats reset clears accumulators but keeps configuration intact.
- Histogram output matches the upstream shape for known datasets.
