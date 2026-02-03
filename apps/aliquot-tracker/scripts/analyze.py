import argparse
import math
import random
from collections import Counter


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

    # Deterministic bases for 64-bit; works well in practice for larger too.
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
    # a_{k+1} = s(a_k) = sigma(a_k) - a_k
    if n <= 1:
        return 0
    return sigma_from_factors(factorize(n)) - n


def run_aliquot(seed: int, steps: int, trace: bool, max_print: int):
    n = seed
    peak = n
    peak_step = 0

    if trace:
        print(f"step 0: {n}")

    for i in range(1, steps + 1):
        nxt = aliquot_next(n)

        # Rule check: by construction nxt is s(n). This is here to make it explicit.
        # If you later plug another engine's output, compare against nxt.
        n = nxt

        if n > peak:
            peak = n
            peak_step = i

        if trace and i <= max_print:
            print(f"step {i}: {n}")

    return {
        "seed": seed,
        "steps": steps,
        "final": n,
        "peak": peak,
        "peak_step": peak_step,
        "peak_bits": peak.bit_length(),
    }


def main():
    parser = argparse.ArgumentParser(description="Compute aliquot sequence exactly and report final/peak.")
    parser.add_argument("seed", type=int, help="start value")
    parser.add_argument("steps", type=int, help="number of transitions to apply")
    parser.add_argument("--trace", action="store_true", help="print values along the way")
    parser.add_argument("--max-print", type=int, default=50, help="limit printed steps when --trace is used")
    parser.add_argument("--expect-final", type=int, default=None, help="compare computed final with this value")
    parser.add_argument("--expect-peak", type=int, default=None, help="compare computed peak with this value")
    args = parser.parse_args()

    random.seed(0xC0FFEE)

    res = run_aliquot(args.seed, args.steps, args.trace, args.max_print)

    print("Aliquot real verification")
    print(f"  seed      : {res['seed']}")
    print(f"  steps     : {res['steps']}")
    print(f"  final     : {res['final']}")
    print(f"  peak      : {res['peak']}")
    print(f"  peak_step : {res['peak_step']}")
    print(f"  peak_bits : {res['peak_bits']}")

    print("Rule check")
    print("  transition: a_{k+1} = sigma(a_k) - a_k (computed by factorization)")

    if args.expect_final is not None:
        ok = (res["final"] == args.expect_final)
        print(f"Compare")
        print(f"  expect_final: {args.expect_final}")
        print(f"  match_final : {ok}")

    if args.expect_peak is not None:
        ok = (res["peak"] == args.expect_peak)
        print(f"Compare")
        print(f"  expect_peak: {args.expect_peak}")
        print(f"  match_peak : {ok}")


if __name__ == "__main__":
    main()

