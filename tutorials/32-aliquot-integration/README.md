# Tutorial 32 – Aliquot Integration

[`modules/32-aliquot-integration`](../modules/32-aliquot-integration/README.md) walks through wiring your math + limiters into `apps/aliquot-tracker`.

`lesson32_aliquot_integration.c` demonstrates the rate-limit/divisor combo; build it as you map features from the app back to the cloned modules.

## Checklist

1. List the subsystems the app touches (sum of divisors, rate limiters, persistence) and note which ones you have already cloned.
2. Use this folder to stage integration notes, mock HTTP or CLI flows, and any helper scripts you need while replaying the app logic.
3. Rebuild and run `apps/aliquot-tracker` (or its selected components) after each milestone to confirm behavior matches the lesson.
4. Capture screenshots/logs from the sample driver or the real app and summarize what changed.
