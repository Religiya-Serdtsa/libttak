# Tutorial 18 – Rate Limiters

The Lesson 18 walkthrough covers the token-bucket logic implemented in `src/limit/limit.c`.

`lesson18_rate_limiters.c` is a quick harness for `ttak_ratelimit_t`; rebuild it as you tweak refill math or burst caps.

## Checklist

1. Write down the formulas for token refill, burst limits, and fractional buckets so you have an oracle when cloning the code.
2. Implement `ttak_ratelimit_init`, `ttak_ratelimit_allow`, and any helper you rely on, matching the floating-point precision expectations documented in the lesson.
3. Use `lesson18_rate_limiters.c` plus custom scripts to simulate steady-state and bursty traffic patterns since no dedicated test binary exists.
4. Record how you validated correctness (graphs, logs, comparisons) and keep the evidence with your notes.
