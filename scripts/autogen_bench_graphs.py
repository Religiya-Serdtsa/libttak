#!/usr/bin/env python3
import subprocess
import re
import matplotlib.pyplot as plt
import os
import time

BENCH_DIR = "bench/ttl-cache-multithread-bench"
COMPILERS = ["gcc", "tcc", "clang"]
RUN_SECONDS = 10

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
        proc = subprocess.Popen([f"./{BENCH_DIR}/ttl_cache_bench_lockfree"], 
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        
        results = []
        start_time = time.time()
        while time.time() - start_time < RUN_SECONDS:
            line = proc.stdout.readline()
            if not line:
                break
            # Parse line like: " 1s | 29182046 |          90 |  212136 |  6339 | 555052"
            parts = [p.strip() for p in line.split("|")]
            if len(parts) >= 6:
                try:
                    ops = int(parts[1])
                    rss = int(parts[-1])
                    results.append((ops, rss))
                    print(f"  {compiler}: {ops} Ops/s, {rss} KB")
                except ValueError:
                    continue
        
        proc.terminate()
        
        if not results:
            return None
            
        avg_ops = sum(r[0] for r in results) / len(results)
        peak_ops = max(r[0] for r in results)
        final_rss = results[-1][1]
        
        return {"avg_ops": avg_ops, "peak_ops": peak_ops, "rss": final_rss}
        
    except Exception as e:
        print(f"Failed to benchmark {compiler}: {e}")
        return None

def generate_graphs(data):
    names = list(data.keys())
    peaks = [d["peak_ops"] / 1e6 for d in data.values()]
    rss = [d["rss"] for d in data.values()]

    # Throughput Graph
    plt.figure(figsize=(10, 6))
    bars = plt.bar(names, peaks, color=['#4c72b0', '#55a868', '#c44e52'])
    plt.ylabel('Peak Throughput (Million Ops/s)')
    plt.title('Lock-Free TTL Cache Peak Throughput by Compiler')
    plt.ylim(0, max(peaks) * 1.2)
    for bar in bars:
        height = bar.get_height()
        plt.text(bar.get_x() + bar.get_width()/2., height + 0.5, f'{height:.1f}M', ha='center', va='bottom', fontweight='bold')
    plt.tight_layout()
    plt.savefig(os.path.join(BENCH_DIR, "throughput_comparison.png"))
    print("Generated throughput_comparison.png")

    # RSS Graph
    plt.figure(figsize=(10, 6))
    bars = plt.bar(names, rss, color=['#4c72b0', '#55a868', '#c44e52'])
    plt.ylabel('Final RSS (KB)')
    plt.title('Memory Footprint (RSS) by Compiler')
    plt.ylim(0, max(rss) * 1.2)
    for bar in bars:
        height = bar.get_height()
        plt.text(bar.get_x() + bar.get_width()/2., height + 100, f'{height:,} KB', ha='center', va='bottom', fontweight='bold')
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
