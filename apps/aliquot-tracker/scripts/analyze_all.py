import argparse
import csv
import json
import math
import os
import random
from collections import Counter

STATE_DIR = "/opt/aliquot-tracker"
FOUND_LOG = os.path.join(STATE_DIR, "aliquot_found.jsonl")


def is_probable_prime(n: int) -> bool:
    if n < 2:
        return False

    small = [2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37]
    for p in small:
        if n == p:
            return True
        if n % p == 0:
            return False

    d = n - 1
    s = 0
    while (d & 1) == 0:
        d >>= 1
        s += 1

    bases = [2, 325, 9375, 28178, 450775, 9780504, 1795265022]
    for a in bases:
        a %= n
        if a == 0:
            continue
        x = pow(a, d, n)
        if x == 1 or x == n - 1:
            continue
        for _ in range(s - 1):
            x = (x * x) % n
            if x == n - 1:
                break
        else:
            return False
    return True


def pollard_rho(n: int) -> int:
    if n % 2 == 0:
        return 2
    if n % 3 == 0:
        return 3

    while True:
        c = random.randrange(1, n)
        x = random.randrange(0, n)
        y = x
        d = 1

        def f(v: int) -> int:
            return (v * v + c) % n

        while d == 1:
            x = f(x)
            y = f(f(y))
            d = math.gcd(abs(x - y), n)

        if d != n:
            return d


def factorize(n: int) -> Counter:
    res = Counter()
    if n < 2:
        return res

    for p in [2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37]:
        while n % p == 0:
            res[p] += 1
            n //= p

    def rec(m: int) -> None:
        if m == 1:
            return
        if is_probable_prime(m):
            res[m] += 1
            return
        d = pollard_rho(m)
        rec(d)
        rec(m // d)

    if n > 1:
        rec(n)

    return res


def sigma_from_factors(f: Counter) -> int:
    s = 1
    for p, e in f.items():
        s *= (pow(p, e + 1) - 1) // (p - 1)
    return s


def aliquot_next(n: int) -> int:
    if n <= 1:
        return 0
    return sigma_from_factors(factorize(n)) - n


def classify_terminal(n: int) -> str:
    if n == 1:
        return "terminated"
    if n == 0:
        return "terminated"
    if is_probable_prime(n):
        return "terminated"
    return "open"


def run_seed(seed: int, steps: int, step_limit: int | None):
    n = seed
    peak = n
    peak_step = 0

    lim = steps if step_limit is None else min(steps, step_limit)

    for i in range(1, lim + 1):
        n = aliquot_next(n)
        if n > peak:
            peak = n
            peak_step = i

    return {
        "seed": seed,
        "steps_req": steps,
        "steps_done": lim,
        "final": n,
        "peak": peak,
        "peak_step": peak_step,
        "peak_bits": peak.bit_length(),
        "computed_status": classify_terminal(n),
    }


def format_table(rows, columns):
    widths = []
    for col in columns:
        widths.append(max(len(col), max((len(str(r.get(col, ""))) for r in rows), default=0)))

    def fmt_row(obj):
        parts = []
        for col, w in zip(columns, widths):
            parts.append(str(obj.get(col, "")).rjust(w))
        return " | ".join(parts)

    header = fmt_row({c: c for c in columns})
    sep = "-+-".join("-" * w for w in widths)
    body = "\n".join(fmt_row(r) for r in rows)
    return header + "\n" + sep + "\n" + body


def main():
    parser = argparse.ArgumentParser(description="Batch aliquot verification with table report.")
    parser.add_argument("--seed", type=int, default=None, help="only verify one seed")
    parser.add_argument("--max-seeds", type=int, default=None, help="stop after N seeds")
    parser.add_argument("--step-limit", type=int, default=None, help="cap steps per seed")
    parser.add_argument("--csv", type=str, default=None, help="write CSV to this path")
    parser.add_argument("--sort", choices=["seed", "peak_bits", "peak", "steps_req"], default="peak_bits",
                        help="sort key for table output")
    parser.add_argument("--desc", action="store_true", help="sort descending")
    args = parser.parse_args()

    random.seed(0xC0FFEE)

    if not os.path.exists(FOUND_LOG):
        print(f"Error: {FOUND_LOG} not found.")
        return

    results = []
    fail_count = 0
    count = 0

    with open(FOUND_LOG, "r") as f_in:
        for line in f_in:
            line = line.strip()
            if not line:
                continue

            try:
                entry = json.loads(line)
                seed = int(entry["seed"])
                steps = int(entry["steps"])
                reported_status = str(entry.get("status", "")).lower()
            except Exception:
                continue

            if args.seed is not None and seed != args.seed:
                continue

            try:
                res = run_seed(seed, steps, args.step_limit)
                results.append({
                    "seed": res["seed"],
                    "steps_req": res["steps_req"],
                    "steps_done": res["steps_done"],
                    "reported": reported_status,
                    "computed": res["computed_status"],
                    "peak_bits": res["peak_bits"],
                    "peak_step": res["peak_step"],
                    "peak": res["peak"],
                    "final": res["final"],
                })
            except Exception as e:
                fail_count += 1
                results.append({
                    "seed": seed,
                    "steps_req": steps,
                    "steps_done": 0,
                    "reported": reported_status,
                    "computed": "error",
                    "peak_bits": "",
                    "peak_step": "",
                    "peak": "",
                    "final": f"error: {e}",
                })

            count += 1
            if args.max_seeds is not None and count >= args.max_seeds:
                break

    key = args.sort
    results.sort(key=lambda r: (r.get(key) if r.get(key) != "" else -1), reverse=args.desc)

    cols = ["seed", "steps_req", "steps_done", "reported", "computed", "peak_bits", "peak_step", "peak", "final"]
    print(format_table(results, cols))
    print()
    print(f"summary: total={len(results)} failed={fail_count}")

    if args.csv:
        with open(args.csv, "w", newline="") as f_out:
            w = csv.DictWriter(f_out, fieldnames=cols)
            w.writeheader()
            for r in results:
                w.writerow(r)
        print(f"csv: {args.csv}")
    else:
        default_csv = "aliquot_verify_all.csv"
        with open(default_csv, "w", newline="") as f_out:
            w = csv.DictWriter(f_out, fieldnames=cols)
            w.writeheader()
            for r in results:
                w.writerow(r)
        print(f"csv: {default_csv}")


if __name__ == "__main__":
    main()
