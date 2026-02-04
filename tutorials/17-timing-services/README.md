# Tutorial 17 – Timing Services

[`modules/17-timing-services`](../modules/17-timing-services/README.md) returns to the low-level timers in `src/timing/timing.c` and `src/timing/deadline.c`.

`lesson17_timing_services.c` reads ticks and deadlines; use `make run` to compare your results to the expectations in the lesson.

## Checklist

1. Outline how the library obtains millisecond vs. nanosecond precision on your platform and note any fallbacks.
2. Rebuild `ttak_get_tick_count`, deadline helpers, and time arithmetic so higher modules can rely on them.
3. Execute `make tests/test_timing` plus any additional timing experiments to ensure the APIs behave monotonically.
4. Store benchmark results or oscilloscope captures (if you take them) here for future tuning.
