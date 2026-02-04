# 34 – Dangerous Primer

**Focus:** Read the unsafe docs before touching shared-memory bridges.

**Source material:**
- `tutorials/DANGEROUS/README.md`
- `tutorials/DANGEROUS/libttak_unsafe.hlp`

## Steps
1. Open the unsafe README and list every warning/requirement.
1. Use the helper to read `libttak_unsafe.hlp` entirely—mark confusing sections for later.
1. Summarize how ownership inheritance works when contexts cross process boundaries.
1. Write a checklist you must follow before running unsafe code.

## Checks
- You can explain why `ttak_context_t` is dangerous in your own words.
- Helper marks show at least one risky paragraph for future review.
