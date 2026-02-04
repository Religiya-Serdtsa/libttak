# Tutorial 01 – Getting Started

This folder hosts the hands-on assets that accompany [`modules/01-getting-started`](../modules/01-getting-started/README.md). Use it after you have already built and **installed** `libttak` (e.g., `make && sudo make install`). The intent is to prove that your toolchain is wired correctly and to watch the lifetime APIs work in a tiny, self-contained program.

## What you will do

- Compile `getting_started.c`, which allocates a short-lived message with `ttak_mem_alloc`, inspects it midway through its lifetime, and observes expiration.
- Run the program and compare the log output to the lesson checklist.
- Jot down your "What I learned" entry before moving on to Lesson 02.

## Building the sample

The provided `Makefile` assumes that `libttak` lives under `/usr/local`. Adjust `PREFIX`, `CFLAGS`, or `LDFLAGS` when your installation lives elsewhere.

```bash
cd tutorials/01-getting-started
make            # builds ./getting_started
make run        # runs it with the default timestamps
```

To see how the app behaves when you simulate later timestamps, override the `NOW` and `LIFETIME` environment variables when running:

```bash
NOW=2000 LIFETIME=600 make run
```

`make clean` removes the binary so you can start fresh.

## Next steps

After you confirm that the sample mirrors the expectations in Lesson 01, move on to `tutorials/02-helper-workflow` to wire up the helper program before you start cloning modules in depth.
