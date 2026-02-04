# 02 – Helper Workflow

**Focus:** Build and learn the helper program plus `.hlp` navigation controls.

**Source material:**
- `tutorials/helper/`
- `tutorials/libttak.hlp`
- `tutorials/DANGEROUS/libttak_unsafe.hlp`

## Steps
1. `cd tutorials/helper && make` to produce the helper binary (fix warnings now).
1. Run `tutorials/helper/helper tutorials/libttak.hlp` and page through each section with `i`/`k` to learn the shortcuts.
1. Test the mark feature by hitting `Enter` on a confusing paragraph and confirm `marked_explanations.txt` records it.
1. Optional: run the helper against the unsafe manual so you are familiar with swapping resources later.

## Checks
- Helper binary exists and runs without segfaults or garbled UTF-8.
- `marked_explanations.txt` captures at least one entry so you know where the file lands.

## Notes
- Keep the helper open whenever a lesson references manual paragraphs; mimic a paper textbook by bookmarking anything unclear.
