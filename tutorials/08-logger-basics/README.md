# Tutorial 08 – Logger Basics

[`modules/08-logger-basics`](../modules/08-logger-basics/README.md) covers the minimal logger found in `src/log/logger.c`—severity filtering plus custom sinks.

`lesson08_logger_basics.c` routes logs to stdout; rebuild it with `make` whenever you tweak formatting or buffering rules.

## Checklist

1. Read through the lesson with `include/ttak/log/logger.h` open so you know how callbacks and log levels are represented.
2. Experiment with alternate sinks (files, ring buffers, stderr) by editing the driver and capturing the behavior in this folder.
3. After cloning the implementation, run `make tests/test_new_features` to exercise the logger assertions baked into that suite.
4. Write down the edge cases you found (e.g., NULL sink, format truncation) so you can reference them when these APIs are used in later lessons.
