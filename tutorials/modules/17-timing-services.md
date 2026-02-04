# 17 – Timing Services

**Focus:** Re-create timers and deadlines to measure durations accurately.

**Source material:**
- `src/timing/timing.c`
- `src/timing/deadline.c`

## Steps
1. Clone the cross-platform clock selection logic.
1. Implement `ttak_deadline_from_now` plus comparison helpers exactly.
1. Observe how timers integrate with async scheduling and replicate the callback chain.
1. Write tests that simulate slow clocks to ensure fallback paths still work.

## Checks
- Timers provide millisecond + nanosecond accuracy like the reference.
- Deadline comparisons behave consistently on all supported OSes.
