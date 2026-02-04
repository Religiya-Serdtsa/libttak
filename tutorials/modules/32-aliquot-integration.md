# 32 – Aliquot Tracker Integration

**Focus:** Wire rebuilt modules into the main app.

**Source material:**
- `apps/aliquot-tracker/src/main.c`
- `apps/aliquot-tracker/src`

## Steps
1. Clone the CLI argument parsing and configuration setup first.
1. Integrate thread pool, async scheduler, and math modules you rebuilt earlier.
1. Ensure logging + timing contexts are wired exactly like the reference.
1. Run the tracker against a short aliquot sequence and compare output.

## Checks
- CLI options behave like upstream app.
- Sequence output matches the original for sample inputs.
