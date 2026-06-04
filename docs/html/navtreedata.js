/*
 @licstart  The following is the entire license notice for the JavaScript code in this file.

 The MIT License (MIT)

 Copyright (C) 1997-2020 by Dimitri van Heesch

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 and associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 @licend  The above is the entire license notice for the JavaScript code in this file
*/
var NAVTREE =
[
  [ "libttak", "index.html", [
    [ "LibTTAK Benchmark Suite", "index.html", "index" ],
    [ "멀티스레드 TTL 캐시 벤치마크 (한국어)", "md_bench_2ttl-cache-multithread-bench_2README_8ko.html", [
      [ "핵심 지표", "md_bench_2ttl-cache-multithread-bench_2README_8ko.html#autotoc_md7", null ],
      [ "CI 상세 이미지", "md_bench_2ttl-cache-multithread-bench_2README_8ko.html#autotoc_md8", null ],
      [ "입력 raw 파일", "md_bench_2ttl-cache-multithread-bench_2README_8ko.html#autotoc_md9", null ],
      [ "참고", "md_bench_2ttl-cache-multithread-bench_2README_8ko.html#autotoc_md10", null ]
    ] ],
    [ "Development History", "md_HISTORY.html", [
      [ "2026-01-30", "md_HISTORY.html#autotoc_md27", [
        [ "Initial Skeleton Creation & Build Stabilization", "md_HISTORY.html#autotoc_md28", null ],
        [ "Key Diffs Overview", "md_HISTORY.html#autotoc_md29", null ]
      ] ]
    ] ],
    [ "RAII in LibTTAK: How It Differs from Traditional Resource Management", "md_RAII.html", [
      [ "Overview", "md_RAII.html#autotoc_md31", null ],
      [ "1. Explicit Lifetimes Instead of Implicit Destructors", "md_RAII.html#autotoc_md33", null ],
      [ "2. Owner-Based Sandboxing (<tt>ttak_owner_t</tt>)", "md_RAII.html#autotoc_md35", null ],
      [ "3. Detachable Generational Arenas (<tt>ttak_detachable_context_t</tt>)", "md_RAII.html#autotoc_md37", null ],
      [ "4. Epoch-Based Reclamation (EBR) Instead of Immediate Free", "md_RAII.html#autotoc_md39", null ],
      [ "5. Signal-Aware Graceful Teardown", "md_RAII.html#autotoc_md41", null ],
      [ "6. No Hidden Global State or TLS Caches", "md_RAII.html#autotoc_md43", null ],
      [ "Summary", "md_RAII.html#autotoc_md45", null ],
      [ "Traditional Scope-Bound RAII (GCC/Clang)", "md_RAII.html#autotoc_md47", null ],
      [ "Why Network, Math, and More Live Inside a Single Systems Library", "md_RAII.html#autotoc_md49", [
        [ "Overview", "md_RAII.html#autotoc_md50", null ],
        [ "1. One Lifetime Model for Every Subsystem", "md_RAII.html#autotoc_md52", null ],
        [ "2. Zero-Copy and Scheduling Co-Design", "md_RAII.html#autotoc_md54", null ],
        [ "3. Deterministic Scheduling Across Domains", "md_RAII.html#autotoc_md56", null ],
        [ "4. A Single Allocator Contract", "md_RAII.html#autotoc_md58", null ],
        [ "5. Unified Observability", "md_RAII.html#autotoc_md60", null ],
        [ "6. TinyCC Parity and ABI Stability", "md_RAII.html#autotoc_md62", null ],
        [ "Summary", "md_RAII.html#autotoc_md64", null ]
      ] ]
    ] ],
    [ "LibTTAK (한국어 안내)", "md_README_8ko.html", [
      [ "벤치마크(한국어)", "md_README_8ko.html#autotoc_md66", null ]
    ] ],
    [ "Mathematical and Historical References for libttak Algorithms", "md_REFERENCES.html", [
      [ "<tt>ttak_math_lane_mul</tt> (formerly <tt>ttak_math_dawonsul_lane_mul</tt>)", "md_REFERENCES.html#autotoc_md105", null ],
      [ "<tt>ttak_matrix_set_ols_magic_square_4x4</tt> (formerly <tt>ttak_matrix_set_gusuryak_4x4</tt>)", "md_REFERENCES.html#autotoc_md107", null ],
      [ "<tt>ttak_math_approx_sin</tt> and <tt>ttak_math_approx_cos</tt>", "md_REFERENCES.html#autotoc_md109", null ],
      [ "<tt>ttak_bigreal_op_aligned_addsub</tt> (formerly <tt>ttak_bigreal_op_cheonwonsul</tt>)", "md_REFERENCES.html#autotoc_md111", null ],
      [ "Latin-Square Scatter LUT in <tt>src/mem/arena_helper.c</tt>", "md_REFERENCES.html#autotoc_md113", null ],
      [ "OLS Traversal in <tt>src/container/pool.c</tt>", "md_REFERENCES.html#autotoc_md115", null ],
      [ "Lock-Free Lattice Ingress in <tt>src/net/lattice.c</tt>", "md_REFERENCES.html#autotoc_md117", null ],
      [ "<tt>ttak_apply_mols_control</tt> in <tt>include/ttak/mols_control.h</tt>", "md_REFERENCES.html#autotoc_md119", null ],
      [ "Buddy Allocator Residue Lookup in <tt>src/phys/mem/buddy.c</tt>", "md_REFERENCES.html#autotoc_md121", null ]
    ] ],
    [ "LibTTAK Specification", "md_SPECS.html", [
      [ "1. Purpose", "md_SPECS.html#autotoc_md123", null ],
      [ "2. Design Philosophy", "md_SPECS.html#autotoc_md124", null ],
      [ "3. Architectural Overview", "md_SPECS.html#autotoc_md125", null ],
      [ "4. Memory Model", "md_SPECS.html#autotoc_md126", [
        [ "4.1 Fortress Allocator (<tt>ttak_mem_alloc_safe</tt>)", "md_SPECS.html#autotoc_md127", null ],
        [ "4.2 Epoch & Garbage Collection", "md_SPECS.html#autotoc_md128", null ],
        [ "4.3 Detachable Arenas", "md_SPECS.html#autotoc_md129", null ],
        [ "4.4 Ownership", "md_SPECS.html#autotoc_md130", null ]
      ] ],
      [ "5. Concurrency Model", "md_SPECS.html#autotoc_md131", null ],
      [ "6. Data Structures", "md_SPECS.html#autotoc_md132", null ],
      [ "7. I/O Model", "md_SPECS.html#autotoc_md133", null ],
      [ "8. Math & Scripting", "md_SPECS.html#autotoc_md134", null ],
      [ "9. Security & Integrity", "md_SPECS.html#autotoc_md135", null ],
      [ "10. Extensibility Rules", "md_SPECS.html#autotoc_md136", null ],
      [ "11. Performance Expectations", "md_SPECS.html#autotoc_md137", null ],
      [ "12. Documentation & Testing", "md_SPECS.html#autotoc_md138", null ],
      [ "13. Compliance Checklist", "md_SPECS.html#autotoc_md139", null ]
    ] ],
    [ "Clone Coding Path", "md_tutorials_2CLONE__PATH.html", null ],
    [ "Topics", "topics.html", "topics" ],
    [ "Classes", "annotated.html", [
      [ "Class List", "annotated.html", "annotated_dup" ],
      [ "Class Index", "classes.html", null ],
      [ "Class Members", "functions.html", [
        [ "All", "functions.html", "functions_dup" ],
        [ "Functions", "functions_func.html", null ],
        [ "Variables", "functions_vars.html", "functions_vars" ]
      ] ]
    ] ],
    [ "Files", "files.html", [
      [ "File List", "files.html", "files_dup" ],
      [ "File Members", "globals.html", [
        [ "All", "globals.html", "globals_dup" ],
        [ "Functions", "globals_func.html", "globals_func" ],
        [ "Variables", "globals_vars.html", null ],
        [ "Typedefs", "globals_type.html", null ],
        [ "Enumerations", "globals_enum.html", null ],
        [ "Enumerator", "globals_eval.html", null ],
        [ "Macros", "globals_defs.html", "globals_defs" ]
      ] ]
    ] ]
  ] ]
];

var NAVTREEINDEX =
[
"00__overview_8puml.html",
"calculus_8h.html#acda4cb3a1a24ae48f1e988b6b458c80f",
"factor_8h.html#ae6b18eac3f9615ae93f70bbe09218716",
"include_2stdatomic_8h.html#a753852398c578c7b0e671ebe39a5a099",
"mem_8h.html#a000835e8881250f937ecebbde55ff0dd",
"session_8h.html#a7d232d0409f2d48425cc50bdc099c9ac",
"structttak__bigscript__limits__t.html#ae1739a95e602bf880bd3e49ada3b1914",
"structttak__mem__node.html#adeba8cfc20ec3186ab1f360d7eaf098a",
"structttak__slab__t.html#a7bbf6e9431f3bf03d7d3303e955e0376",
"ttak__align_8h.html#a57fbdeca2fdfd0ec356c5d1fa8aba7e3"
];

var SYNCONMSG = 'click to disable panel synchronisation';
var SYNCOFFMSG = 'click to enable panel synchronisation';