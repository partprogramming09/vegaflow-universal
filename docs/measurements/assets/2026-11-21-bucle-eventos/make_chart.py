#!/usr/bin/env python3
"""Draws idle-cpu.svg from idle-cpu.csv (2026-11-21 ABBA: daemon CPU time in 10 min at rest, no game).

idle-cpu.csv comes as-is from the laptop run (TotalProcessorTime before and after 600 s, after 30 s of warm-up).
Usage: make_chart.py
"""
import csv
import os

HERE = os.path.dirname(os.path.abspath(__file__))
COLORS = {"A": "#2a78d6", "B": "#eb6834"}
NAMES = {"A": "A: main 676449c (temporizador de 1,5 s)", "B": "B: E2.4 (despierta por eventos)"}


def draw():
    with open(os.path.join(HERE, "idle-cpu.csv")) as f:
        rows = list(csv.DictReader(f))
    assert len(rows) == 4 and [r["cond"] for r in rows] == ["A", "B", "B", "A"], "expected ABBA order"
    top = max(float(r["cpu_ms"]) for r in rows)
    x0, width, row_h = 150, 560, 34
    h = 110 + row_h * len(rows)
    sx = lambda v: x0 + v / (top * 1.1) * width
    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="880" height="{h}" viewBox="0 0 880 {h}" font-family="system-ui, sans-serif">',
           '<style>.t{font-size:14px;fill:#0b0b0b;font-weight:600}.m{font-size:12px;fill:#52514e}.g{stroke:#e4e3df;stroke-width:1}</style>',
           f'<rect width="880" height="{h}" fill="#fcfcfb"/>',
           f'<text x="16" y="28" class="t">Tiempo de CPU del daemon en 10 min de reposo, sin juego (ms)</text>']
    for tick in range(0, int(top * 1.1) + 1, 2000):
        svg.append(f'<line x1="{sx(tick):.1f}" y1="44" x2="{sx(tick):.1f}" y2="{50 + row_h * len(rows)}" class="g"/>')
        svg.append(f'<text x="{sx(tick):.1f}" y="{66 + row_h * len(rows)}" class="m" text-anchor="middle">{tick}</text>')
    for i, r in enumerate(rows):
        y = 52 + row_h * i
        v = float(r["cpu_ms"])
        svg.append(f'<text x="16" y="{y + 16}" class="m">Bloque {r["block"]} · {r["cond"]}</text>')
        svg.append(f'<rect x="{x0}" y="{y}" width="{sx(v) - x0:.1f}" height="22" rx="3" fill="{COLORS[r["cond"]]}">'
                   f'<title>Bloque {r["block"]} ({r["cond"]}): {v:.0f} ms en {r["seconds"]} s</title></rect>')
        svg.append(f'<text x="{sx(v) + 8:.1f}" y="{y + 16}" class="m">{v:.0f}</text>')
    ly = h - 20
    for j, c in enumerate(("A", "B")):
        x = x0 + 340 * j
        svg.append(f'<rect x="{x}" y="{ly - 10}" width="12" height="12" rx="2" fill="{COLORS[c]}"/><text x="{x + 18}" y="{ly}" class="m">{NAMES[c]}</text>')
    svg.append("</svg>")
    with open(os.path.join(HERE, "idle-cpu.svg"), "w") as f:
        f.write("\n".join(svg) + "\n")


if __name__ == "__main__":
    draw()
