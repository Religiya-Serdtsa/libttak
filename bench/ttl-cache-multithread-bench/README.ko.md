# 멀티스레드 TTL 캐시 벤치마크 (한국어)

이 디렉터리는 LibTTAK lock-free shared memory 성능 벤치마크를 제공합니다.

## 핵심 지표

- N초 처리량 (Ops/s)
- N초 RSS footprint (MB)
- N초 메모리 reclamation ratio (`Clean/s ÷ Retire/s`)

## 최신 벤치마크 결과 (GitHub Copilot CI)

| 컴파일러 | Peak 처리량 | 평균 처리량 (60초) | 최종 RSS | 성능 비고 |
|----------|-----------------|----------------------|-----------|------------------|
| **GCC** | **27.13M Ops/s** | 20.00M Ops/s | 579.3 MB | 3 vCPU 가상 러너 기준 |
| **Clang** | **27.11M Ops/s** | 20.91M Ops/s | 579.4 MB | 3 vCPU 가상 러너 기준 |
| **TCC** | **14.89M Ops/s** | 11.41M Ops/s | 578.9 MB | 3 vCPU 가상 러너 기준 (TCC 조율 파라미터 적용) |

### CI 환경

- 플랫폼: GitHub Copilot CI runner (Linux, KVM)
- 커널: `Linux 6.12.47 x86_64`
- CPU: `Intel(R) Xeon(R) Platinum 8272CL CPU @ 2.60GHz` (3 vCPU)
- 메모리: 17 GiB RAM, swap 없음

## 기술적 분석: TCC 메모리 불균형 현상 해결 (v3)

이전 버전에서는 극심한 갱신 상황에서 TCC (~2.9 GB)와 GCC/Clang (< 30 MB) 간의 RSS (Resident Set Size) 차이가 심각하게 발생했습니다. 이는 LibTTAK의 아키텍처적 결함이 아니라 컴파일러별 코드 생성 특징 때문이었습니다:
1. **최적화 및 인라이닝 지연**: 에포크 기반 GC의 적극적인 인라이닝 미비로 메모리 회수가 지연되어 회수 부채가 쌓였습니다.
2. **원자적 연산 구현 차이**: TCC의 보수적인 원자적 연산 처리로 인해 에포크 상에서 해제 가능한 객체가 오래 남았습니다.
3. **처리량 대비 청소 속도 불일치**: 생성되는 슬롯 대비 클린업 루틴 속도가 느려 일시적인 적체가 발생했습니다.

### v3 개선 사항:
이를 해결하기 위해 다음 조치가 도입되었습니다:
- **아키텍처 전용 인라인 어셈블리 도입**: `include/ttak/arch/ttak_arch.h`에 TCC 전용 네이티브 `amd64`/`arm64` pause, atomic, rdtsc 어셈블리 루틴을 구현하여 범용 폴백을 우회했습니다.
- **TCC 맞춤형 파라미터**: TCC 실행 시 배치 크기 및 메인터넌스 스캔 튜닝 설정을 적용하여, 메모리 회수 속도를 최적화하고 GCC/Clang 수준의 안정적인 메모리 풋프린트(~578.9 MB)를 유지하도록 해결했습니다.

## CI 상세 이미지

<!-- AUTO-CI-BENCHMARK:START -->
`copilot_ci_benchmark.svg`, `throughput_comparison.svg`, `rss_comparison.svg`는 같은 GitHub CI raw 벤치마크 출력에서 생성됩니다.

상세 이미지 `copilot_ci_benchmark.svg`는 다음 2개 섹션으로 생성됩니다.

1. **컴파일러 3종 비교 섹션**: GCC / Clang / TCC 오버레이 라인 차트
2. **Embedded Allocator 섹션**: GCC 고정 + `EMBEDDED=0` vs `EMBEDDED=1` 비교

컴파일러 3종 비교 섹션은 각 컴파일러를 **60초**씩 측정해 생성합니다.

이미지 재생성:

```bash
python3 ./run_ci_benchmark_series.py --duration 60
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
