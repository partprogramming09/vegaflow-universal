#!/usr/bin/env python3
"""Compares two conditions from the PresentMon frame CSVs saved by scripts/bench.ps1.

Use: python3 vfframes.py GLOB_A GLOB_B [--warmup 5]
     python3 vfframes.py --self-test

Frames of every capture of a condition are pooled, and the 1% Low difference gets a 95%
confidence interval from resampling one-second blocks (frames next to each other are not
independent). The verdict applies the thresholds fixed in the test plan:
mean FPS needs >= 3% between medians, non-overlapping ranges and >= 5 captures per condition;
1% Low needs captures >= 120 s, a difference over 5% and an interval that excludes 0.
The 0.1% Low is never used here: it needs captures of 5 minutes or more.
"""
import csv
import glob
import math
import random
import statistics
import sys


def lows(frametimes, fraction):
    """Returns the FPS of the mean of the slowest frames past `fraction` (same definition as bench.ps1)."""
    s = sorted(frametimes)
    tail = s[min(int(math.floor(fraction * len(s))), len(s) - 1):]
    mean = sum(tail) / len(tail)
    return 1000.0 / mean if mean > 0 else 0.0


def read_capture(path, warmup):
    """Returns {second: [frametime ms]} for the frames of one CSV after the warmup."""
    blocks = {}
    with open(path, newline="", encoding="utf-8-sig") as f:
        for r in csv.DictReader(f):
            t = float(r["TimeInMs"]) / 1000.0 if r.get("TimeInMs") else float(r["TimeInSeconds"])
            ms = r.get("MsBetweenPresents", "NA")
            if t < warmup or ms in ("NA", ""):
                continue
            if float(ms) > 0:
                blocks.setdefault(int(t), []).append(float(ms))
    return blocks


def load(pattern, warmup):
    """Returns (per-capture rows (path, seconds, frames, avg fps, 1% low), all one-second blocks)."""
    per, blocks = [], []
    for p in sorted(glob.glob(pattern)):
        by = read_capture(p, warmup)
        ft = [x for b in by.values() for x in b]
        per.append((p, len(by), len(ft), 1000.0 * len(ft) / sum(ft), lows(ft, 0.99)))
        blocks.extend(by.values())
    return per, blocks


def pooled(blocks):
    """Returns the 1% Low of all frames in the blocks."""
    return lows([x for b in blocks for x in b], 0.99)


def bootstrap(a, b, n=2000, seed=1):
    """Returns the 95% interval of the 1% Low change from a to b in percent, resampling one-second blocks."""
    rng, d = random.Random(seed), []
    for _ in range(n):
        la, lb = pooled(rng.choices(a, k=len(a))), pooled(rng.choices(b, k=len(b)))
        d.append(100.0 * (lb - la) / la)
    d.sort()
    return d[int(0.025 * n)], d[int(0.975 * n) - 1]


def compare(pa, ba, pb, bb):
    """Prints the per-capture rows, the pooled numbers and the verdict for both thresholds."""
    for name, per in (("A", pa), ("B", pb)):
        for p, sec, frames, avg, low1 in per:
            print("%s %s  %ds %d cuadros  avg=%.2f  1%%low=%.2f" % (name, p, sec, frames, avg, low1))
    fa, fb = [r[3] for r in pa], [r[3] for r in pb]
    ma, mb = statistics.median(fa), statistics.median(fb)
    d = 100.0 * (mb - ma) / ma
    overlap = not (min(fb) > max(fa) or max(fb) < min(fa))
    ok = abs(d) >= 3 and not overlap and len(fa) >= 5 and len(fb) >= 5
    print("FPS medio: A %.2f [%.2f-%.2f]  B %.2f [%.2f-%.2f]  cambio %+.1f%%  -> %s" % (
        ma, min(fa), max(fa), mb, min(fb), max(fb), d,
        "cuenta" if ok else "no cuenta (necesita 3%%, rangos sin solape y 5 capturas por condicion; n=%d y %d)" % (len(fa), len(fb))))
    la, lb = pooled(ba), pooled(bb)
    lo, hi = bootstrap(ba, bb)
    long_enough = all(r[1] >= 120 for r in pa + pb)
    ok = long_enough and abs(100.0 * (lb - la) / la) >= 5 and not (lo <= 0 <= hi)
    print("1%% Low agrupado: A %.2f  B %.2f  cambio %+.1f%%  IC95 [%+.1f%%, %+.1f%%]  -> %s" % (
        la, lb, 100.0 * (lb - la) / la, lo, hi,
        "cuenta" if ok else "no cuenta (necesita capturas de 120 s o mas, cambio sobre 5% y un intervalo que excluya 0)"))


def self_test():
    """Checks the low definition, the interval on equal data (contains 0) and on shifted data (excludes 0)."""
    assert abs(lows([10.0] * 99 + [100.0], 0.99) - 10.0) < 1e-9
    rng = random.Random(7)
    a = [[rng.gauss(23.0, 1.0) if rng.random() > 0.02 else 40.0 for _ in range(43)] for _ in range(300)]
    same = [[rng.gauss(23.0, 1.0) if rng.random() > 0.02 else 40.0 for _ in range(43)] for _ in range(300)]
    slower = [[x * 1.2 for x in b] for b in same]
    lo, hi = bootstrap(a, same, n=300)
    assert lo <= 0 <= hi, (lo, hi)
    lo, hi = bootstrap(a, slower, n=300)
    assert hi < 0, (lo, hi)
    print("ok")


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        self_test()
        sys.exit(0)
    args = [x for x in sys.argv[1:] if not x.startswith("--")]
    w = float(sys.argv[sys.argv.index("--warmup") + 1]) if "--warmup" in sys.argv else 5.0
    if "--warmup" in sys.argv:
        args.remove(sys.argv[sys.argv.index("--warmup") + 1])
    if len(args) != 2:
        sys.exit(__doc__)
    pa, ba = load(args[0], w)
    pb, bb = load(args[1], w)
    if not pa or not pb:
        sys.exit("ningun CSV coincide con alguno de los dos patrones")
    compare(pa, ba, pb, bb)
