#!/usr/bin/env python3
"""Aggregates the result rows printed by the VegaFlow measurement jobs (ab, bg-factors, sensors).

Use: python3 vfstats.py FILE|- [LABEL_PREFIX]   (- reads standard input)
     python3 vfstats.py --self-test

Rows are grouped by label without the trailing -N repetition; each column shows
mean, median and range. Compare conditions measured inside the same job only: the
confined-background condition changes by up to ~10% between jobs.
"""
import re
import statistics
import sys

SENS_RE = re.compile(r"^sens,")
NUMBER = r"-?\d+(\.\d+)?"


def stats(text, prefix=""):
    """Groups result rows by label and returns one line per group with n, mean, median and range per column.

    Understands ab rows (label,frames,avg,1%,0.1%,sd,mode), bg-factors rows
    (label,avg,1%,0.1%,sd,procesos=N,bg_cpu_s=X) and sens rows (sens,label,key=value,...).
    """
    groups = {}
    for raw in text.splitlines():
        parts = [p.strip() for p in raw.strip().split(",")]
        if len(parts) < 3:
            continue
        if SENS_RE.match(raw):
            label, cols = parts[1], {}
            for kv in parts[2:]:
                k, _, v = kv.partition("=")
                try:
                    cols[k] = float(v)
                except ValueError:
                    pass
        else:
            label = parts[0]
            nums = [float(x) for x in parts[1:6] if re.fullmatch(NUMBER, x)]
            extra = {}
            for kv in parts[1:]:
                k, _, v = kv.partition("=")
                if v and re.fullmatch(NUMBER, v):
                    extra[k] = float(v)
            if len(parts) >= 7 and re.fullmatch(r"\d+", parts[1]) and not parts[6].startswith("bg_cpu_s"):
                cols = dict(zip(["frames", "avg", "low1", "low01", "sd"], nums[:5]))
            elif extra.get("bg_cpu_s") is not None and len(nums) >= 4:
                cols = dict(zip(["avg", "low1", "low01", "sd"], nums[:4]))
                cols["bg_cpu_s"] = extra["bg_cpu_s"]
            else:
                continue
        if not label.startswith(prefix):
            continue
        key = ("sens " if SENS_RE.match(raw) else "") + re.sub(r"-\d+$", "", label)
        groups.setdefault(key, []).append(cols)
    out = []
    for key, rows in groups.items():
        names = [n for n in rows[0] if n not in ("frames", "n", "procesos")]
        cells = []
        for n in names:
            vals = [r[n] for r in rows if n in r]
            cells.append("%s=%.1f med=%.1f [%.1f-%.1f]" % (n, sum(vals) / len(vals), statistics.median(vals), min(vals), max(vals)))
        out.append("%s n=%d  %s" % (key, len(rows), "  ".join(cells)))
    return out


def self_test():
    """Checks the three row shapes, the median and the label prefix filter."""
    sample = ("a-on-d3d11-1,100,50.0,40.0,30.0,1.0,Hardware: Independent Flip\n"
              "a-on-d3d11-2,100,60.0,40.0,30.0,1.0,Hardware: Independent Flip\n"
              "a-on-d3d11-3,100,90.0,40.0,30.0,1.0,Hardware: Independent Flip\n"
              "bgt-aff2-d3d11-w6-1,113.0,70.0,66.0,1.3,procesos=8,bg_cpu_s=60.0\n"
              "sens,sc-on-d3d11-1,n=115,temp_avg=82.9,temp_max=85.1\n")
    got = stats(sample)
    assert got[0].startswith("a-on-d3d11 n=3") and "avg=66.7 med=60.0 [50.0-90.0]" in got[0], got
    assert got[1].startswith("bgt-aff2-d3d11-w6 n=1") and "bg_cpu_s=60.0" in got[1], got
    assert got[2].startswith("sens sc-on-d3d11 n=1") and "temp_max=85.1" in got[2], got
    assert stats(sample, "bgt") == [got[1]]
    print("ok")


def main(argv):
    """Reads a results file (or - for standard input) and prints the grouped statistics."""
    if argv[:1] == ["--self-test"]:
        self_test()
        return 0
    if len(argv) not in (1, 2):
        print(__doc__, file=sys.stderr)
        return 64
    text = sys.stdin.read() if argv[0] == "-" else open(argv[0], encoding="utf-8", errors="replace").read()
    print("\n".join(stats(text, argv[1] if len(argv) == 2 else "")))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
