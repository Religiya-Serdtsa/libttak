#!/usr/bin/env python3
"""Generate CI benchmark SVG with compiler comparison + embedded allocator section."""

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

EMBEDDED_FILES = {
    "EMBEDDED=0": [BENCH_DIR / "ci_benchmark_raw_gcc_embedded0.txt"],
    "EMBEDDED=1": [BENCH_DIR / "ci_benchmark_raw_gcc_embedded1.txt"],
}

COLORS = {
    "gcc": "#58a6ff",
    "clang": "#2ea043",
    "tcc": "#f2cc60",
    "EMBEDDED=0": "#58a6ff",
    "EMBEDDED=1": "#ff7b72",
}

LINE_RE = re.compile(
    r"^\s*(\d+)s\s*\|\s*(\d+)\s*\|\s*([\d.]+)\s*\|\s*([\d.]+)\s*\|\s*([\d.]+)"
    r"\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*(\d+)\s*$"
)


def parse_samples(text: str) -> list[dict[str, float]]:
    rows = []
    for line in text.splitlines():
        m = LINE_RE.match(line)
        if not m:
            continue
        sec, ops, _hit, _miss, _exp, _writes, _lat, _epoch, rss_kb, _evict, clean, retire = m.groups()
        clean_i, retire_i = int(clean), int(retire)
        rows.append(
            {
                "sec": int(sec),
                "ops_m": int(ops) / 1_000_000.0,
                "rss_mb": int(rss_kb) / 1024.0,
                "reclaim_pct": (clean_i / retire_i * 100.0) if retire_i > 0 else 0.0,
            }
        )
    return rows


def load_series(paths_map: dict[str, list[Path]]) -> dict[str, list[dict[str, float]]]:
    data = {}
    for name, paths in paths_map.items():
        chosen = next((p for p in paths if p.exists()), None)
        if not chosen:
            continue
        parsed = parse_samples(chosen.read_text(encoding="utf-8"))
        if parsed:
            data[name] = parsed
    return data


def scale_points(xs, ys, x0, y0, w, h, x_min, x_max, y_min, y_max):
    if x_max == x_min:
        x_max += 1
    if y_max == y_min:
        y_max += 1
    points = []
    for x, y in zip(xs, ys):
        px = x0 + ((x - x_min) / (x_max - x_min)) * w
        py = y0 + h - ((y - y_min) / (y_max - y_min)) * h
        points.append((px, py))
    return points


def draw_legend(x, y, order, present):
    out = []
    cursor = x
    for name in order:
        color = COLORS[name] if name in present else "#6a737d"
        label = name if name in present else f"{name} (N/A)"
        out.append(f"<line x1='{cursor}' y1='{y}' x2='{cursor+34}' y2='{y}' stroke='{color}' stroke-width='4'/>")
        out.append(f"<text x='{cursor+44}' y='{y+5}' fill='#c9d6e2' font-size='16' font-family='Arial'>{label.upper()}</text>")
        cursor += 250
    return "\n".join(out)


def draw_panel(title, y_label, metric_key, x, y, w, h, data):
    out = [
        f"<rect x='{x}' y='{y}' width='{w}' height='{h}' rx='14' fill='#11182e' stroke='#4b5872' stroke-width='1.2'/>",
        f"<text x='{x+30}' y='{y+36}' fill='#e6edf3' font-size='22' font-family='Arial' font-weight='700'>{title}</text>",
    ]

    gx, gy, gw, gh = x + 120, y + 54, w - 190, h - 120
    all_secs = [r["sec"] for rows in data.values() for r in rows]
    all_vals = [r[metric_key] for rows in data.values() for r in rows]
    x_min, x_max = min(all_secs), max(all_secs)
    y_min, y_max = min(all_vals), max(all_vals)
    pad = (y_max - y_min) * 0.15 if y_max > y_min else 1.0
    y_min -= pad
    y_max += pad

    for i in range(7):
        yy = gy + i * gh / 6
        out.append(f"<line x1='{gx}' y1='{yy:.1f}' x2='{gx+gw}' y2='{yy:.1f}' stroke='#3b465c' stroke-width='1' stroke-dasharray='6 6' opacity='0.55'/>")

    out.append(f"<line x1='{gx}' y1='{gy+gh}' x2='{gx+gw}' y2='{gy+gh}' stroke='#95a2bb' stroke-width='1.4'/>")
    out.append(f"<line x1='{gx}' y1='{gy}' x2='{gx}' y2='{gy+gh}' stroke='#95a2bb' stroke-width='1.4'/>")

    for sec in range(int(x_min), int(x_max) + 1):
        if sec % 2 != 0 and sec != x_max:
            continue
        px = gx + ((sec - x_min) / (x_max - x_min if x_max != x_min else 1)) * gw
        out.append(f"<text x='{px:.1f}' y='{gy+gh+30}' text-anchor='middle' fill='#c9d6e2' font-size='12' font-family='Arial'>{sec}s</text>")

    out.extend(
        [
            f"<text x='{x+35}' y='{y+h/2:.1f}' fill='#c9d6e2' font-size='14' font-family='Arial' transform='rotate(-90 {x+35},{y+h/2:.1f})'>{y_label}</text>",
            f"<text x='{gx+gw/2:.1f}' y='{y+h-18}' text-anchor='middle' fill='#c9d6e2' font-size='14' font-family='Arial'>Elapsed Time (s)</text>",
            f"<text x='{gx-10}' y='{gy+6}' text-anchor='end' fill='#9fb0c0' font-size='12' font-family='Arial'>{y_max:.2f}</text>",
            f"<text x='{gx-10}' y='{gy+gh+5}' text-anchor='end' fill='#9fb0c0' font-size='12' font-family='Arial'>{y_min:.2f}</text>",
        ]
    )

    for name, rows in data.items():
        xs = [r["sec"] for r in rows]
        ys = [r[metric_key] for r in rows]
        points = scale_points(xs, ys, gx, gy, gw, gh, x_min, x_max, y_min, y_max)
        poly = " ".join(f"{px:.1f},{py:.1f}" for px, py in points)
        out.append(f"<polyline points='{poly}' fill='none' stroke='{COLORS[name]}' stroke-width='3.2' stroke-linecap='round' stroke-linejoin='round'/>")
        for px, py in points:
            out.append(f"<circle cx='{px:.1f}' cy='{py:.1f}' r='4.0' fill='{COLORS[name]}' stroke='#0b1020' stroke-width='1' />")

    return "\n".join(out)


