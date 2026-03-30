#!/usr/bin/env python3
"""Update CI benchmark sections in README.md and README.ko.md."""

from __future__ import annotations

import argparse
from pathlib import Path


START = "<!-- AUTO-CI-BENCHMARK:START -->"
END = "<!-- AUTO-CI-BENCHMARK:END -->"


def replace_block(path: Path, block: str) -> None:
    text = path.read_text(encoding="utf-8")
    if START not in text or END not in text:
        raise SystemExit(f"missing automation markers in {path}")
    before, rest = text.split(START, 1)
    _, after = rest.split(END, 1)
    updated = f"{before}{START}\n{block}\n{END}{after}"
    path.write_text(updated, encoding="utf-8")


def build_en_block(duration: int) -> str:
    return f"""The CI artifact `copilot_ci_benchmark.svg` uses a roomy 3-panel line-chart layout and keeps the 3-compiler comparison format (GCC / Clang / TCC).

For the compiler-comparison section, each compiler is measured for **{duration} seconds** to capture steady-state trends:

- N-second throughput trend (compiler overlay)
- N-second RSS footprint trend (compiler overlay)
- N-second memory reclamation ratio trend (`Clean/s ÷ Retire/s`, compiler overlay)

The layout reserves extra panel/axis/legend margins to prevent overlap or distortion in CI preview renderers.

Regenerate with:

```bash
TTAK_BENCH_DURATION_SEC={duration} TTAK_BENCH_THREADS=1 make CC=gcc ttl_cache_bench_lockfree && TTAK_BENCH_DURATION_SEC={duration} TTAK_BENCH_THREADS=1 ./ttl_cache_bench_lockfree > ci_benchmark_raw_gcc.txt
TTAK_BENCH_DURATION_SEC={duration} TTAK_BENCH_THREADS=1 make CC=clang ttl_cache_bench_lockfree && TTAK_BENCH_DURATION_SEC={duration} TTAK_BENCH_THREADS=1 ./ttl_cache_bench_lockfree > ci_benchmark_raw_clang.txt
TTAK_BENCH_DURATION_SEC={duration} TTAK_BENCH_THREADS=1 make CC=tcc ttl_cache_bench_lockfree && TTAK_BENCH_DURATION_SEC={duration} TTAK_BENCH_THREADS=1 ./ttl_cache_bench_lockfree > ci_benchmark_raw_tcc.txt
python3 ./generate_ci_benchmark_svg.py
python3 ./update_readme_ci_section.py --duration {duration}
```"""


def build_ko_block(duration: int) -> str:
    return f"""`copilot_ci_benchmark.svg`는 다음 2개 섹션으로 생성됩니다.

1. **컴파일러 3종 비교 섹션**: GCC / Clang / TCC 오버레이 라인 차트
2. **Embedded Allocator 섹션**: GCC 고정 + `EMBEDDED=0` vs `EMBEDDED=1` 비교

컴파일러 3종 비교 섹션은 각 컴파일러를 **{duration}초**씩 측정해 생성합니다.

이미지 재생성:

```bash
TTAK_BENCH_DURATION_SEC={duration} TTAK_BENCH_THREADS=1 make CC=gcc ttl_cache_bench_lockfree && TTAK_BENCH_DURATION_SEC={duration} TTAK_BENCH_THREADS=1 ./ttl_cache_bench_lockfree > ci_benchmark_raw_gcc.txt
TTAK_BENCH_DURATION_SEC={duration} TTAK_BENCH_THREADS=1 make CC=clang ttl_cache_bench_lockfree && TTAK_BENCH_DURATION_SEC={duration} TTAK_BENCH_THREADS=1 ./ttl_cache_bench_lockfree > ci_benchmark_raw_clang.txt
TTAK_BENCH_DURATION_SEC={duration} TTAK_BENCH_THREADS=1 make CC=tcc ttl_cache_bench_lockfree && TTAK_BENCH_DURATION_SEC={duration} TTAK_BENCH_THREADS=1 ./ttl_cache_bench_lockfree > ci_benchmark_raw_tcc.txt
python3 ./generate_ci_benchmark_svg.py
python3 ./update_readme_ci_section.py --duration {duration}
```"""


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--duration", type=int, default=60)
    args = parser.parse_args()
    base = Path(__file__).resolve().parent
    replace_block(base / "README.md", build_en_block(args.duration))
    replace_block(base / "README.ko.md", build_ko_block(args.duration))


if __name__ == "__main__":
    main()
