# 멀티스레드 TTL 캐시 벤치마크 (한국어)

이 디렉터리는 LibTTAK lock-free shared memory 성능 벤치마크를 제공합니다.

## 핵심 지표

- N초 처리량 (Ops/s)
- N초 RSS footprint (MB)
- N초 메모리 reclamation ratio (`Clean/s ÷ Retire/s`)

## CI 상세 이미지

`copilot_ci_benchmark.svg`는 다음 2개 섹션으로 생성됩니다.

1. **컴파일러 3종 비교 섹션**: GCC / Clang / TCC 오버레이 라인 차트
2. **Embedded Allocator 섹션**: GCC 고정 + `EMBEDDED=0` vs `EMBEDDED=1` 비교

이미지 재생성:

```bash
python3 ./generate_ci_benchmark_svg.py
```

## 입력 raw 파일

컴파일러 비교:

- `ci_benchmark_raw_gcc.txt` (없으면 `ci_benchmark_raw.txt` 사용)
- `ci_benchmark_raw_clang.txt`
- `ci_benchmark_raw_tcc.txt`

Embedded 비교:

- `ci_benchmark_raw_gcc_embedded0.txt`
- `ci_benchmark_raw_gcc_embedded1.txt`

## 참고

환경에 따라 TCC 설치가 불가능할 수 있습니다. 이 경우 TCC raw 파일이 없으면 그래프 범례에 N/A로 표시됩니다.
