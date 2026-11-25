#!/usr/bin/env python3
"""Revisa instancias .dat antes de correrlas (solo biblioteca estandar).

Detecta:
  1. Archivos inconsistentes: ids declarados en set O/N/M sin coordenadas
     o sin tarifa (el parser de C++ los rechaza).
  2. Instancias infactibles: puentes que ni solos caben en un turno Q
     (camino minimo de un dia que contiene al puente > Q).

Uso: python3 scripts/check_instances.py instances_v3 [instances ...]
"""
import glob
import heapq
import os
import re
import sys

INF = 1e18


def parse(fn):
    t = open(fn).read()
    sets = {k: list(map(int, re.search(r"set %s:=([^;]*);" % k, t).group(1).split())) for k in "ONMK"}
    Q = float(re.search(r"param Q:=\s*([\d.]+);", t).group(1))
    coords = re.search(r"param:\s*xcoord ycoord w q:=(.*?);", t, re.S).group(1)
    w, with_coord = {}, set()
    for ln in coords.strip().splitlines():
        p = ln.split()
        if len(p) >= 5:
            w[int(p[0])] = float(p[3]); with_coord.add(int(p[0]))
    rates = re.search(r"param f:=(.*?);", t, re.S).group(1).split()
    with_rate = {int(rates[i]) for i in range(0, len(rates) - 1, 2)}
    d, cols = {}, []
    for ln in t[t.index("param d"):].splitlines():
        ln = ln.split("#")[0].strip()
        if not ln or ln.startswith("param") or ln == ";":
            continue
        if ln.startswith(":"):
            cols = list(map(int, ln.strip(":=").replace(":", " ").replace("=", " ").split()))
            continue
        p = ln.split()
        for c, v in zip(cols, p[1:]):
            d[(int(p[0]), c)] = float(v)
    return sets, Q, w, with_coord, with_rate, d


def dijkstra(nodes, seeds, edge):
    dist = dict(seeds)
    pq = [(c, x) for x, c in seeds.items()]
    heapq.heapify(pq)
    while pq:
        c, x = heapq.heappop(pq)
        if c > dist[x]:
            continue
        for y, wgt in edge(x):
            if c + wgt < dist.get(y, INF):
                dist[y] = c + wgt; heapq.heappush(pq, (dist[y], y))
    return dist


def check(fn):
    s, Q, w, with_coord, with_rate, d = parse(fn)
    O, N, M = s["O"], s["N"], s["M"]
    missing = [i for i in O + N + M if i not in with_coord] + [m for m in M if m not in with_rate]
    if missing:
        return f"INCONSISTENTE: ids sin coordenadas/tarifa {sorted(set(missing))}"
    ends = set(O + M)
    to_end = dijkstra(N, {x: min([d[(x, e)] for e in ends if (x, e) in d] or [INF]) for x in N},
                      lambda x: [(y, d[(y, x)] + w[x]) for y in N if y != x and (y, x) in d])
    from_start = dijkstra(N, {x: min([d[(e, x)] for e in ends if (e, x) in d] or [INF]) + w[x] for x in N},
                          lambda x: [(y, d[(x, y)] + w[y]) for y in N if y != x and (x, y) in d])
    bad = {b: round(from_start[b] + to_end[b]) for b in N if from_start[b] + to_end[b] > Q}
    return f"INFACTIBLE: puentes que no caben en un turno Q={Q:.0f}: {bad}" if bad else None


if __name__ == "__main__":
    total = problems = 0
    for arg in sys.argv[1:]:
        for fn in sorted(glob.glob(os.path.join(arg, "*.dat"))):
            total += 1
            msg = check(fn)
            if msg:
                problems += 1
                print(f"{os.path.basename(fn)}: {msg}")
    print(f"{total} instancias revisadas, {problems} con problemas")
