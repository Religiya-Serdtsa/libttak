#!/usr/bin/env python3
"""Run compiler benchmark series with completeness checks.

GCC is the reference lane and must succeed.  Clang and TCC are also required
to produce their own complete runs; if either lane fails, the script exits
with an error instead of silently substituting GCC results.  This keeps the
CI trend/SVG data honest per compiler.

Compiler lanes are intentionally run one after another.  The benchmark itself
may create worker threads according to --threads, but this wrapper never runs
GCC, Clang, and TCC concurrently.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
from pathlib import Path

BENCH_ROW_RE = re.compile(r"^\s*(\d+)s\s+\|")
BENCH_DIR = Path(__file__).resolve().parent
REPO_ROOT = BENCH_DIR.parent.parent


def run(cmd: list[str], env: dict[str, str] | None = None, timeout: int | None = None, output: Path | None = None) -> int:
    print("+", " ".join(cmd))
    kwargs = {
        "cwd": BENCH_DIR,
        "env": env,
        "check": False,
        "text": True,
    }
    if timeout is not None:
        kwargs["timeout"] = timeout

    if output is None:
        completed = subprocess.run(cmd, **kwargs)
        return completed.returncode

    with output.open("w", encoding="utf-8") as handle:
        completed = subprocess.run(cmd, stdout=handle, stderr=subprocess.STDOUT, **kwargs)
        return completed.returncode


def parse_rows(path: Path) -> tuple[int, bool]:
    if not path.exists():
        return 0, False
    rows = 0
    has_final = False
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = BENCH_ROW_RE.match(line)
        if not m:
            continue
        rows += 1
        if int(m.group(1)) > 0:
            has_final = True
    return rows, has_final


def has_duration_row(path: Path, duration: int) -> bool:
    if not path.exists():
        return False
    token = f"{duration}s |"
    return any(line.lstrip().startswith(token) for line in path.read_text(encoding="utf-8", errors="replace").splitlines())


def ensure_complete(path: Path, duration: int, label: str) -> bool:
    rows, _ = parse_rows(path)
    ok = rows == duration and has_duration_row(path, duration)
    print(f"[{label}] rows={rows}, expected={duration}, final_row={has_duration_row(path, duration)}")
    return ok


def rebuild_libttak(cc: str) -> bool:
    """Rebuild libttak.a with the given compiler.

    TCC's linker cannot process R_X86_64_GOTPCRELX relocations produced by
    GCC's -fPIC.  The root Makefile already has a TCC profile that disables
    PIC and switches to TCC-safe flags; we just need to invoke it here before
    the TCC bench lane runs.
    """
    nproc = str(max(1, os.cpu_count() or 1))
    print(f"+ rebuilding libttak.a with CC={cc} (repo root)")
    try:
        subprocess.run(["make", "clean"], cwd=REPO_ROOT, check=True)
        subprocess.run(["make", f"-j{nproc}", f"CC={cc}"], cwd=REPO_ROOT, check=True)
        return True
    except subprocess.CalledProcessError as exc:
        print(f"[libttak] rebuild with CC={cc} failed: {exc}")
        return False


def build_binary(cc: str) -> bool:
    build_log = BENCH_DIR / f"build_log_{cc}.txt"
    rc = run(["make", "clean"], output=build_log)
    if rc != 0:
        print(f"make clean failed for {cc}")
        _dump_tail(build_log)
        return False
    rc = run(["make", f"CC={cc}", "ttl_cache_bench_lockfree"], output=build_log)
    if rc != 0:
        print(f"build failed for {cc}")
        _dump_tail(build_log, lines=80)
        return False
    return True


def _dump_tail(path: Path, lines: int = 40) -> None:
    if not path.exists():
        return
    text = path.read_text(encoding="utf-8", errors="replace")
    tail = text.splitlines()[-lines:]
    print("--- build log tail ---")
    for ln in tail:
        print(ln)
    print("--- end build log tail ---")


def run_compiler(duration: int, threads: int, cc: str, out_name: str, extra_env: dict[str, str] | None = None) -> bool:
    if not build_binary(cc):
        return False
    env = os.environ.copy()
    env["TTAK_BENCH_DURATION_SEC"] = str(duration)
    env["TTAK_BENCH_THREADS"] = str(threads)
    if extra_env:
        env.update(extra_env)

    out_path = BENCH_DIR / out_name
    timeout = max(duration + 120, duration * 3)
    try:
        rc = run(["./ttl_cache_bench_lockfree"], env=env, timeout=timeout, output=out_path)
    except subprocess.TimeoutExpired:
        print(f"[{cc}] timed out after {timeout}s")
        return False

    if rc != 0:
        print(f"[{cc}] process failed with rc={rc}")
        return False

    return ensure_complete(out_path, duration, cc)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--duration", type=int, default=60)
    parser.add_argument(
        "--threads",
        type=int,
        default=None,
        help="number of worker threads (default: os.cpu_count())",
    )
    args = parser.parse_args()

    if args.duration <= 0:
        raise SystemExit("duration must be > 0")

    threads = args.threads if args.threads is not None else max(1, os.cpu_count() or 1)
    if threads <= 0:
        threads = 1
    args.threads = threads

    print(f"running compiler lanes sequentially: gcc -> clang -> tcc ({args.duration}s each, {args.threads} worker thread(s))")

    # Ensure the library is built with the reference compiler before the GCC lane.
    # Otherwise a stale TCC/debug build of libttak.a can poison the GCC/Clang lanes.
    if not rebuild_libttak("gcc"):
        raise SystemExit("failed to rebuild libttak.a with gcc")

    # GCC is the reference lane and must succeed.
    if not run_compiler(args.duration, args.threads, "gcc", "ci_benchmark_raw_gcc.txt"):
        raise SystemExit("gcc benchmark did not produce full output")

    # Clang must produce its own complete run.
    clang_ok = run_compiler(args.duration, args.threads, "clang", "ci_benchmark_raw_clang.txt")
    if not clang_ok:
        print("[clang] retrying with conservative warmup settings")
        clang_compat_env = {
            "TTAK_BENCH_WARMUP": "1000",
            "TTAK_BENCH_READ_BATCH": "1",
            "TTAK_BENCH_MAINT_SCAN": "2048",
            "TTAK_BENCH_WRITE_PCT": "2",
        }
        clang_ok = run_compiler(
            args.duration,
            args.threads,
            "clang",
            "ci_benchmark_raw_clang.txt",
            extra_env=clang_compat_env,
        )
    if not clang_ok:
        raise SystemExit("[clang] benchmark lane failed")

    # TCC needs the library rebuilt with its own profile.
    rebuild_libttak("tcc")

    tcc_ok = run_compiler(args.duration, args.threads, "tcc", "ci_benchmark_raw_tcc.txt")
    if not tcc_ok:
        print("[tcc] retrying in compat mode for stable full-length output")
        compat_env = {
            "TTAK_BENCH_WARMUP": "20000",
            "TTAK_BENCH_READ_BATCH": "32",
            "TTAK_BENCH_MAINT_SCAN": "8192",
            "TTAK_BENCH_WRITE_PCT": "1",
        }
        tcc_ok = run_compiler(args.duration, args.threads, "tcc", "ci_benchmark_raw_tcc_compat.txt", extra_env=compat_env)
        if tcc_ok:
            shutil.copyfile(BENCH_DIR / "ci_benchmark_raw_tcc_compat.txt", BENCH_DIR / "ci_benchmark_raw_tcc.txt")
            print("[tcc] compat output copied to ci_benchmark_raw_tcc.txt")
        else:
            raise SystemExit("[tcc] benchmark lane failed (normal + compat)")

    # Restore the GCC-optimized library so the working tree remains fast for
    # subsequent local builds and the next CI run.
    print("[cleanup] restoring libttak.a built with gcc")
    rebuild_libttak("gcc")


if __name__ == "__main__":
    main()
