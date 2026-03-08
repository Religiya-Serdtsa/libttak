#!/usr/bin/env python3
import subprocess
import re
import matplotlib.pyplot as plt
import os
import time

BENCH_DIR = "bench/ttl-cache-multithread-bench"
COMPILERS = ["gcc", "tcc", "clang"]
RUN_SECONDS = 300

def _parse_elapsed_seconds(label):
    digits = "".join(ch for ch in label if ch.isdigit())
    return int(digits) if digits else None

def run_bench(compiler):
    print(f"--- Benchmarking with {compiler} ---")
    try:
        # Build library with the same compiler first
        subprocess.run(["make", "clean"], check=True, capture_output=True)
        env = os.environ.copy()
        env["CC"] = compiler
        subprocess.run(["make"], check=True, env=env, capture_output=True)

        # Build benchmark
        subprocess.run(["make", "-C", BENCH_DIR, "clean"], check=True, capture_output=True)
        subprocess.run(["make", "-C", BENCH_DIR], check=True, env=env, capture_output=True)
        
        # Run
        run_env = os.environ.copy()
        if compiler == "tcc":
            run_env.setdefault("MALLOC_CHECK_", "2")

        series = []
        for attempt in range(3):
            proc = subprocess.Popen([f"./{BENCH_DIR}/ttl_cache_bench_lockfree"], 
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=run_env)
            series = []
            while True:
                line = proc.stdout.readline()
                if not line:
                    if proc.poll() is not None:
                        break
                    continue
                # Parse line like: " 1s | 29182046 |          90 |  212136 |  6339 | 555052"
                parts = [p.strip() for p in line.split("|")]
                if len(parts) >= 6:
                    try:
                        ops = int(parts[1])
                        rss = int(parts[-1])
                        elapsed = _parse_elapsed_seconds(parts[0])
                        if elapsed is None:
                            continue
                        series.append({"ops": ops, "rss": rss, "elapsed": elapsed})
                        elapsed_txt = f"{elapsed}s"
                        print(f"  {compiler}: t={elapsed_txt} {ops} Ops/s, {rss} KB")
                        if elapsed is not None and elapsed >= RUN_SECONDS:
                            break
                    except ValueError:
                        continue
            
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()

            if series or compiler != "tcc":
                break
            print(f"  {compiler}: no samples collected (attempt {attempt + 1}), retrying...")
            time.sleep(1)
        
        if not series:
            return None
            
        avg_ops = sum(p["ops"] for p in series) / len(series)
        peak_ops = max(p["ops"] for p in series)
        final_rss = series[-1]["rss"]
        
        return {"avg_ops": avg_ops, "peak_ops": peak_ops, "rss": final_rss, "series": series}
        
    except Exception as e:
        print(f"Failed to benchmark {compiler}: {e}")
        return None

def generate_graphs(data):
    if not data:
        return

    palette = {
        "gcc": "#4c72b0",
        "tcc": "#55a868",
        "clang": "#c44e52",
    }

    # Throughput Trend
    plt.figure(figsize=(12, 6))
    for compiler, payload in data.items():
        series = payload.get("series", [])
        if not series:
            continue
        times = [pt["elapsed"] for pt in series]
        throughput = [pt["ops"] / 1e6 for pt in series]
        color = palette.get(compiler, None)
        plt.plot(times, throughput, label=f"{compiler} (peak {payload['peak_ops']/1e6:.1f}M)", color=color, linewidth=2)
    plt.xlabel("Elapsed Time (s)")
    plt.ylabel("Throughput (Million Ops/s)")
    plt.title("Lock-Free TTL Cache Throughput Convergence")
    plt.grid(True, linestyle="--", alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(BENCH_DIR, "throughput_comparison.png"))
    print("Generated throughput_comparison.png")

    # RSS Trend
    plt.figure(figsize=(12, 6))
    for compiler, payload in data.items():
        series = payload.get("series", [])
        if not series:
            continue
        times = [pt["elapsed"] for pt in series]
        rss_values = [pt["rss"] for pt in series]
        color = palette.get(compiler, None)
        plt.plot(times, rss_values, label=f"{compiler} (final {payload['rss']:,} KB)", color=color, linewidth=2)
    plt.xlabel("Elapsed Time (s)")
    plt.ylabel("RSS (KB)")
    plt.title("Lock-Free TTL Cache RSS Trend")
    plt.grid(True, linestyle="--", alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(BENCH_DIR, "rss_comparison.png"))
    print("Generated rss_comparison.png")

if __name__ == "__main__":
    bench_data = {}
    for comp in COMPILERS:
        res = run_bench(comp)
        if res:
            bench_data[comp] = res
    
    if bench_data:
        generate_graphs(bench_data)
    else:
        print("No benchmark data collected.")
