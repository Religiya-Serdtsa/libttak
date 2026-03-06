ALIQUOT-3: High-Performance Sequential Range Sweeper
Version: 1.2.0 (libttak-based)
Engine: Sociable-3 Dominator

[TECHNICAL SPECIFICATIONS]
- Architecture: Multi-threaded Asynchronous Task Pool
- Core Library: libttak (Systems Programming Framework)
- Numeric Precision: Arbitrary-precision integers via libttak BigInt
- Sieve Strategy: 3-step Aliquot sequence verification (n -> s(n) -> s(s(n)) -> s(s(s(n))) == n)
- Parallelism: POSIX Threads with atomic range-claiming (lock-free distribution)
- Integrity: SHA-256 Range Hashing (Merkle-style verification for every 10,000 seeds)
- I/O: Non-blocking JSONL logging for found candidates and range proofs

[OPERATIONAL MODES]
1. SWEEP MODE: Sequential scanning from the last saved checkpoint in /opt/aliquot-3/
2. VERIFY MODE: Deep trace of specific seeds using the --verify <seed> flag

[DEPENDENCIES]
- Linux Kernel 6.1+ (Optimized for Debian Trixie/Bookworm)
- libttak (Security, Threading, and Math modules)
- OpenSSL (SHA-256 backend)

[NOTES]
- This system generates cryptographic proof-of-work for all scanned ranges. 
- All results are timestamped and hashed to ensure indisputable priority over discovered sociable cycles.
- If you have performed extreme overclocking on your system, run this to verify your hardware stability.

[RESILIENCE]
- Set `ALIQUOT_SUMDIV_MAX_BITS` (default: 192) to cap the largest intermediate value allowed in the divisor-sum chain.
- Seeds that exceed the cap are recorded in `skipped_seeds.jsonl` so the scanner maintains a non-zero processing rate.
