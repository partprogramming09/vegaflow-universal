#!/usr/bin/env python3
"""Builds summary.csv and octopath-f2-4.svg from the bench.ps1 JSON results of the 2026-11-22 F2.4 measurements.

Usage: make_chart.py <folder with the fetched f2base-*, f2fac-* and f2reg-* JSON files>
       make_chart.py            (redraws from summary.csv)
The 2026-11-09 baseline captures come from ../2026-11-09/n09fix-none-off-*.json (already in the repository).
One dot per counted capture (120 s): mean FPS and 1% Low as bench.ps1 computed them; the tick is the median.
"""
import csv
import glob
import json
import os
import re
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SUMMARY = os.path.join(HERE, "summary.csv")
OLD = os.path.join(HERE, "..", "2026-11-09")
# (section, row id, row label, file glob, label regex for the row)
ROWS = [
    ("Líneas base (preset, daemon apagado)", "base-old", "vieja 2026-11-09 (1 sesión)", OLD, r"^n09fix-none-off-d3d11-\d$"),
    ("Líneas base (preset, daemon apagado)", "base-new", "nueva 2026-11-22 (3 lanzamientos)", None, r"^f2base-full-b\d\dc\d$"),
    ("Preset por factor (ABBA)", "factory", "factory (sin preset)", None, r"^f2fac-factory-b\d\d$"),
    ("Preset por factor (ABBA)", "streaming", "streaming (9 líneas)", None, r"^f2fac-streaming-b\d\d$"),
    ("Preset por factor (ABBA)", "visual", "recortes visuales (18)", None, r"^f2fac-visual-b\d\d$"),
    ("Preset por factor (ABBA)", "async", "AsyncCompute (2)", None, r"^f2fac-async-b\d\d$"),
    ("Preset por factor (ABBA)", "full", "preset completo (29)", None, r"^f2fac-full-b\d\d$"),
    ("Regresión (daemon de main)", "reg-off", "daemon apagado", None, r"^f2reg-off-d3d11-\d$"),
    ("Regresión (daemon de main)", "reg-on", "daemon encendido", None, r"^f2reg-on-d3d11-\d$"),
]
DOT = "#2a78d6"


def summarize(folder):
    out = []
    for section, rid, _, src, pat in ROWS:
        for path in sorted(glob.glob(os.path.join(src or folder, "*.json"))):
            with open(path, encoding="utf-8-sig") as f:
                r = json.load(f)
            if re.match(pat, r["label"]):
                out.append({"row": rid, "label": r["label"], "frames": r["frames"], "avg_fps": r["avg_fps"],
                            "low1_fps": r["fps_1pct_low"], "present_mode": r["present_mode"]})
    with open(SUMMARY, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(out[0]))
        w.writeheader()
        w.writerows(out)


def panel(lines, rows, by_row, key, title, x0, lo, hi, step):
    width = 250
    sx = lambda v: x0 + (v - lo) / (hi - lo) * width
    bottom = rows[-1][1] + 14
    lines.append(f'<text x="{x0}" y="30" class="t">{title}</text>')
    v = lo
    while v <= hi + 1e-9:
        lines.append(f'<line x1="{sx(v):.1f}" y1="44" x2="{sx(v):.1f}" y2="{bottom}" class="g"/>')
        lines.append(f'<text x="{sx(v):.1f}" y="{bottom + 16}" class="m" text-anchor="middle">{v:g}</text>')
        v += step
    for rid, y in rows:
        vals = [float(r[key]) for r in by_row.get(rid, [])]
        if not vals:
            continue
        med = statistics.median(vals)
        lines.append(f'<line x1="{sx(med):.1f}" y1="{y - 9}" x2="{sx(med):.1f}" y2="{y + 9}" stroke="#0b0b0b" stroke-width="2"/>')
        for r in by_row[rid]:
            x = sx(float(r[key]))
            lines.append(f'<circle cx="{x:.1f}" cy="{y}" r="5" fill="{DOT}" fill-opacity="0.75" stroke="#fcfcfb" stroke-width="2">'
                         f'<title>{r["label"]}: {float(r[key]):.2f} FPS ({r["present_mode"]})</title></circle>')
        lines.append(f'<text x="{x0 + width + 8}" y="{y + 4}" class="m">{med:.1f}</text>')


def draw():
    with open(SUMMARY) as f:
        data = list(csv.DictReader(f))
    by_row = {}
    for r in data:
        by_row.setdefault(r["row"], []).append(r)
    rows, lines, y, last = [], [], 44, None
    for section, rid, label, _, _ in ROWS:
        if rid not in by_row:
            continue
        if section != last:
            y += 26
            lines.append(f'<text x="16" y="{y}" class="s">{section}</text>')
            y += 22
            last = section
        lines.append(f'<text x="28" y="{y + 4}" class="m">{label} · n={len(by_row[rid])}</text>')
        rows.append((rid, y))
        y += 24
    lo_a = min(float(r["avg_fps"]) for r in data) // 2 * 2
    hi_a = -(-max(float(r["avg_fps"]) for r in data) // 2) * 2
    lo_l = min(float(r["low1_fps"]) for r in data) // 2 * 2
    hi_l = -(-max(float(r["low1_fps"]) for r in data) // 2) * 2
    h = y + 40
    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="980" height="{h}" viewBox="0 0 980 {h}" font-family="system-ui, sans-serif">',
           '<style>.t{font-size:14px;fill:#0b0b0b;font-weight:600}.s{font-size:13px;fill:#0b0b0b;font-weight:600}'
           '.m{font-size:12px;fill:#52514e}.g{stroke:#e4e3df;stroke-width:1}</style>',
           f'<rect width="980" height="{h}" fill="#fcfcfb"/>']
    svg += lines
    panel(svg, rows, by_row, "avg_fps", "FPS medio por captura (raya = mediana)", 300, lo_a, hi_a, 2)
    panel(svg, rows, by_row, "low1_fps", "1% Low por captura (raya = mediana)", 660, lo_l, hi_l, 2)
    svg.append("</svg>")
    with open(os.path.join(HERE, "octopath-f2-4.svg"), "w") as f:
        f.write("\n".join(svg) + "\n")


if __name__ == "__main__":
    if len(sys.argv) > 1:
        summarize(sys.argv[1])
    draw()
