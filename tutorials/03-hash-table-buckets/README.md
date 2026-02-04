# Tutorial 03 – Hash Table Buckets

Use this space while working through [`modules/03-hash-table-buckets`](../modules/03-hash-table-buckets/README.md). Suggested flow:

1. Read the lesson once to understand which source files under `src/ht/` and headers under `include/ttak/ht/` you are rebuilding.
2. Sketch the data layout for buckets (e.g., `bucket_count`, probe strategy, tombstone handling) in a scratch file inside this folder.
3. Re-implement the bucket allocator and verification helpers described in the lesson.
4. Compile the unit tests that target `ttak_ht_*` APIs: `make test_ht` from the repo root. Keep the test logs or failures in this folder so you can compare runs.
5. Summarize what you learned plus any open questions before proceeding to the map operations lesson.

Keeping these per-lesson artifacts in `tutorials/03-hash-table-buckets` mirrors how the rest of the tutorial folders should evolve: each folder houses the code snippets, scratch notes, and verification logs you produce while clone-coding that module.
