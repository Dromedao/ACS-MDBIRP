#!/usr/bin/env python3
"""Resume uno o varios CSV de scripts/benchmark.sh por tamano (numero de puentes).

Uso: python3 scripts/summarize.py bench_out/old.csv [bench_out/v3.csv ...]
"""
import re
import sys
import statistics as st
from collections import defaultdict


def load(fn):
    rows, fails = [], []
    for ln in open(fn):
        p = ln.strip().split(",")
        if len(p) < 15:
            fails.append((p[1] if len(p) > 1 else "?", p[2] if len(p) > 2 else "?"))
            continue
        rows.append(dict(inst=p[1].split("/")[-1], greedy=float(p[3]), final=float(p[4]),
                         teams=int(p[5]), secs=float(p[7]), valid=p[8] == "1" and p[0] == "0"))
    return rows, fails


def nb(name):
    return int(re.search(r"_b(\d+)_", name).group(1))


for fn in sys.argv[1:]:
    rows, fails = load(fn)
    good = [r for r in rows if r["valid"]]
    print(f"\n=== {fn}: {len(rows) + len(fails)} corridas, {len(good)} validas, "
          f"{len(rows) - len(good) + len(fails)} fallidas")
    for inst, seed in sorted(set(fails)):
        print(f"  FALLA: {inst} (semilla {seed})")
    g = defaultdict(list)
    for r in good:
        g[nb(r["inst"])].append(r)
    print(f"{'b':>4} {'n':>4} {'greedy':>12} {'ACS':>12} {'mejora%':>8} {'equipos':>8} {'seg_med':>8} {'seg_max':>8}")
    for b in sorted(g):
        x = g[b]
        print(f"{b:>4} {len(x):>4} {st.mean(r['greedy'] for r in x):>12.0f} {st.mean(r['final'] for r in x):>12.0f} "
              f"{st.mean((r['greedy'] - r['final']) / r['greedy'] * 100 for r in x):>8.2f} "
              f"{st.mean(r['teams'] for r in x):>8.2f} {st.mean(r['secs'] for r in x):>8.2f} "
              f"{max(r['secs'] for r in x):>8.2f}")
    if good:
        print(f"TOTAL: mejora media del ACS sobre el greedy {st.mean((r['greedy'] - r['final']) / r['greedy'] * 100 for r in good):.2f}%, "
              f"tiempo acumulado {sum(r['secs'] for r in good):.0f}s")