def draw_embedded_section(x, y, w, h, embedded_data):
    out = [
        f"<rect x='{x}' y='{y}' width='{w}' height='{h}' rx='14' fill='#0f172a' stroke='#4b5872' stroke-width='1.2'/>",
        f"<text x='{x+24}' y='{y+34}' fill='#e6edf3' font-size='22' font-family='Arial' font-weight='700'>Embedded Allocator Section (GCC fixed, EMBEDDED flag only)</text>",
        draw_legend(x + 24, y + 62, ["EMBEDDED=0", "EMBEDDED=1"], set(embedded_data.keys())),
    ]

    panel_w = (w - 80) // 3
    panel_y = y + 80
    panel_h = h - 100
    metrics = [
        ("Throughput", "ops_m", "M Ops/s"),
        ("RSS", "rss_mb", "MB"),
        ("Reclaim", "reclaim_pct", "%"),
    ]

    for idx, (name, key, unit) in enumerate(metrics):
        px = x + 20 + idx * (panel_w + 20)
        out.append(draw_panel(f"{name} ({unit})", name, key, px, panel_y, panel_w, panel_h, embedded_data))

    return "\n".join(out)


def main() -> None:
    compiler_data = load_series(RAW_FILES)
    if not compiler_data:
        raise SystemExit("No compiler benchmark raw files found.")

    embedded_data = load_series(EMBEDDED_FILES)
    duration_source = compiler_data.get("gcc") or next(iter(compiler_data.values()))
    duration = int(max(r["sec"] for r in duration_source))

    width, height = 1800, 2350
    parts = [
        f"<svg xmlns='http://www.w3.org/2000/svg' width='{width}' height='{height}' viewBox='0 0 {width} {height}'>",
        "<rect width='100%' height='100%' fill='#0b1020'/>",
        "<text x='50%' y='62' text-anchor='middle' fill='#e6edf3' font-size='36' font-family='Arial' font-weight='700'>LibTTAK TTL Cache Benchmark (GitHub Copilot CI)</text>",
        "<text x='50%' y='100' text-anchor='middle' fill='#9fb0c0' font-size='20' font-family='Arial'>3-Compiler Comparison + Embedded Allocator Section</text>",
        f"<text x='50%' y='132' text-anchor='middle' fill='#9fb0c0' font-size='18' font-family='Arial'>Duration: {duration}s</text>",
        draw_legend(120, 170, ["gcc", "clang", "tcc"], set(compiler_data.keys())),
        draw_panel("N-second Throughput Trend", "Throughput (Million Ops/s)", "ops_m", 50, 210, 1700, 420, compiler_data),
        draw_panel("N-second RSS Footprint Trend", "RSS Footprint (MB)", "rss_mb", 50, 675, 1700, 420, compiler_data),
        draw_panel("N-second Memory Reclamation Ratio", "Reclamation Ratio (%)", "reclaim_pct", 50, 1140, 1700, 420, compiler_data),
    ]

    if embedded_data:
        parts.append(draw_embedded_section(50, 1605, 1700, 700, embedded_data))

    parts.append("</svg>")
    OUT_FILE.write_text("\n".join(parts), encoding="utf-8")
    print(f"Generated {OUT_FILE}")


if __name__ == "__main__":
    main()
