# Tutorial 28 – SHA-256

[`modules/28-sha256`](../modules/28-sha256/README.md) covers the crypto helper implemented in `src/security/sha256.c`.

`lesson28_sha256.c` hashes "libttak"; compile it while you mirror the block schedule and padding logic.

## Checklist

1. Keep the SHA-256 constants and schedule diagram in this folder for quick reference.
2. Clone `sha256_init/update/final` and make sure they match the FIPS 180-4 flow, including length padding.
3. Use `make tests/test_security` plus external known-answer tests to validate your implementation.
4. Record any optimizations (loop unrolling, endianness tricks) you decide to port so future maintainers know what changed.
