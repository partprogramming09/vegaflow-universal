#!/usr/bin/env python3
"""Builds summary.csv and fse-octopath.svg from the PresentMon CSVs of the 2026-11-20 FSE A/B (Octopath Traveler II).

Usage: make_chart.py <folder with a1/ b1/ b2/ a2/ unzipped packs>   (without arguments it redraws from summary.csv)
"""
import csv
import glob
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SUMMARY = os.path.join(HERE, "summary.csv")
COLORS = {"A": "#2a78d6", "B": "#eb6834"}
NAMES = {"A": "A: FSE 2,1,1,0 (como estaba)", "B": "B: valores borrados (Windows decide)"}


def summarize(root):
    rows = []
    for block in ("a1", "b1", "b2", "a2"):
        for path in sorted(glob.glob(os.path.join(root, block, "*-[12]-*.csv"))):
            with open(path, encoding="utf-8-sig") as f:
                frames = list(csv.DictReader(f))
            ms = sorted(float(r["MsBetweenPresents"]) for r in frames if r.get("MsBetweenPresents"))
            avg = 1000.0 * len(ms) / sum(ms)
            worst = ms[-max(1, len(ms) // 100):]  # slowest 1% of frames
            low = 1000.0 * len(worst) / sum(worst)
            modes = {r.get("PresentMode", "") for r in frames}
            rows.append({"block": block.upper(), "condition": block[0].upper(), "frames": len(ms),
                         "avg_fps": round(avg, 2), "low1_fps": round(low, 2), "present_mode": "|".join(sorted(modes))})
    assert len(rows) == 8, f"expected 8 captures, found {len(rows)}"
    with open(SUMMARY, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)


def panel(rows, key, title, x0, lo, hi):
    """One dot panel: one row per capture, x = FPS on a shared scale lo..hi."""
    out = [f'<text x="{x0}" y="40" class="t">{title}</text>']
    width = 240
    sx = lambda v: x0 + (v - lo) / (hi - lo) * width
    for tick in range(int(lo), int(hi) + 1, 2):
        out.append(f'<line x1="{sx(tick):.1f}" y1="55" x2="{sx(tick):.1f}" y2="{60 + 24 * len(rows)}" class="g"/>')
        out.append(f'<text x="{sx(tick):.1f}" y="{78 + 24 * len(rows)}" class="m" text-anchor="middle">{tick}</text>')
    for i, r in enumerate(rows):
        y = 70 + 24 * i
        v = float(r[key])
        out.append(f'<circle cx="{sx(v):.1f}" cy="{y}" r="5" fill="{COLORS[r["condition"]]}" stroke="#fcfcfb" stroke-width="2">'
                   f'<title>{r["block"]} captura: {v:.2f} FPS ({r["present_mode"]})</title></circle>')
        out.append(f'<text x="{sx(v) + 10:.1f}" y="{y + 4}" class="m">{v:.1f}</text>')
    return out


def draw():
    with open(SUMMARY) as f:
        rows = list(csv.DictReader(f))
    h = 130 + 24 * len(rows)
    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="880" height="{h}" viewBox="0 0 880 {h}" font-family="system-ui, sans-serif">',
           '<style>.t{font-size:14px;fill:#0b0b0b;font-weight:600}.m{font-size:12px;fill:#52514e}.g{stroke:#e4e3df;stroke-width:1}</style>',
           f'<rect width="880" height="{h}" fill="#fcfcfb"/>']
    for i, r in enumerate(rows):
        svg.append(f'<text x="16" y="{74 + 24 * i}" class="m">{r["block"]} · {r["present_mode"].replace("Hardware: ", "")}</text>')
    svg += panel(rows, "avg_fps", "FPS medio", 200, 28, 32)
    svg += panel(rows, "low1_fps", "1% Low (FPS)", 560, 22, 30)
    ly = h - 22
    for j, c in enumerate(("A", "B")):
        x = 200 + 330 * j
        svg.append(f'<circle cx="{x}" cy="{ly - 4}" r="5" fill="{COLORS[c]}"/><text x="{x + 12}" y="{ly}" class="m">{NAMES[c]}</text>')
    svg.append("</svg>")
    with open(os.path.join(HERE, "fse-octopath.svg"), "w") as f:
        f.write("\n".join(svg) + "\n")


if __name__ == "__main__":
    if len(sys.argv) > 1:
        summarize(sys.argv[1])
    draw()
