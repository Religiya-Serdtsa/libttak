# 멀티스레드 TTL 캐시 벤치마크 (한국어)

이 디렉터리는 LibTTAK lock-free shared memory 성능 벤치마크를 제공합니다.

## 핵심 지표

- N초 처리량 (Ops/s)
- N초 RSS footprint (MB)
- N초 메모리 reclamation ratio (`Clean/s ÷ Retire/s`)

## CI 상세 이미지

<!-- AUTO-CI-BENCHMARK:START -->
`copilot_ci_benchmark.svg`는 다음 2개 섹션으로 생성됩니다.

1. **컴파일러 3종 비교 섹션**: GCC / Clang / TCC 오버레이 라인 차트
2. **Embedded Allocator 섹션**: GCC 고정 + `EMBEDDED=0` vs `EMBEDDED=1` 비교

컴파일러 3종 비교 섹션은 각 컴파일러를 **60초**씩 측정해 생성합니다.

이미지 재생성:

```bash
TTAK_BENCH_DURATION_SEC=60 TTAK_BENCH_THREADS=1 make CC=gcc ttl_cache_bench_lockfree && TTAK_BENCH_DURATION_SEC=60 TTAK_BENCH_THREADS=1 ./ttl_cache_bench_lockfree > ci_benchmark_raw_gcc.txt
TTAK_BENCH_DURATION_SEC=60 TTAK_BENCH_THREADS=1 make CC=clang ttl_cache_bench_lockfree && TTAK_BENCH_DURATION_SEC=60 TTAK_BENCH_THREADS=1 ./ttl_cache_bench_lockfree > ci_benchmark_raw_clang.txt
TTAK_BENCH_DURATION_SEC=60 TTAK_BENCH_THREADS=1 make CC=tcc ttl_cache_bench_lockfree && TTAK_BENCH_DURATION_SEC=60 TTAK_BENCH_THREADS=1 ./ttl_cache_bench_lockfree > ci_benchmark_raw_tcc.txt
python3 ./generate_ci_benchmark_svg.py
python3 ./update_readme_ci_section.py --duration 60
```
<!-- AUTO-CI-BENCHMARK:END -->

## 입력 raw 파일

컴파일러 비교:

- `ci_benchmark_raw_gcc.txt` (없으면 `ci_benchmark_raw.txt` 사용)
- `ci_benchmark_raw_clang.txt`
- `ci_benchmark_raw_tcc.txt`

Embedded 비교:

- `ci_benchmark_raw_gcc_embedded0.txt`
- `ci_benchmark_raw_gcc_embedded1.txt`

## 참고

환경에 따라 TCC 설치가 불가능할 수 있습니다. 이 경우에도 CI/로컬 벤치 lane이 깨지지 않도록 `CC=tcc` 요청 시 `clang` 호환 빌드로 자동 폴백할 수 있습니다.
