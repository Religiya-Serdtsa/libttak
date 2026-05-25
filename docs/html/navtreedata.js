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
      [ "Why Network, Math, and More Live Inside a Single Systems Library", "md_RAII.html#autotoc_md47", [
        [ "Overview", "md_RAII.html#autotoc_md48", null ],
        [ "1. One Lifetime Model for Every Subsystem", "md_RAII.html#autotoc_md50", null ],
        [ "2. Zero-Copy and Scheduling Co-Design", "md_RAII.html#autotoc_md52", null ],
        [ "3. Deterministic Scheduling Across Domains", "md_RAII.html#autotoc_md54", null ],
        [ "4. A Single Allocator Contract", "md_RAII.html#autotoc_md56", null ],
        [ "5. Unified Observability", "md_RAII.html#autotoc_md58", null ],
        [ "6. TinyCC Parity and ABI Stability", "md_RAII.html#autotoc_md60", null ],
        [ "Summary", "md_RAII.html#autotoc_md62", null ]
      ] ]
    ] ],
    [ "LibTTAK (한국어 안내)", "md_README_8ko.html", [
      [ "벤치마크(한국어)", "md_README_8ko.html#autotoc_md64", null ]
    ] ],
    [ "LibTTAK Specification", "md_SPECS.html", [
      [ "1. Purpose", "md_SPECS.html#autotoc_md102", null ],
      [ "2. Design Philosophy", "md_SPECS.html#autotoc_md103", null ],
      [ "3. Architectural Overview", "md_SPECS.html#autotoc_md104", null ],
      [ "4. Memory Model", "md_SPECS.html#autotoc_md105", [
        [ "4.1 Fortress Allocator (<tt>ttak_mem_alloc_safe</tt>)", "md_SPECS.html#autotoc_md106", null ],
        [ "4.2 Epoch & Garbage Collection", "md_SPECS.html#autotoc_md107", null ],
        [ "4.3 Detachable Arenas", "md_SPECS.html#autotoc_md108", null ],
        [ "4.4 Ownership", "md_SPECS.html#autotoc_md109", null ]
      ] ],
      [ "5. Concurrency Model", "md_SPECS.html#autotoc_md110", null ],
      [ "6. Data Structures", "md_SPECS.html#autotoc_md111", null ],
      [ "7. I/O Model", "md_SPECS.html#autotoc_md112", null ],
      [ "8. Math & Scripting", "md_SPECS.html#autotoc_md113", null ],
      [ "9. Security & Integrity", "md_SPECS.html#autotoc_md114", null ],
      [ "10. Extensibility Rules", "md_SPECS.html#autotoc_md115", null ],
      [ "11. Performance Expectations", "md_SPECS.html#autotoc_md116", null ],
      [ "12. Documentation & Testing", "md_SPECS.html#autotoc_md117", null ],
      [ "13. Compliance Checklist", "md_SPECS.html#autotoc_md118", null ]
    ] ],
    [ "Clone Coding Path", "md_tutorials_2CLONE__PATH.html", null ],
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
        [ "Macros", "globals_defs.html", null ]
      ] ]
    ] ]
  ] ]
];

var NAVTREEINDEX =
[
"00__overview_8puml.html",
"calculus_8h.html#af5ab65970ac1f025b3795157144fb68f",
"factor_8h_source.html",
"include_2ttak_2math_2ntt_8h.html#a3372a84ab2ae3592b9f69d2389320880",
"mem__internal_8h.html#a2d580a873c45f7b039612c0a816a4e07",
"shared_8h.html#aa61738cdd0322bee3972eb1e4b9134b9",
"structttak__crt__term.html#aea1e8f16579f89002fb1fdb8b856bd34",
"structttak__net__endpoint.html#a96667effbe7d4467e6b783cc9525c31f",
"structttak__thread__pool.html#ab6f0dc625cc83c204a3ad9964fe955d3",
"wyhash_8h.html#a3c7655cae4aaf843a2a13ce57d76a565"
];

var SYNCONMSG = 'click to disable panel synchronisation';
var SYNCOFFMSG = 'click to enable panel synchronisation';