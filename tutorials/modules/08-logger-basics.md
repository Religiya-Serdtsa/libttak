# 08 – Logger Basics

**Focus:** Clone the stderr logger and tiny formatting helpers.

**Source material:**
- `src/log/logger.c`
- `include/ttak/log.h`

## Steps
1. Implement `ttak_logger_log` with the same formatting + severity filters.
1. Add structured context output (thread id, subsystem tag) exactly where the original prints it.
1. Confirm the logger gracefully downgrades when stderr is unavailable.
1. Hook the logger into a sample program so you can see prefixes.

## Checks
- Logger respects log levels and avoids re-entrancy.
- Sample run prints the same prefix/order as upstream logging.
