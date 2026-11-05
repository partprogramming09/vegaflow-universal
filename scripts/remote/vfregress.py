#!/usr/bin/env python3
"""Regression gate: compares new frame CSVs of a scene with its saved baseline.

Use: python3 vfregress.py BASELINE.json 'GLOB/*.csv'
     python3 vfregress.py --make BASELINE.json SCENE 'GLOB_LAUNCH1' 'GLOB_LAUNCH2' ...   (one glob per game launch)
     python3 vfregress.py --self-test

--make stores, per scene, the median mean FPS, the pooled 1% Low and the noise measured between
launches. The check fails when the mean FPS drops by more than max(1%, 4 x its noise) or the
pooled 1% Low drops by more than max(3%, 4 x its noise), and warns when the presentation mode
changes. The baseline keeps summaries, not frames, so the 1% Low has no confidence interval here:
use `vfdev frames` to look into a failure. Use captures of 120 s or more (3 or more).
"""
import collections
import csv
import glob
import importlib.util
import json
import os
import statistics
import sys
import tempfile

_spec = importlib.util.spec_from_file_location("vfframes", os.path.join(os.path.dirname(os.path.abspath(__file__)), "vfframes.py"))
vf = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(vf)


def present_mode(pattern):
    """Returns the most common PresentMode of the CSVs that match the pattern."""
    c = collections.Counter()
    for p in glob.glob(pattern):
        with open(p, newline="", encoding="utf-8-sig") as f:
            c.update(r.get("PresentMode", "") for r in csv.DictReader(f))
    return c.most_common(1)[0][0] if c else ""


def summarize(pattern):
    """Returns (median mean FPS, pooled 1% Low) of all captures that match the pattern."""
    per, blocks = vf.load(pattern, 5.0)
    if not per:
        sys.exit("ningun CSV coincide con " + pattern)
    return statistics.median(r[3] for r in per), vf.pooled(blocks)


def spread(values):
    """Returns the range of the values as a percent of the smallest."""
    return 100.0 * (max(values) - min(values)) / min(values)


def make(path, scene, patterns):
    """Writes the baseline of a scene from one glob per launch."""
    runs = [summarize(p) for p in patterns]
    base = {"scene": scene, "launches": len(runs), "present_mode": present_mode(patterns[0]),
            "avg_fps": statistics.median(r[0] for r in runs), "low1": statistics.median(r[1] for r in runs),
            "noise_avg_pct": spread([r[0] for r in runs]), "noise_low_pct": spread([r[1] for r in runs])}
    json.dump(base, open(path, "w"), indent=1)
    print(json.dumps(base))


def check(path, pattern):
    """Prints PASS or FAIL for a new set of captures against the baseline and returns True when it passes."""
    b = json.load(open(path))
    avg, low = summarize(pattern)
    d_avg, d_low = 100.0 * (avg - b["avg_fps"]) / b["avg_fps"], 100.0 * (low - b["low1"]) / b["low1"]
    t_avg, t_low = max(1.0, 4 * b["noise_avg_pct"]), max(3.0, 4 * b["noise_low_pct"])
    mode = present_mode(pattern)
    ok = d_avg > -t_avg and d_low > -t_low
    print("%s  FPS medio %.2f contra base %.2f (%+.2f%%, limite -%.2f%%)  1%% Low agrupado %.2f contra %.2f (%+.2f%%, limite -%.2f%%)" % (
        "PASS" if ok else "FAIL", avg, b["avg_fps"], d_avg, t_avg, low, b["low1"], d_low, t_low))
    if mode != b["present_mode"]:
        print("AVISO: modo de presentacion %r, la base tiene %r: son configuraciones distintas" % (mode, b["present_mode"]))
    return ok


def self_test():
    """Builds two tiny CSV sets (same and 10% slower), checks PASS for the same and FAIL for the slower one."""
    d = tempfile.mkdtemp()

    def write(name, scale):
        with open(os.path.join(d, name), "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["TimeInMs", "MsBetweenPresents", "PresentMode"])
            for i in range(1500):
                w.writerow([6000 + i * 20.0, (20.0 + (3.0 if i % 100 == 0 else 0.0)) * scale, "Hardware: Independent Flip"])
    write("a1.csv", 1.0)
    write("a2.csv", 1.0)
    write("b1.csv", 1.1)
    base = os.path.join(d, "base.json")
    make(base, "test", [os.path.join(d, "a1.csv"), os.path.join(d, "a2.csv")])
    assert check(base, os.path.join(d, "a1.csv")) is True
    assert check(base, os.path.join(d, "b1.csv")) is False
    print("ok")


if __name__ == "__main__":
    a = sys.argv[1:]
    if a == ["--self-test"]:
        self_test()
    elif len(a) >= 4 and a[0] == "--make":
        make(a[1], a[2], a[3:])
    elif len(a) == 2:
        sys.exit(0 if check(a[0], a[1]) else 1)
    else:
        sys.exit(__doc__)
