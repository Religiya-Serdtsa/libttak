# 18 – Rate Limiters

**Focus:** Clone the token bucket limiter and understand refill math.

**Source material:**
- `src/limit/limit.c`

## Steps
1. Recreate limiter structs and initialization, documenting capacity vs burst logic.
1. Implement refill + consume so they match the timer tick math from Lesson 17.
1. Add logging hooks when rate limits trigger.
1. Test with simulated spikes to confirm tokens clamp correctly.

## Checks
- Limiter never refills beyond capacity.
- Burst simulation matches upstream reference log.
