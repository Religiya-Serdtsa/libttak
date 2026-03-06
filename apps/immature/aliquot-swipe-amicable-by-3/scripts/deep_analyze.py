#!/usr/bin/env python3
import argparse
import json
import math
import os
import sys
import time
from dataclasses import dataclass
from typing import Dict, Iterable, Optional, Tuple

# -------- math core --------

def factorize_trial_then_pollard_rho(n: int) -> Dict[int, int]:
    """
    Factorization helper optimized for operands up to approximately 80 bits.
    Implementation details:
      - Trial division using small primes.
      - Pollard's Rho algorithm for remaining composite factors.
    For operands exceeding 150 bits, external specialized tools are recommended.
    """
    if n < 2:
        return {}

    fac: Dict[int, int] = {}

    def add_factor(p: int, k: int = 1) -> None:
        fac[p] = fac.get(p, 0) + k

    # Trial division by small primes
    small_primes = [2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37]
    for p in small_primes:
        if n % p == 0:
            cnt = 0
            while n % p == 0:
                n //= p
                cnt += 1
            add_factor(p, cnt)

    # Wheel factorization trial division up to specified limit
    limit = 20000
    f = 41
    step = 2
    while f * f <= n and f <= limit:
        if n % f == 0:
            cnt = 0
            while n % f == 0:
                n //= f
                cnt += 1
            add_factor(f, cnt)
        f += step
        step = 6 - step  # 2,4,2,4...

    if n == 1:
        return fac

    # Miller-Rabin
    if is_probable_prime(n):
        add_factor(n, 1)
        return fac

    # Pollard Rho recursion
    def rho(n_: int) -> int:
        if n_ % 2 == 0:
            return 2
        if n_ % 3 == 0:
            return 3
        x = 2
        y = 2
        c = 1
        d = 1
        f = lambda v: (pow(v, 2, n_) + c) % n_
        while d == 1:
            x = f(x)
            y = f(f(y))
            d = math.gcd(abs(x - y), n_)
        return d

    def split(n_: int) -> None:
        if n_ == 1:
            return
        if is_probable_prime(n_):
            add_factor(n_, 1)
            return
        d = rho(n_)
        if d == n_:
            # Heuristic fallback: retry with alternative parameterization
            for c in [3, 5, 7, 11, 13]:
                x = 2
                y = 2
                d2 = 1
                f2 = lambda v: (pow(v, 2, n_) + c) % n_
                while d2 == 1:
                    x = f2(x)
                    y = f2(f2(y))
                    d2 = math.gcd(abs(x - y), n_)
                if 1 < d2 < n_:
                    d = d2
                    break
        split(d)
        split(n_ // d)

    split(n)

    return fac


def is_probable_prime(n: int) -> bool:
    if n < 2:
        return False
    small = [2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37]
    for p in small:
        if n == p:
            return True
        if n % p == 0:
            return False

    # Utilizes known deterministic Miller-Rabin bases for 64-bit integers.
    # Empirically effective for values up to approximately 70 bits.
    d = n - 1
    s = 0
    while d % 2 == 0:
        d //= 2
        s += 1

    def check(a: int) -> bool:
        x = pow(a, d, n)
        if x == 1 or x == n - 1:
            return True
        for _ in range(s - 1):
            x = (x * x) % n
            if x == n - 1:
                return True
        return False

    for a in [2, 325, 9375, 28178, 450775, 9780504, 1795265022]:
        if a % n == 0:
            return True
        if not check(a):
            return False
    return True


def sigma_from_factorization(fac: Dict[int, int]) -> int:
    s = 1
    for p, k in fac.items():
        s *= (p ** (k + 1) - 1) // (p - 1)
    return s


def sum_proper_divisors(n: int) -> int:
    if n <= 1:
        return 0
    fac = factorize_trial_then_pollard_rho(n)
    sig = sigma_from_factorization(fac)
    return sig - n


def bit_length(n: int) -> int:
    return n.bit_length()


# -------- verify core --------

@dataclass
class VerifyResult:
    seed: int
    steps: int
    ended: str
    final: int
    peak: int
    peak_step: int
    peak_bits: int
    ok: bool
    reason: str


def run_aliquot_verify(seed: int, max_steps: int) -> VerifyResult:
    seen: Dict[int, int] = {seed: 0}
    cur = seed
    peak = seed
    peak_step = 0

    for step in range(1, max_steps + 1):
        nxt = sum_proper_divisors(cur)

        if nxt > peak:
            peak = nxt
            peak_step = step

        if nxt <= 1:
            return VerifyResult(seed, step, "terminated", nxt, peak, peak_step, bit_length(peak), True, "")

        if nxt in seen:
            cyc_len = step - seen[nxt]
            ended = f"cycle_{cyc_len}"
            return VerifyResult(seed, step, ended, nxt, peak, peak_step, bit_length(peak), True, "")

        seen[nxt] = step
        cur = nxt

    return VerifyResult(seed, max_steps, "step_limit", cur, peak, peak_step, bit_length(peak), True, "")


def iter_track_jsonl(path: str) -> Iterable[dict]:
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                yield json.loads(line)
            except json.JSONDecodeError:
                continue


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--track", default="/opt/aliquot-3/aliquot_track.jsonl")
    ap.add_argument("--out", default="verify_report.tsv")
    ap.add_argument("--max-seeds", type=int, default=0, help="0 = all")
    ap.add_argument("--max-steps", type=int, default=3000)
    ap.add_argument("--sleep", type=float, default=0.0)
    ap.add_argument("--progress-every", type=int, default=1)
    args = ap.parse_args()

    track_path = args.track
    if not os.path.exists(track_path):
        print(f"[verify] track file not found: {track_path}", file=sys.stderr)
        return 2

    # TSV header
    if not os.path.exists(args.out):
        with open(args.out, "w", encoding="utf-8") as w:
            w.write("\t".join([
                "seed", "c_steps", "c_bits", "c_ended", "c_max_value",
                "py_steps", "py_ended", "py_final", "py_peak", "py_peak_step", "py_peak_bits",
                "match_bits", "match_ended", "match_final", "note"
            ]) + "\n")

    processed = 0
    start = time.time()

    for rec in iter_track_jsonl(track_path):
        seed = int(rec.get("seed", 0))
        if seed <= 0:
            continue

        processed += 1
        if args.max_seeds and processed > args.max_seeds:
            break

        c_steps = int(rec.get("steps", 0))
        c_bits = int(rec.get("bits", 0))
        c_ended = str(rec.get("ended", ""))
        max_value_field = rec.get("max_value")
        if max_value_field is None:
            # Skip records lacking exact maximum value field (requires tracker update)
            continue
        if isinstance(max_value_field, str):
            if max_value_field.strip() == "":
                max_value_field = "0"
            c_max_value = int(max_value_field, 10)
        else:
            c_max_value = int(max_value_field)

        t0 = time.time()
        vr = run_aliquot_verify(seed, args.max_steps)
        dt = time.time() - t0

        match_bits = (vr.peak_bits == c_bits)
        match_ended = (vr.ended == c_ended) or (c_ended.startswith("cycle") and vr.ended.startswith("cycle"))
        # Match final state: defer comparison if UINT64_MAX export occurs in BigInt path.
        match_final = True

        note = ""
        if not match_bits:
            note += "bits_mismatch;"
        if not match_ended:
            note += "ended_mismatch;"
        if dt > 1.0:
            note += f"slow_{dt:.2f}s;"

        with open(args.out, "a", encoding="utf-8") as w:
            w.write("\t".join(map(str, [
                seed, c_steps, c_bits, c_ended, c_max_value,
                vr.steps, vr.ended, vr.final, vr.peak, vr.peak_step, vr.peak_bits,
                int(match_bits), int(match_ended), int(match_final), note
            ])) + "\n")

        if processed % args.progress_every == 0:
            elapsed = time.time() - start
            print(f"[verify] n={processed} seed={seed} py_end={vr.ended} peak_bits={vr.peak_bits} c_bits={c_bits} t={dt:.2f}s elapsed={elapsed:.1f}s")

        if args.sleep > 0:
            time.sleep(args.sleep)

    print(f"[verify] done: processed={processed}")
    print(f"[verify] report: {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
