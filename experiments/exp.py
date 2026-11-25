#!/usr/bin/env python3
"""Orquestador de experimentos del MDBIRP con ACS (solo biblioteca estándar).

Etapas (se ejecutan en este orden, cada una guarda sus resultados en results/):

  hardware   registra CPU, memoria y compilador
  subset     elige el 40 % de las instancias para sintonizar (estratificado por tamaño)
  tune       busca los parámetros del ACS en ese subset con las semillas 1-5
  calibrate  mide el factor k = tiempo con búsqueda local / tiempo sin ella
  evaluate   corre todas las instancias con las semillas 101-120 en cuatro variantes
  traces     guarda convergencia y soluciones de instancias representativas

Etapas aparte (no entran en 'all'):

  ablation   operadores de búsqueda local por separado
  budget     sensibilidad al presupuesto (10 veces las iteraciones) en b25 y b60
  noise      variación entre semillas de una misma configuración
  evalcfg    evalúa una configuración dada (la usa optuna_tune.py)

Uso:  python3 experiments/exp.py <etapa> [--quick]
      python3 experiments/exp.py all
El análisis (tablas, pruebas y gráficas) lo hace analyze.py a partir de results/.
"""
import argparse
import csv
import glob
import json
import os
import platform
import random
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "mdbirp")
EXP = os.path.join(ROOT, "experiments")
RES = os.path.join(EXP, "results")
SUBSET_FILE = os.path.join(EXP, "subset_tuning.txt")
INSTANCES = sorted(glob.glob(os.path.join(ROOT, "instances", "*.dat")))

TUNE_SEEDS = [1, 2, 3, 4, 5]
EVAL_SEEDS = list(range(101, 121))
BUDGET = 1000            # hormigas x iteraciones en la sintonización (mismo cómputo por configuración)
SUBSET_SEED = 2026

FIELDS = ["instance", "seed", "greedy_cost", "final_cost", "teams", "iterations", "seconds",
          "valid", "greedy_teams", "travel", "dispatch", "lodging", "nights", "lb_teams"]

DEFAULT = dict(alpha=1.0, beta=2.0, rho=0.1, xi=0.1, q0=0.9, ants=10, ls_moves="all", ls_prob=1.0)


def family(path):
    """Cantidad de puentes según el nombre: Instance_b30_... -> 30."""
    return int(os.path.basename(path).split("_")[1][1:])


