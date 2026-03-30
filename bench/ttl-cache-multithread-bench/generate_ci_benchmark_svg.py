#!/usr/bin/env python3
"""Generate CI benchmark SVG with roomy 3-panel compiler comparison line charts."""

from __future__ import annotations

import re
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
OUT_FILE = BENCH_DIR / "copilot_ci_benchmark.svg"

RAW_FILES = {
    "gcc": [BENCH_DIR / "ci_benchmark_raw_gcc.txt", BENCH_DIR / "ci_benchmark_raw.txt"],
    "clang": [BENCH_DIR / "ci_benchmark_raw_clang.txt"],
    "tcc": [BENCH_DIR / "ci_benchmark_raw_tcc.txt"],
}

COLORS = {"gcc": "#58a6ff", "clang": "#2ea043", "tcc": "#f2cc60"}

LINE_RE = re.compile(
    r"^\s*(\d+)s\s*\|\s*(\d+)\s*\|\s*([\d.]+)\s*\|\s*([\d.]+)\s*\|\s*([\d.]+)"
    r"\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*(\d+)\s*$"
)


def parse_samples(text: str) -> list[dict[str, float]]:
    rows: list[dict[str, float]] = []
    for line in text.splitlines():
        m = LINE_RE.match(line)
        if not m:
            continue
        sec, ops, _hit, _miss, _exp, _writes, _lat, _epoch, rss_kb, _evict, clean, retire = m.groups()
        clean_i = int(clean)
        retire_i = int(retire)
        rows.append(
            {
                "sec": int(sec),
                "ops_m": int(ops) / 1_000_000.0,
                "rss_mb": int(rss_kb) / 1024.0,
                "reclaim_pct": (clean_i / retire_i * 100.0) if retire_i > 0 else 0.0,
            }
        )
    return rows


def load_compiler_series() -> dict[str, list[dict[str, float]]]:
    data: dict[str, list[dict[str, float]]] = {}
    for compiler, paths in RAW_FILES.items():
        chosen = None
        for p in paths:
            if p.exists():
                chosen = p
                break
        if not chosen:
            continue
        parsed = parse_samples(chosen.read_text(encoding="utf-8"))
        if parsed:
            data[compiler] = parsed
    return data


def scale_points(xs, ys, x0, y0, w, h, x_min, x_max, y_min, y_max):
    if x_max == x_min:
        x_max += 1
    if y_max == y_min:
        y_max += 1
    out = []
    for x, y in zip(xs, ys):
        px = x0 + ((x - x_min) / (x_max - x_min)) * w
        py = y0 + h - ((y - y_min) / (y_max - y_min)) * h
        out.append((px, py))
    return out


def draw_legend(x, y, compilers_present):
    lines = []
    order = ["gcc", "clang", "tcc"]
    cursor = x
    for c in order:
        color = COLORS[c] if c in compilers_present else "#6a737d"
        status = "" if c in compilers_present else " (N/A)"
        lines.append(f"<line x1='{cursor}' y1='{y}' x2='{cursor+34}' y2='{y}' stroke='{color}' stroke-width='4'/>\n")
        lines.append(f"<text x='{cursor+44}' y='{y+5}' fill='#c9d6e2' font-size='16' font-family='Arial'>{c.upper()}{status}</text>\n")
        cursor += 220
    return "".join(lines)


