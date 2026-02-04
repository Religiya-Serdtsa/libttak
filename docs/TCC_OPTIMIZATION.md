# TCC-Oriented Build and Math Tuning

`libttak` can now be compiled entirely with **Tiny C Compiler (TCC)** at `-O0`.
The project builds without relying on compiler-driven vectorization or loop
transformations, so correctness is identical between development and
production. High throughput is recovered through carefully scheduled C and
portable-inline-assembly idioms that map cleanly to every major 64-bit ISA.

## Build Expectations

* `make` defaults to `tcc` for every object in the library.
* Dependency files are emitted via `-MD/-MF` so incremental builds keep
  working even though TCC lacks `-MMD`.
* There is no optimization flag separation for math modules anymore; every
  translation unit is compiled with the same transparent flag set.

## Hardcore C Tricks

The helper header `ttak/types/fixed.h` packages the manual patterns that
bridge the gap between “no optimization” and “high throughput”:

1. **Portable limb arithmetic** – `ttak_u128_t`/`ttak_u256_t` expose fixed-size
   containers for 128/256-bit math using only 64-bit limbs. Helper routines
   take care of addition, subtraction, shifts, and multiplication so TinyCC can
   run Montgomery/CRT logic without native `__int128` support.
2. **Manual carry tracking** – every helper returns the carry/overflow so
   call-sites can fall back to bigint code when values exceed two limbs. This
   keeps σ(n) bookkeeping predictable and cache-friendly without compiler help.
3. **Unsigned-mask arithmetic** – bit helpers (e.g., `ttak_u128_bit`,
   `ttak_u128_and`) let us apply branchless selection and masking patterns
   directly on the portable limbs when building modulus masks or LLT residues.

The guiding rules stay intact:

* Decompose work into ALU-ready primitives on the limb helpers
  (`ttak_u128_add`, `ttak_u128_mul_u64`, etc.).
* Keep independent data streams alive so MUL latency is hidden explicitly by
  the call sequence (even though TCC preserves source order).
* Cache pointer dereferences into locals before entering tight loops; the new
  helpers follow the same pattern so the compiler has predictable access paths.

## Portable Assembly Strategy

Inline assembly is kept ISA-neutral by guarding each block with architecture
checks and providing a pure-C fallback. Most math code now relies on the limb
helpers instead, keeping the exported APIs ISA-neutral even on TinyCC builds.

## Applying the Tricks Elsewhere

* Use `ttak_branchless_select_*` instead of `if/else` whenever the predicate
  is derived from a simple comparison. This keeps pipelines warm and side-steps
  branch predictors that TCC never learns to micro-optimize for us.
* Whenever repeated exponentiation or polynomial expansions appear, prefer
  `ttak_pow_u128_latency` (or the same pattern) so independent multiply streams
  stay interleaved.
* Prefer reciprocal multiplication (magic-number division) for constant
  divisors. TCC won’t emit it automatically, so code it by hand when the math
  allows it.

Following these guardrails guarantees that the “no optimization” TCC build
still saturates the ALUs on x86_64, AArch64, and RISC-V without sacrificing
the portable, inspection-friendly nature of the library.
