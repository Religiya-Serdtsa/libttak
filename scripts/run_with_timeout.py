#!/usr/bin/env python3
"""Small helper to run a command with a wall-clock timeout."""

from __future__ import annotations

import argparse
import subprocess
import sys


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run a command with the provided timeout (in seconds).",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--timeout",
        type=float,
        required=True,
        help="Maximum allowed runtime for the command (seconds).",
    )
    parser.add_argument(
        "cmd",
        nargs=argparse.REMAINDER,
        help="Command to execute; prefix with '--' before the actual command.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    command = list(args.cmd)
    if command and command[0] == "--":
        command = command[1:]

    if not command:
        print("run_with_timeout.py: missing command after '--'", file=sys.stderr)
        return 2

    try:
        completed = subprocess.run(command, check=False, timeout=args.timeout)
    except subprocess.TimeoutExpired:
        print(
            f"[ERROR] Command exceeded timeout ({args.timeout:.3f}s)",
            file=sys.stderr,
        )
        return 124

    return completed.returncode


if __name__ == "__main__":
    raise SystemExit(main())