def draw_panel(title, y_label, metric_key, x, y, w, h, data):
    lines = []
    lines.append(f"<rect x='{x}' y='{y}' width='{w}' height='{h}' rx='14' fill='#11182e' stroke='#4b5872' stroke-width='1.2'/>")
    lines.append(f"<text x='{x+30}' y='{y+36}' fill='#e6edf3' font-size='22' font-family='Arial' font-weight='700'>{title}</text>")

    gx, gy, gw, gh = x + 120, y + 54, w - 190, h - 120

    all_secs, all_vals = [], []
    for rows in data.values():
        all_secs.extend(r["sec"] for r in rows)
        all_vals.extend(r[metric_key] for r in rows)

    x_min, x_max = min(all_secs), max(all_secs)
    y_min, y_max = min(all_vals), max(all_vals)
    y_pad = (y_max - y_min) * 0.15 if y_max > y_min else 1.0
    y_min -= y_pad
    y_max += y_pad

    for i in range(7):
        yy = gy + i * gh / 6
        lines.append(f"<line x1='{gx}' y1='{yy:.1f}' x2='{gx+gw}' y2='{yy:.1f}' stroke='#3b465c' stroke-width='1' stroke-dasharray='6 6' opacity='0.55'/>")

    lines.append(f"<line x1='{gx}' y1='{gy+gh}' x2='{gx+gw}' y2='{gy+gh}' stroke='#95a2bb' stroke-width='1.4'/>")
    lines.append(f"<line x1='{gx}' y1='{gy}' x2='{gx}' y2='{gy+gh}' stroke='#95a2bb' stroke-width='1.4'/>")

    for sec in range(int(x_min), int(x_max) + 1):
        if sec % 2 != 0 and sec != x_max:
            continue
        px = gx + ((sec - x_min) / (x_max - x_min if x_max != x_min else 1)) * gw
        lines.append(f"<text x='{px:.1f}' y='{gy+gh+30}' text-anchor='middle' fill='#c9d6e2' font-size='12' font-family='Arial'>{sec}s</text>")

    lines.append(f"<text x='{x+35}' y='{y+h/2:.1f}' fill='#c9d6e2' font-size='14' font-family='Arial' transform='rotate(-90 {x+35},{y+h/2:.1f})'>{y_label}</text>")
    lines.append(f"<text x='{gx+gw/2:.1f}' y='{y+h-18}' text-anchor='middle' fill='#c9d6e2' font-size='14' font-family='Arial'>Elapsed Time (s)</text>")
    lines.append(f"<text x='{gx-10}' y='{gy+6}' text-anchor='end' fill='#9fb0c0' font-size='12' font-family='Arial'>{y_max:.2f}</text>")
    lines.append(f"<text x='{gx-10}' y='{gy+gh+5}' text-anchor='end' fill='#9fb0c0' font-size='12' font-family='Arial'>{y_min:.2f}</text>")

    for compiler, rows in data.items():
        xs = [r["sec"] for r in rows]
        ys = [r[metric_key] for r in rows]
        points = scale_points(xs, ys, gx, gy, gw, gh, x_min, x_max, y_min, y_max)
        poly = " ".join(f"{px:.1f},{py:.1f}" for px, py in points)
        c = COLORS[compiler]
        lines.append(f"<polyline points='{poly}' fill='none' stroke='{c}' stroke-width='3.2' stroke-linecap='round' stroke-linejoin='round'/>")
        for px, py in points:
            lines.append(f"<circle cx='{px:.1f}' cy='{py:.1f}' r='4.0' fill='{c}' stroke='#0b1020' stroke-width='1' />")

    return "\n".join(lines)


def main() -> None:
    data = load_compiler_series()
    if not data:
        raise SystemExit("No benchmark raw files found.")

    primary = data.get("gcc") or next(iter(data.values()))
    duration = int(max(r["sec"] for r in primary))

    header = "LibTTAK TTL Cache Benchmark (GitHub Copilot CI) - 3-Compiler Detailed Comparison"
    sub = "N-second Throughput / RSS Footprint / Memory Reclamation Ratio (with extra-wide margins)"

    width, height = 1800, 1600
    svg_parts = [
        f"<svg xmlns='http://www.w3.org/2000/svg' width='{width}' height='{height}' viewBox='0 0 {width} {height}'>",
        "<rect width='100%' height='100%' fill='#0b1020'/>",
        f"<text x='50%' y='62' text-anchor='middle' fill='#e6edf3' font-size='36' font-family='Arial' font-weight='700'>{header}</text>",
        f"<text x='50%' y='100' text-anchor='middle' fill='#9fb0c0' font-size='20' font-family='Arial'>{sub}</text>",
        f"<text x='50%' y='132' text-anchor='middle' fill='#9fb0c0' font-size='18' font-family='Arial'>Duration: {duration}s</text>",
        draw_legend(120, 170, set(data.keys())),
        draw_panel("N-second Throughput Trend", "Throughput (Million Ops/s)", "ops_m", 50, 210, 1700, 420, data),
        draw_panel("N-second RSS Footprint Trend", "RSS Footprint (MB)", "rss_mb", 50, 675, 1700, 420, data),
        draw_panel("N-second Memory Reclamation Ratio", "Reclamation Ratio (%)", "reclaim_pct", 50, 1140, 1700, 420, data),
        "</svg>",
    ]

    OUT_FILE.write_text("\n".join(svg_parts), encoding="utf-8")
    print(f"Generated {OUT_FILE}")


if __name__ == "__main__":
    main()