def flags(cfg, iterations=None, ls_moves=None):
    its = iterations if iterations is not None else max(1, BUDGET // cfg["ants"])
    return ["--alpha", str(cfg["alpha"]), "--beta", str(cfg["beta"]), "--rho", str(cfg["rho"]),
            "--xi", str(cfg["xi"]), "--q0", str(cfg["q0"]), "--ants", str(cfg["ants"]),
            "--iterations", str(its), "--ls-moves", ls_moves or cfg["ls_moves"],
            "--ls-prob", str(cfg["ls_prob"])]


def run_one(instance, seed, extra):
    """Corre el binario con --csv y devuelve la fila como diccionario."""
    cmd = [BIN, instance, "--seed", str(seed), "--csv"] + extra
    p = subprocess.run(cmd, capture_output=True, text=True)
    line = p.stdout.strip().splitlines()
    if not line:
        raise RuntimeError(f"sin salida: {' '.join(cmd)}\n{p.stderr}")
    row = dict(zip(FIELDS, line[-1].split(",")))
    row["instance"] = os.path.basename(row["instance"])
    return row


def pmap(fn, items, workers):
    with ThreadPoolExecutor(max_workers=workers) as ex:
        return list(ex.map(fn, items))


def workers(timing=False):
    n = os.cpu_count() or 4
    return max(1, n // 2) if timing else n


def load_subset():
    with open(SUBSET_FILE) as f:
        names = [l.strip() for l in f if l.strip()]
    return [os.path.join(ROOT, "instances", n) for n in names]


# etapas
def stage_hardware(_a):
    os.makedirs(RES, exist_ok=True)
    def sh(c):
        return subprocess.run(c, shell=True, capture_output=True, text=True).stdout.strip()
    txt = "\n".join([
        "CPU: " + sh("lscpu | grep 'Model name' | sed 's/.*: *//'"),
        "Núcleos lógicos: " + str(os.cpu_count()),
        "Memoria: " + sh("free -h | awk '/Mem/{print $2}'"),
        "SO: " + sh("grep PRETTY_NAME /etc/os-release | cut -d= -f2 | tr -d '\"'") + " (" + platform.platform() + ")",
        "Compilador: " + sh("g++ --version | head -1"),
        "Flags: -std=c++17 -Wall -Wextra -O2",
    ])
    with open(os.path.join(RES, "hardware.txt"), "w") as f:
        f.write(txt + "\n")
    print(txt)


def stage_subset(a):
    """40 % de cada familia (8 de 20), fijado con una semilla; b71 no se usa para sintonizar."""
    rng = random.Random(SUBSET_SEED)
    chosen = []
    fams = sorted({family(p) for p in INSTANCES})
    for fam in fams:
        names = [os.path.basename(p) for p in INSTANCES if family(p) == fam]
        if len(names) < 20:
            continue
        chosen += rng.sample(names, round(0.4 * len(names)))
    chosen.sort()
    with open(SUBSET_FILE, "w") as f:
        f.write("\n".join(chosen) + "\n")
    print(f"subset: {len(chosen)} de {len(INSTANCES)} instancias -> {SUBSET_FILE}")


def sample_config(rng):
    return dict(alpha=round(rng.uniform(0.5, 2.0), 2), beta=round(rng.uniform(1.0, 5.0), 2),
                rho=round(rng.uniform(0.05, 0.5), 2), xi=round(rng.uniform(0.05, 0.3), 2),
                q0=round(rng.uniform(0.5, 0.98), 2), ants=rng.choice([5, 10, 20, 40]),
                ls_moves=rng.choice(["none", "2opt", "relocate", "all"]),
                ls_prob=rng.choice([0.25, 0.5, 1.0]))


def perturb(cfg, rng):
    c = dict(cfg)
    def jig(k, lo, hi, d=2):
        c[k] = round(min(hi, max(lo, c[k] * rng.uniform(0.85, 1.15))), d)
    jig("alpha", 0.5, 2.0); jig("beta", 1.0, 5.0); jig("rho", 0.05, 0.5)
    jig("xi", 0.05, 0.3); jig("q0", 0.5, 0.98)
    if rng.random() < 0.3:
        c["ants"] = rng.choice([5, 10, 20, 40])
    if rng.random() < 0.3:
        c["ls_moves"] = rng.choice(["none", "2opt", "relocate", "all"])
        c["ls_prob"] = rng.choice([0.25, 0.5, 1.0])
    return c


def stage_tune(a):
    """Búsqueda aleatoria + refinamiento. Objetivo: costo final / costo del greedy de referencia
    (greedy con los parámetros por defecto), promediado por instancia y luego entre instancias.
    Se usa una referencia fija porque el greedy depende de beta."""
    os.makedirs(RES, exist_ok=True)
    subset = load_subset()
    if a.quick:
        subset = subset[::8]
    seeds = TUNE_SEEDS[:2] if a.quick else TUNE_SEEDS
    n_random, n_refine = (3, 2) if a.quick else (60, 20)

    refs = {os.path.basename(p): run_one(p, 1, ["--iterations", "0"]) for p in subset}
    ref = {k: float(r["final_cost"]) for k, r in refs.items()}
    ref_nd = {k: float(r["travel"]) + float(r["lodging"]) for k, r in refs.items()}   # sin despacho
    rng = random.Random(7)
    log_path = os.path.join(RES, "tuning.csv")
    cols = list(DEFAULT) + ["phase", "objective", "objective_nodispatch", "teams", "seconds"]
    log = open(log_path, "w", newline="")
    w = csv.writer(log); w.writerow(cols)

    def evaluate(cfg, phase):
        jobs = [(p, s) for p in subset for s in seeds]
        rows = pmap(lambda j: run_one(j[0], j[1], flags(cfg)), jobs, workers())
        rel, rel_nd = {}, {}
        for (p, _), r in zip(jobs, rows):
            if r["valid"] != "1":
                raise RuntimeError(f"solución inválida en sintonización: {p}")
            k = os.path.basename(p)
            rel.setdefault(k, []).append(float(r["final_cost"]) / ref[k])
            rel_nd.setdefault(k, []).append((float(r["travel"]) + float(r["lodging"])) / ref_nd[k])
        obj = sum(sum(v) / len(v) for v in rel.values()) / len(rel)
        obj_nd = sum(sum(v) / len(v) for v in rel_nd.values()) / len(rel_nd)
        res = dict(cfg, phase=phase, objective=obj, objective_nodispatch=obj_nd,
                   teams=sum(float(r["teams"]) for r in rows) / len(rows),
                   seconds=sum(float(r["seconds"]) for r in rows) / len(rows))
        w.writerow([res[c] for c in cols]); log.flush()
        print(f"[{phase}] obj={obj:.5f} sin_despacho={obj_nd:.5f} {cfg}", flush=True)
        return obj

    results = [(evaluate(DEFAULT, "default"), DEFAULT)]
    for _ in range(n_random):
        c = sample_config(rng); results.append((evaluate(c, "random"), c))
    best = min(results, key=lambda t: t[0])[1]
    for _ in range(n_refine):
        c = perturb(best, rng); results.append((evaluate(c, "refine"), c))
    obj, best = min(results, key=lambda t: t[0])
    log.close()
    out = dict(best, objective=obj, default_objective=results[0][0],
               budget=BUDGET, tune_seeds=seeds, subset_size=len(subset))
    with open(os.path.join(RES, "tuned_config.json"), "w") as f:
        json.dump(out, f, indent=2)
    print("mejor configuración:", out)


def ls_variant_moves(cfg):
    """Variante con búsqueda local: la sintonizada, o 'all' si la sintonización prefirió 'none'."""
    return (cfg["ls_moves"], False) if cfg["ls_moves"] != "none" else ("all", True)


def stage_calibrate(a):
    """k = tiempo medio con búsqueda local / tiempo medio sin ella, en el subset de sintonización."""
    with open(os.path.join(RES, "tuned_config.json")) as f:
        cfg = json.load(f)
    subset = load_subset()
    if a.quick:
        subset = subset[::8]
    seeds = TUNE_SEEDS[:2] if a.quick else TUNE_SEEDS
    moves, forced = ls_variant_moves(cfg)
    jobs = [(p, s) for p in subset for s in seeds]
    t_ls = pmap(lambda j: float(run_one(j[0], j[1], flags(cfg, ls_moves=moves))["seconds"]), jobs, workers(True))
    t_no = pmap(lambda j: float(run_one(j[0], j[1], flags(cfg, ls_moves="none"))["seconds"]), jobs, workers(True))
    cfg["k"] = (sum(t_ls) / len(t_ls)) / (sum(t_no) / len(t_no))
    cfg["time_ls"] = sum(t_ls) / len(t_ls)
    cfg["time_nols"] = sum(t_no) / len(t_no)
    cfg["ls_variant_moves"] = moves
    cfg["ls_forced"] = forced
    with open(os.path.join(RES, "tuned_config.json"), "w") as f:
        json.dump(cfg, f, indent=2)
    print(f"k = {cfg['k']:.3f}  (con LS {cfg['time_ls']:.3f} s, sin LS {cfg['time_nols']:.3f} s)")


def variants(cfg):
    its = max(1, BUDGET // cfg["ants"])
    return {
        "greedy": flags(cfg, iterations=0),
        "acs_nols": flags(cfg, ls_moves="none"),
        "acs_ls": flags(cfg, ls_moves=cfg["ls_variant_moves"]),
        "acs_nols_matched": flags(cfg, iterations=max(1, round(its * cfg["k"])), ls_moves="none"),
    }


def stage_evaluate(a):
    with open(os.path.join(RES, "tuned_config.json")) as f:
        cfg = json.load(f)
    insts = INSTANCES[::40] if a.quick else INSTANCES
    seeds = EVAL_SEEDS[:3] if a.quick else EVAL_SEEDS
    out_path = os.path.join(RES, "eval_runs.csv")
    done = set()
    if os.path.exists(out_path) and not a.quick:
        with open(out_path) as f:
            for r in csv.DictReader(f):
                done.add((r["variant"], r["instance"], r["seed"]))
    new_file = not os.path.exists(out_path) or a.quick
    f = open(out_path, "w" if new_file else "a", newline="")
    wr = csv.writer(f)
    if new_file:
        wr.writerow(["variant"] + FIELDS)
    lock = threading.Lock()
    vs = variants(cfg)
    jobs = [(v, p, s) for v in vs for p in insts for s in seeds
            if (v, os.path.basename(p), str(s)) not in done]
    print(f"{len(jobs)} corridas pendientes")

    def work(j):
        v, p, s = j
        r = run_one(p, s, vs[v])
        with lock:
            wr.writerow([v] + [r[c] for c in FIELDS]); f.flush()
        return r
    t0 = time.time()
    pmap(work, jobs, workers(True))
    f.close()
    print(f"evaluación terminada en {time.time() - t0:.0f} s -> {out_path}")


def stage_traces(a):
    with open(os.path.join(RES, "tuned_config.json")) as f:
        cfg = json.load(f)
    tdir, sdir = os.path.join(RES, "traces"), os.path.join(RES, "solutions")
    os.makedirs(tdir, exist_ok=True); os.makedirs(sdir, exist_ok=True)
    vs = variants(cfg)
    reps = []
    for fam in sorted({family(p) for p in INSTANCES}):
        reps.append([p for p in INSTANCES if family(p) == fam][0])
    seeds = EVAL_SEEDS[:3] if a.quick else EVAL_SEEDS
    jobs = []
    for p in reps:
        base = os.path.basename(p)[:-4]
        for v in ("acs_nols", "acs_ls"):
            for s in seeds:
                jobs.append((p, s, vs[v] + ["--trace", os.path.join(tdir, f"{v}__{base}__{s}.csv")]
                             + (["--solution", os.path.join(sdir, f"{v}__{base}.csv")] if s == seeds[0] else [])))
        jobs.append((p, seeds[0], vs["greedy"] + ["--solution", os.path.join(sdir, f"greedy__{base}.csv")]))
    pmap(lambda j: run_one(j[0], j[1], j[2]), jobs, workers(True))
    print(f"{len(jobs)} corridas con traza/solución -> {tdir}, {sdir}")


def stage_ablation(a):
    """Ablación de operadores de búsqueda local con la configuración sintonizada:
    none, 2opt, relocate y all, mismas iteraciones, todas las instancias, semillas 101-120."""
    with open(os.path.join(RES, "tuned_config.json")) as f:
        cfg = json.load(f)
    insts = INSTANCES[::40] if a.quick else INSTANCES
    seeds = EVAL_SEEDS[:3] if a.quick else EVAL_SEEDS
    ops = ["none", "2opt", "relocate", "all"]
    jobs = [(m, p, s) for m in ops for p in insts for s in seeds]
    print(f"{len(jobs)} corridas")
    rows = pmap(lambda j: (j[0], run_one(j[1], j[2], flags(cfg, ls_moves=j[0]))), jobs, workers(True))
    with open(os.path.join(RES, "ablation_runs.csv"), "w", newline="") as f:
        w = csv.writer(f); w.writerow(["ls_moves"] + FIELDS)
        for m, r in rows:
            w.writerow([m] + [r[c] for c in FIELDS])


def stage_budget(a):
    """Sensibilidad al presupuesto: b25 y b60 con 10 veces las iteraciones (configuración sintonizada, con la
    búsqueda local de la configuración), semillas 101-120. Interesa cuántas corridas llegan a 1 equipo."""
    with open(os.path.join(RES, "tuned_config.json")) as f:
        cfg = json.load(f)
    insts = [p for p in INSTANCES if family(p) in (25, 60)]
    seeds = EVAL_SEEDS[:3] if a.quick else EVAL_SEEDS
    its = 10 * max(1, BUDGET // cfg["ants"])
    jobs = [(p, s) for p in insts for s in seeds]
    print(f"{len(jobs)} corridas con {its} iteraciones")
    rows = pmap(lambda j: run_one(j[0], j[1], flags(cfg, iterations=its)), jobs, workers(True))
    with open(os.path.join(RES, "budget_runs.csv"), "w", newline="") as f:
        w = csv.writer(f); w.writerow(FIELDS)
        for r in rows:
            w.writerow([r[c] for c in FIELDS])


def subset_refs(insts):
    """Costo y costo sin despacho del greedy de referencia (parámetros por defecto)."""
    refs = {os.path.basename(p): run_one(p, 1, ["--iterations", "0"]) for p in insts}
    return ({k: float(r["final_cost"]) for k, r in refs.items()},
            {k: float(r["travel"]) + float(r["lodging"]) for k, r in refs.items()})


def objective(cfg, insts, seeds, ref, ref_nd):
    """Mismo objetivo que la sintonización: costo final / costo del greedy de referencia."""
    jobs = [(p, s) for p in insts for s in seeds]
    rows = pmap(lambda j: run_one(j[0], j[1], flags(cfg)), jobs, workers())
    rel, rel_nd = {}, {}
    for (p, _), r in zip(jobs, rows):
        if r["valid"] != "1":
            raise RuntimeError(f"solución inválida: {p}")
        k = os.path.basename(p)
        rel.setdefault(k, []).append(float(r["final_cost"]) / ref[k])
        rel_nd.setdefault(k, []).append((float(r["travel"]) + float(r["lodging"])) / ref_nd[k])
    return (sum(sum(v) / len(v) for v in rel.values()) / len(rel),
            sum(sum(v) / len(v) for v in rel_nd.values()) / len(rel_nd),
            sum(float(r["teams"]) for r in rows) / len(rows))


def parse_cfg(text):
    """alpha=1.2,beta=2,... -> dict con los tipos correctos."""
    cfg = dict(DEFAULT)
    for kv in text.split(","):
        k, v = kv.split("=")
        cfg[k] = v if k == "ls_moves" else (int(v) if k == "ants" else float(v))
    return cfg


def stage_evalcfg(a):
    """Evalúa una configuración (para Optuna y para comparar sintonizadores).
    --on subset: subset de sintonización con semillas 1-5; --on rest: el resto de las instancias
    (menos b71) con las semillas 101-105."""
    cfg = parse_cfg(a.cfg)
    sub = set(os.path.basename(p) for p in load_subset())
    if a.on == "subset":
        insts, seeds = load_subset(), TUNE_SEEDS
    else:
        insts = [p for p in INSTANCES if os.path.basename(p) not in sub and family(p) != 71]
        seeds = EVAL_SEEDS[:5]
    ref, ref_nd = subset_refs(insts)
    obj, obj_nd, teams = objective(cfg, insts, seeds, ref, ref_nd)
    print(json.dumps(dict(objective=obj, objective_nodispatch=obj_nd, teams=teams, n_instances=len(insts))))


def stage_noise(a):
    """Dispersión del objetivo entre lotes de semillas distintas, con la configuración fija.
    Sirve para comparar el ruido con las diferencias entre configuraciones."""
    with open(os.path.join(RES, "tuned_config.json")) as f:
        tuned = json.load(f)
    tuned = {k: tuned[k] for k in DEFAULT}
    subset = load_subset()
    ref, ref_nd = subset_refs(subset)
    batches = 8
    rows = []
    for name, cfg in (("default", DEFAULT), ("tuned", tuned)):
        for b in range(batches):
            seeds = [1000 + 5 * b + i for i in range(5)]
            obj, obj_nd, teams = objective(cfg, subset, seeds, ref, ref_nd)
            rows.append((name, b, obj, obj_nd, teams))
            print(f"[noise] {name} lote {b}: obj={obj:.5f} sin_despacho={obj_nd:.5f}", flush=True)
    with open(os.path.join(RES, "noise.csv"), "w", newline="") as f:
        w = csv.writer(f); w.writerow(["config", "batch", "objective", "objective_nodispatch", "teams"])
        w.writerows(rows)


STAGES = {"hardware": stage_hardware, "subset": stage_subset, "tune": stage_tune,
          "calibrate": stage_calibrate, "evaluate": stage_evaluate, "traces": stage_traces}
EXTRA = {"noise": stage_noise, "evalcfg": stage_evalcfg, "ablation": stage_ablation, "budget": stage_budget}

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("stage", choices=list(STAGES) + list(EXTRA) + ["all"])
    ap.add_argument("--quick", action="store_true", help="versión reducida para probar el flujo")
    ap.add_argument("--cfg", default="", help="evalcfg: alpha=1,beta=2,rho=.1,xi=.1,q0=.9,ants=10,ls_moves=all,ls_prob=1")
    ap.add_argument("--on", default="subset", choices=["subset", "rest"], help="evalcfg: instancias a usar")
    a = ap.parse_args()
    if not os.path.exists(BIN):
        sys.exit("falta el binario: ejecuta make en la raíz del proyecto")
    if a.quick:                       # no mezclar la prueba rápida con los resultados reales
        RES = os.path.join(EXP, "results_quick")
    os.makedirs(RES, exist_ok=True)
    for name in (list(STAGES) if a.stage == "all" else [a.stage]):
        if name != "evalcfg":
            print(f"=== {name}")
        {**STAGES, **EXTRA}[name](a)
