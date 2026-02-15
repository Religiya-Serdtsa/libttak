# Tutorial 19 – Stats Aggregator

[`modules/19-stats-aggregator`](../modules/19-stats-aggregator/README.md) rebuilds the histogram + running-stats helper in `src/stats/stats.c`.

`lesson19_stats_aggregator.c` feeds a few samples into `ttak_stats_t`; run it as you confirm mean/percentile calculations.

## Checklist

1. List the metrics tracked by `ttak_stats_t` (count, min/max, buckets, ASCII output) so you can double-check them after coding.
2. Clone `ttak_stats_init/record/mean/print_ascii` plus any helper APIs that the lesson calls out.
3. Run `make tests/test_stats` to execute the stats unit plus any extra probes you add to this folder.
4. Document how you validated rolling resets and bucket boundaries (screenshots/logs welcome).
