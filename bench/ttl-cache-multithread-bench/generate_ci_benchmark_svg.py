#!/usr/bin/env python3
"""Generate a detailed CI benchmark SVG with 3 line-chart panels (no external deps)."""

from __future__ import annotations

import re
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
RAW_FILE = BENCH_DIR / "ci_benchmark_raw.txt"
OUT_FILE = BENCH_DIR / "copilot_ci_benchmark.svg"

LINE_RE = re.compile(
    r"^\s*(\d+)s\s*\|\s*(\d+)\s*\|\s*([\d.]+)\s*\|\s*([\d.]+)\s*\|\s*([\d.]+)"
    r"\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*(\d+)\s*$"
)


def parse_samples(text: str) -> list[dict[str, float]]:
    out = []
    for line in text.splitlines():
        m = LINE_RE.match(line)
        if not m:
            continue
        sec, ops, _hit, _miss, _exp, _writes, _lat, _epoch, rss_kb, _evict, clean, retire = m.groups()
        retire_i = int(retire)
        clean_i = int(clean)
        out.append(
            {
                "sec": int(sec),
                "ops": int(ops),
                "rss_kb": int(rss_kb),
                "reclaim_ratio": (clean_i / retire_i * 100.0) if retire_i > 0 else 0.0,
            }
        )
    if not out:
        raise ValueError("No benchmark samples parsed")
    return out


def polyline_points(xs, ys, x0, y0, w, h):
    x_min, x_max = min(xs), max(xs)
    y_min, y_max = min(ys), max(ys)
    if y_max == y_min:
        y_max = y_min + 1

    points = []
    for x, y in zip(xs, ys):
        px = x0 + ((x - x_min) / (x_max - x_min if x_max != x_min else 1)) * w
        py = y0 + h - ((y - y_min) / (y_max - y_min)) * h
        points.append((px, py))
    return points, y_min, y_max


def draw_panel(title, color, xs, ys, x, y, w, h, y_label):
    pts, y_min, y_max = polyline_points(xs, ys, x + 90, y + 26, w - 130, h - 70)
    content = []
    content.append(f"<rect x='{x}' y='{y}' width='{w}' height='{h}' rx='12' fill='#11182e' stroke='#4b5872' stroke-width='1'/>")
    content.append(f"<text x='{x + 24}' y='{y + 24}' fill='#e6edf3' font-size='18' font-family='Arial' font-weight='700'>{title}</text>")

    gx, gy, gw, gh = x + 90, y + 26, w - 130, h - 70
    for i in range(6):
        yy = gy + i * gh / 5
        content.append(f"<line x1='{gx}' y1='{yy:.1f}' x2='{gx + gw}' y2='{yy:.1f}' stroke='#3b465c' stroke-width='1' stroke-dasharray='4 4' opacity='0.5'/>")

    content.append(f"<line x1='{gx}' y1='{gy + gh}' x2='{gx + gw}' y2='{gy + gh}' stroke='#8995ac' stroke-width='1.3'/>")
    content.append(f"<line x1='{gx}' y1='{gy}' x2='{gx}' y2='{gy + gh}' stroke='#8995ac' stroke-width='1.3'/>")

    for i, sec in enumerate(xs):
        px = gx + (i / (len(xs) - 1 if len(xs) > 1 else 1)) * gw
        if i % 2 == 0 or i == len(xs) - 1:
            content.append(f"<text x='{px:.1f}' y='{gy + gh + 24}' text-anchor='middle' fill='#c9d6e2' font-size='11' font-family='Arial'>{sec}s</text>")

    content.append(f"<text x='{x + 22}' y='{y + h/2:.1f}' fill='#c9d6e2' font-size='12' font-family='Arial' transform='rotate(-90 {x + 22},{y + h/2:.1f})'>{y_label}</text>")
    content.append(f"<text x='{gx + gw/2:.1f}' y='{y + h - 10}' text-anchor='middle' fill='#c9d6e2' font-size='12' font-family='Arial'>Elapsed Time (s)</text>")
    content.append(f"<text x='{gx - 8}' y='{gy + 5}' text-anchor='end' fill='#9fb0c0' font-size='11' font-family='Arial'>{y_max:.2f}</text>")
    content.append(f"<text x='{gx - 8}' y='{gy + gh + 4}' text-anchor='end' fill='#9fb0c0' font-size='11' font-family='Arial'>{y_min:.2f}</text>")

    poly = " ".join(f"{px:.1f},{py:.1f}" for px, py in pts)
    content.append(f"<polyline points='{poly}' fill='none' stroke='{color}' stroke-width='3' stroke-linejoin='round' stroke-linecap='round'/>")
    for px, py in pts:
        content.append(f"<circle cx='{px:.1f}' cy='{py:.1f}' r='4.2' fill='{color}' stroke='#0b1020' stroke-width='1'/>")

    return "\n".join(content)


def main() -> None:
    s = parse_samples(RAW_FILE.read_text(encoding="utf-8"))
    secs = [p["sec"] for p in s]
    throughput_m = [p["ops"] / 1_000_000.0 for p in s]
    rss_mb = [p["rss_kb"] / 1024.0 for p in s]
    reclaim = [p["reclaim_ratio"] for p in s]

    peak_ops = max(p["ops"] for p in s)
    avg_ops = int(sum(p["ops"] for p in s) / len(s))
    final_rss_mb = s[-1]["rss_kb"] / 1024.0

    width, height = 1560, 1360
    panels = []
    panels.append(draw_panel("N-second Throughput Trend", "#58a6ff", secs, throughput_m, 48, 140, 1464, 350, "Throughput (Million Ops/s)"))
    panels.append(draw_panel("N-second RSS Footprint Trend", "#2ea043", secs, rss_mb, 48, 525, 1464, 350, "RSS Footprint (MB)"))
    panels.append(draw_panel("N-second Memory Reclamation Ratio", "#f2cc60", secs, reclaim, 48, 910, 1464, 350, "Reclamation Ratio (%)"))

    header = (
        "LibTTAK TTL Cache Benchmark (GitHub Copilot CI) - "
        "Detailed Time-Series Comparison"
    )
    sub = f"Duration: {secs[-1]}s | Peak Ops/s: {peak_ops:,} | Avg Ops/s: {avg_ops:,} | Final RSS: {final_rss_mb:.1f} MB"

    svg = f"""<svg xmlns='http://www.w3.org/2000/svg' width='{width}' height='{height}' viewBox='0 0 {width} {height}'>
<rect width='100%' height='100%' fill='#0b1020'/>
<text x='50%' y='56' text-anchor='middle' fill='#e6edf3' font-size='34' font-family='Arial' font-weight='700'>{header}</text>
<text x='50%' y='92' text-anchor='middle' fill='#9fb0c0' font-size='20' font-family='Arial'>{sub}</text>
{panels[0]}
{panels[1]}
{panels[2]}
</svg>
"""

    OUT_FILE.write_text(svg, encoding="utf-8")
    print(f"Generated {OUT_FILE}")


if __name__ == "__main__":
    main()
