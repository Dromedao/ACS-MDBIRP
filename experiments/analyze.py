#!/usr/bin/env python3
"""Análisis de los experimentos: tablas, pruebas estadísticas y gráficas.

Lee experiments/results/ (lo que deja exp.py) y escribe en experiments/results/analysis/.
Requiere numpy, scipy, pandas y matplotlib.

Uso:  python experiments/analyze.py [--quick]
"""
import glob
import json
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from scipy import stats

EXP = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(EXP)
RES = os.path.join(EXP, "results_quick" if "--quick" in sys.argv else "results")
OUT = os.path.join(RES, "analysis")
os.makedirs(OUT, exist_ok=True)

VARIANTS = ["greedy", "acs_nols", "acs_ls", "acs_nols_matched"]
LABEL = {"greedy": "Greedy", "acs_nols": "ACS", "acs_ls": "ACS + LS",
         "acs_nols_matched": "ACS (equal compute)"}
COLOR = {"greedy": "#888888", "acs_nols": "#1f77b4", "acs_ls": "#d95f02", "acs_nols_matched": "#2ca02c"}


# datos
def load():
    d = pd.read_csv(os.path.join(RES, "eval_runs.csv"))
    d["family"] = d["instance"].str.extract(r"_b(\d+)_")[0].astype(int)
    d["nodisp"] = d["travel"] + d["lodging"]
    return d


def holm(p):
    """Corrección de Holm-Bonferroni; devuelve los p ajustados en el orden original."""
    p = np.asarray(p, float)
    order = np.argsort(p)
    adj = np.empty_like(p)
    m, run = len(p), 0.0
    for rank, i in enumerate(order):
        run = max(run, min(1.0, (m - rank) * p[i]))
        adj[i] = run
    return adj


def tex(df, path, caption, label, fmt=None):
    """Tabla LaTeX con booktabs escrita a mano (evita depender de jinja2)."""
    fmt = fmt or {}
    cols = list(df.columns)
    lines = ["\\begin{table}[t]", "\\centering", "\\small",
             "\\begin{tabular}{" + "l" + "r" * (len(cols) - 1) + "}", "\\toprule",
             " & ".join(c.replace("_", "\\_") for c in cols) + " \\\\", "\\midrule"]
    for _, r in df.iterrows():
        cells = []
        for c in cols:
            v = r[c]
            cells.append(fmt[c].format(v) if c in fmt and pd.notna(v) else str(v))
        lines.append(" & ".join(cells) + " \\\\")
    lines += ["\\bottomrule", "\\end{tabular}", f"\\caption{{{caption}}}", f"\\label{{{label}}}", "\\end{table}"]
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


# tablas
def per_instance(d):
    """Una fila por instancia y variante: mejor y promedio entre semillas."""
    g = d.groupby(["instance", "family", "variant"])
    t = g.agg(best=("final_cost", "min"), mean=("final_cost", "mean"), teams=("teams", "mean"),
              nights=("nights", "mean"), nodisp=("nodisp", "mean"), seconds=("seconds", "mean"),
              runs=("seed", "count"), lb=("lb_teams", "first")).reset_index()
    greedy = t[t.variant == "greedy"].set_index("instance")
    t["rel"] = t.apply(lambda r: r["mean"] / greedy.loc[r["instance"], "mean"], axis=1)
    t["rel_nodisp"] = t.apply(lambda r: r["nodisp"] / greedy.loc[r["instance"], "nodisp"], axis=1)
    return t


def tables(d, t):
    wide = t.pivot(index=["instance", "family", "lb"], columns="variant", values=["best", "mean", "teams", "nights", "nodisp"])
    wide.columns = [f"{a}_{b}" for a, b in wide.columns]
    wide.reset_index().to_csv(os.path.join(OUT, "table_instances.csv"), index=False)

    fam = []
    for f, sub in t.groupby("family"):
        row = {"puentes": f, "n": sub.instance.nunique()}
        for v in VARIANTS[1:]:
            s = sub[sub.variant == v]
            row[f"{v}_rel"] = s.rel.mean()
            row[f"{v}_equipos"] = s.teams.mean()
        row["greedy_equipos"] = sub[sub.variant == "greedy"].teams.mean()
        row["cota_equipos"] = sub[sub.variant == "greedy"].lb.mean()
        fam.append(row)
    fam = pd.DataFrame(fam)
    fam.to_csv(os.path.join(OUT, "table_family.csv"), index=False)

    main = fam[["puentes", "n", "greedy_equipos", "acs_nols_equipos", "cota_equipos",
                "acs_nols_rel", "acs_ls_rel", "acs_nols_matched_rel"]].copy()
    main.columns = ["bridges", "n", "teams_greedy", "teams_ACS", "teams_LB", "rel_ACS", "rel_ACS+LS", "rel_ACS_matched"]
    tex(main, os.path.join(OUT, "table_family.tex"),
        "Per family: mean teams and mean cost relative to the greedy solution (lower is better).",
        "tab:family", {c: "{:.3f}" for c in main.columns[2:]})

    # composición del costo de la solución greedy y de la del ACS
    comp = []
    for v in ("greedy", "acs_nols"):
        s = d[d.variant == v].groupby("family")[["dispatch", "lodging", "travel", "nights", "teams"]].mean()
        tot = s.dispatch + s.lodging + s.travel
        c = pd.DataFrame({"variant": v, "dispatch_pct": 100 * s.dispatch / tot, "lodging_pct": 100 * s.lodging / tot,
                          "travel_pct": 100 * s.travel / tot, "nights": s.nights, "teams": s.teams}).reset_index()
        comp.append(c)
    comp = pd.concat(comp)
    comp.to_csv(os.path.join(OUT, "table_composition.csv"), index=False)

    # flota frente a la cota inferior
    g = t[t.variant == "acs_nols"]
    gg = t[t.variant == "greedy"].set_index("instance")
    fleet = pd.DataFrame({
        "family": g.family.values, "instance": g.instance.values, "lb": g.lb.values,
        "teams_acs": g.teams.values, "teams_greedy": gg.loc[g.instance, "teams"].values})
    fleet["acs_at_lb"] = fleet.teams_acs <= fleet.lb + 1e-9
    fleet["acs_below_greedy"] = fleet.teams_acs < fleet.teams_greedy - 1e-9
    fleet["acs_below_lb"] = fleet.teams_acs < fleet.lb - 1e-9
    fs = fleet.groupby("family").agg(n=("instance", "count"), at_lb=("acs_at_lb", "sum"),
                                     below_greedy=("acs_below_greedy", "sum"), below_lb=("acs_below_lb", "sum"))
    fs.to_csv(os.path.join(OUT, "table_fleet.csv"))
    return fam, comp, fs


# pruebas
# Cada comparación es "A -> B"; el efecto es (B - A) / A en %, negativo = B más barato.
COMPARISONS = [("greedy", "acs_nols", "greedy -> ACS (descriptive)"),
               ("acs_nols", "acs_ls", "ACS -> ACS+LS (same iterations)"),
               ("acs_nols_matched", "acs_ls", "ACS equal compute -> ACS+LS"),
               ("acs_nols", "acs_nols_matched", "ACS -> ACS with more iterations (x k)")]


def wilcoxon_row(x, y):
    """Wilcoxon de rangos con signo sobre y - x. Se descartan las diferencias nulas (|d| < 1e-9);
    scipy.stats.wilcoxon con method='auto': exacto si n <= 50 y no hay empates, aproximación normal en
    otro caso. Efecto: correlación rango-biserial (W+ - W-)/(W+ + W-)."""
    diff = y - x
    nz = diff[np.abs(diff) > 1e-9]
    if len(nz) < 3:
        return 1.0, 0.0
    ranks = stats.rankdata(np.abs(nz))
    wp, wm = ranks[nz > 0].sum(), ranks[nz < 0].sum()
    return float(stats.wilcoxon(nz).pvalue), float((wp - wm) / (wp + wm))


def tests(t, instances=None, tag="all"):
    """Pruebas pareadas por instancia (media entre semillas). Holm por métrica sobre todas las
    comparaciones y grupos (familias + global) de este subconjunto de instancias."""
    rows = []
    if instances is not None:
        t = t[t.instance.isin(instances)]
    fams = sorted(t.family.unique())
    for metric, col in (("total cost", "mean"), ("cost without dispatch", "nodisp")):
        piv = t.pivot(index=["instance", "family"], columns="variant", values=col).reset_index()
        for a, b, name in COMPARISONS:
            for group in fams + ["global"]:
                sub = piv if group == "global" else piv[piv.family == group]
                if len(sub) < (3 if "--quick" in sys.argv else 6):
                    continue
                x, y = sub[a].values, sub[b].values
                p, rb = wilcoxon_row(x, y)
                rows.append(dict(subset=tag, metric=metric, comparison=name, group=group, n=len(sub),
                                 median_rel_diff_pct=float(np.median((y - x) / x) * 100),
                                 mean_rel_diff_pct=float(np.mean((y - x) / x) * 100),
                                 rank_biserial=rb, p_raw=p))
    r = pd.DataFrame(rows)
    r["p_holm"] = 1.0
    for metric in r.metric.unique():
        m = r.metric == metric
        r.loc[m, "p_holm"] = holm(r.loc[m, "p_raw"].values)
    r["significant"] = r.p_holm < 0.05
    return r


def all_tests(t):
    """Todas las instancias y, aparte, las que no se usaron para sintonizar (sin contaminación)."""
    sub_file = os.path.join(EXP, "subset_tuning.txt")
    tuned = set(os.path.basename(l.strip()) for l in open(sub_file)) if os.path.exists(sub_file) else set()
    hold = [i for i in t.instance.unique() if i not in tuned]
    r = pd.concat([tests(t, None, "all"), tests(t, hold, "holdout")] if tuned else [tests(t, None, "all")])
    r.to_csv(os.path.join(OUT, "tests.csv"), index=False)
    return r


# gráficas
def fig_boxplots(d, t):
    """Dos figuras por métrica.
    (a) distribución entre instancias: cada caja resume las instancias de la familia; cada dato es la
        media de las 20 semillas de una instancia, relativa al greedy de esa instancia (b71 = 1 punto).
    (b) distribución entre semillas: una instancia representativa por familia (la primera, la misma
        de las trazas); cada dato es una de las 20 semillas."""
    fams = sorted(d.family.unique())
    vs = VARIANTS[1:]
    w = 0.8 / len(vs)

    def draw(ax, getdata, ylabel):
        for k, v in enumerate(vs):
            data = [getdata(v, f) for f in fams]
            pos = np.arange(len(fams)) + (k - (len(vs) - 1) / 2) * w
            ok = [i for i, x in enumerate(data) if len(x) > 1]
            if ok:
                bp = ax.boxplot([data[i] for i in ok], positions=pos[ok], widths=w * 0.9, patch_artist=True,
                                showfliers=False, medianprops=dict(color="black"))
                for b in bp["boxes"]:
                    b.set_facecolor(COLOR[v]); b.set_alpha(0.75)
            for i, x in enumerate(data):
                if len(x) == 1:
                    ax.plot(pos[i], x[0], "D", color=COLOR[v], ms=5)
            ax.plot([], [], "s", color=COLOR[v], label=LABEL[v], ms=9)
        ax.set_xticks(range(len(fams))); ax.set_xticklabels([f"b{f}" for f in fams])
        ax.set_ylabel(ylabel); ax.set_xlabel("family (number of bridges)"); ax.grid(axis="y", alpha=0.3)
        ax.legend(ncol=len(vs), loc="upper center", bbox_to_anchor=(0.5, -0.2), frameon=False)

    fam_of = t.drop_duplicates("instance").set_index("instance").family
    for col, tag, lab in (("final_cost", "total", "total cost / greedy cost"),
                          ("nodisp", "nodispatch", "(travel + lodging) / greedy")):
        gcol = "mean" if col == "final_cost" else "nodisp"
        mean = t.pivot(index="instance", columns="variant", values=gcol)
        fig, ax = plt.subplots(figsize=(11, 4.6))
        draw(ax, lambda v, f: (mean[v] / mean["greedy"])[fam_of[mean.index].values == f].values,
             lab + " (per-instance mean)")
        fig.tight_layout(); fig.savefig(os.path.join(OUT, f"fig_boxplot_{tag}.png"), dpi=160); plt.close(fig)

        rep = {f: sorted(d[d.family == f].instance.unique())[0] for f in fams}

        def seeds(v, f):
            s = d[(d.variant == v) & (d.instance == rep[f])]
            g = d[(d.variant == "greedy") & (d.instance == rep[f])][col].mean()
            return (s[col] / g).values
        fig, ax = plt.subplots(figsize=(11, 4.6))
        draw(ax, seeds, lab + " (20 seeds)")
        fig.tight_layout(); fig.savefig(os.path.join(OUT, f"fig_boxplot_seeds_{tag}.png"), dpi=160); plt.close(fig)


def fig_composition(comp):
    c = comp[comp.variant == "greedy"].sort_values("family")
    fig, axes = plt.subplots(1, 2, figsize=(11, 3.9), gridspec_kw={"width_ratios": [2, 1]})
    x = np.arange(len(c))
    ax = axes[0]
    ax.bar(x, c.dispatch_pct, label="dispatch", color="#1f77b4")
    ax.bar(x, c.lodging_pct, bottom=c.dispatch_pct, label="lodging", color="#d95f02")
    ax.bar(x, c.travel_pct, bottom=c.dispatch_pct + c.lodging_pct, label="travel (<0.1 %, not visible)", color="#2ca02c")
    for i, (dp, lp) in enumerate(zip(c.dispatch_pct, c.lodging_pct)):
        ax.text(i, dp / 2, f"{dp:.1f}", ha="center", va="center", color="white", fontsize=8)
        ax.text(i, 101, f"{lp:.1f}", ha="center", va="bottom", color="#d95f02", fontsize=8)
    ax.set_xticks(x); ax.set_xticklabels([f"b{f}" for f in c.family]); ax.set_ylabel("% of total cost")
    ax.set_ylim(0, 108); ax.set_title("Cost composition, greedy solution (axis from 0; lodging % on top)", fontsize=9)
    ax.legend(loc="upper center", bbox_to_anchor=(0.5, -0.12), ncol=3, frameon=False)
    ax = axes[1]
    ax.bar(x, c.travel_pct, color="#2ca02c")
    ax.set_xticks(x); ax.set_xticklabels([f"b{f}" for f in c.family]); ax.set_ylabel("% of total cost")
    ax.set_title("Travel share only", fontsize=9)
    fig.tight_layout(); fig.savefig(os.path.join(OUT, "fig_composition.png"), dpi=160); plt.close(fig)


def fig_fleet(fam):
    fig, ax = plt.subplots(figsize=(8, 4.2))
    x = np.arange(len(fam)); w = 0.27
    ax.bar(x - w, fam.greedy_equipos, w, label="greedy", color=COLOR["greedy"])
    ax.bar(x, fam.acs_nols_equipos, w, label="ACS", color=COLOR["acs_nols"])
    ax.bar(x + w, fam.cota_equipos, w, label="lower bound (MST)", color="#7b3294")
    ax.set_xticks(x); ax.set_xticklabels([f"b{p}" for p in fam.puentes]); ax.set_ylabel("teams (mean per family)")
    ax.grid(axis="y", alpha=0.3)
    ax.legend(ncol=3, loc="upper center", bbox_to_anchor=(0.5, -0.12), frameon=False)
    fig.tight_layout(); fig.savefig(os.path.join(OUT, "fig_fleet.png"), dpi=160); plt.close(fig)


def fig_convergence():
    """Cada panel tiene su propia escala vertical (las mejoras van de 0,1 % a 40 %); el rango se
    indica en el título de cada panel."""
    files = glob.glob(os.path.join(RES, "traces", "*.csv"))
    if not files:
        return
    tr = {}
    for f in files:
        v, base, seed = os.path.basename(f)[:-4].split("__")
        tr.setdefault(base, {}).setdefault(v, []).append(pd.read_csv(f))
    bases = sorted(tr, key=lambda b: int(b.split("_")[1][1:]))
    n = len(bases); cols = 3; rows = int(np.ceil(n / cols))
    fig, axes = plt.subplots(rows, cols, figsize=(11, 3.0 * rows + 0.4), squeeze=False)
    for ax, base in zip(axes.ravel(), bases):
        lo = 1.0
        for v in ("acs_nols", "acs_ls"):
            runs = tr[base].get(v, [])
            if not runs:
                continue
            m = min(len(r) for r in runs)
            arr = np.array([r.best_cost.values[:m] / r.best_cost.values[0] for r in runs])
            it = runs[0].iter.values[:m]
            ax.plot(it, arr.mean(0), color=COLOR[v], label=LABEL[v])
            ax.fill_between(it, arr.min(0), arr.max(0), color=COLOR[v], alpha=0.15)
            lo = min(lo, float(arr.min()))
        ax.set_title(f"{base.replace('Instance_', '')}  (y axis: {lo:.3f} to 1)", fontsize=9); ax.grid(alpha=0.3)
    for ax in axes.ravel()[n:]:
        ax.axis("off")
    h, l = axes[0][0].get_legend_handles_labels()
    fig.legend(h, l, loc="lower center", ncol=2, frameon=False)
    fig.supxlabel("iteration", y=0.05); fig.supylabel("best cost / initial (greedy) cost")
    fig.tight_layout(rect=(0, 0.07, 1, 1)); fig.savefig(os.path.join(OUT, "fig_convergence.png"), dpi=160); plt.close(fig)


def read_coords(path):
    """Lee tipos y coordenadas del .dat (sets O, N, M y la tabla xcoord ycoord)."""
    sets, coords, mode = {}, {}, False
    with open(path, encoding="utf-8", errors="ignore") as f:
        for line in f:
            s = line.strip()
            if s.startswith("set") and ":=" in s:
                name = s.split()[1].rstrip(":=").rstrip(":")
                sets[name] = [int(x) for x in s.split(":=")[1].replace(";", "").split()]
            elif s.startswith("param:") and "xcoord" in s:
                mode = True
            elif mode:
                if s.startswith(";"):
                    break
                p = s.split()
                if len(p) >= 3:
                    coords[int(p[0])] = (float(p[2]), float(p[1]))     # (lon, lat)
    return sets, coords


def fig_solutions():
    sdir = os.path.join(RES, "solutions")
    greedy = sorted(glob.glob(os.path.join(sdir, "greedy__*.csv")))
    if not greedy:
        return
    bases = [os.path.basename(g)[8:-4] for g in greedy]
    pick = [b for b in bases if b.split("_")[1] in ("b30", "b71")] or bases[:2]
    fig, axes = plt.subplots(len(pick), 2, figsize=(10, 4.4 * len(pick)), squeeze=False)
    cmap = plt.get_cmap("tab10")
    for r, base in enumerate(pick):
        sets, co = read_coords(os.path.join(ROOT, "instances", base + ".dat"))
        for c, v in enumerate(("greedy", "acs_ls")):
            ax = axes[r][c]
            f = os.path.join(sdir, f"{v}__{base}.csv")
            if not os.path.exists(f):
                ax.axis("off"); continue
            sol = pd.read_csv(f, dtype=str, keep_default_na=False)
            for team, grp in sol.groupby("team", sort=True):
                for _, row in grp.iterrows():
                    seq = [int(row.depot)] if int(row.day) == 1 else [prev]
                    seq += [int(b) for b in row.bridges.split()]
                    seq.append(int(row.lodging) if int(row.lodging) != -1 else int(row.depot))
                    prev = int(row.lodging)
                    xs, ys = zip(*[co[n] for n in seq])
                    ax.plot(xs, ys, "-", color=cmap(int(team) % 10), lw=1.2,
                            alpha=0.45 + 0.5 * (int(row.day) % 2))
            for kind, mk, col, sz in (("N", "o", "k", 14), ("M", "s", "#d95f02", 28), ("O", "*", "#1f77b4", 90)):
                pts = [co[i] for i in sets.get(kind, []) if i in co]
                if pts:
                    ax.scatter(*zip(*pts), marker=mk, c=col, s=sz, zorder=3)
            nteams = sol.team.nunique()
            ax.set_title(f"{base.replace('Instance_', '')} - {LABEL[v]} ({nteams} team{'s' if nteams != 1 else ''})", fontsize=9)
            ax.set_xticks([]); ax.set_yticks([])
    from matplotlib.lines import Line2D
    hs = [Line2D([], [], marker="*", color="#1f77b4", ls="", ms=11, label="depot"),
          Line2D([], [], marker="s", color="#d95f02", ls="", label="lodging"),
          Line2D([], [], marker="o", color="k", ls="", label="bridge"),
          Line2D([], [], color=cmap(0), lw=2, label="team 0 (hue = team)"),
          Line2D([], [], color=cmap(1), lw=2, label="team 1"),
          Line2D([], [], color="gray", lw=1.2, alpha=0.95, label="odd day (dark)"),
          Line2D([], [], color="gray", lw=1.2, alpha=0.45, label="even day (light)")]
    fig.legend(handles=hs, loc="lower center", ncol=7, frameon=False, fontsize=8)
    fig.tight_layout(rect=(0, 0.04, 1, 1)); fig.savefig(os.path.join(OUT, "fig_solutions.png"), dpi=160); plt.close(fig)


def fig_tuning():
    p = os.path.join(RES, "tuning.csv")
    if not os.path.exists(p):
        return
    tn = pd.read_csv(p)
    fig, axes = plt.subplots(1, 2, figsize=(10, 3.6))
    for ax, col, ttl in zip(axes, ("objective", "objective_nodispatch"), ("total cost / greedy", "(travel + lodging) / greedy")):
        if col not in tn:
            ax.axis("off"); continue
        for ph, mk in (("default", "*"), ("random", "o"), ("refine", "^")):
            s = tn[tn.phase == ph]
            ax.scatter(s.index, s[col], marker=mk, label=ph, s=60 if ph == "default" else 24)
        ax.set_title(ttl, fontsize=10); ax.set_xlabel("configuration evaluated"); ax.grid(alpha=0.3)
    axes[0].legend(fontsize=8)
    fig.tight_layout(); fig.savefig(os.path.join(OUT, "fig_tuning.png"), dpi=160); plt.close(fig)


def tuning_summary():
    """Compara la configuración por defecto, la del sintonizador propio y la de Optuna:
    en el subset de sintonización (semillas 1-5), en lotes de semillas nuevas sobre el mismo
    subset y en las instancias que no se usaron para sintonizar."""
    need = [os.path.join(RES, f) for f in ("tuning.csv", "noise.csv")]
    if not all(os.path.exists(p) for p in need):
        return None
    tn, nz = pd.read_csv(need[0]), pd.read_csv(need[1])
    rows = []
    d0 = tn[tn.phase == "default"].iloc[0]
    best = tn.sort_values("objective").iloc[0]
    ob = {}
    if os.path.exists(os.path.join(RES, "optuna_best.json")):
        ob = json.load(open(os.path.join(RES, "optuna_best.json")))
    oos = {}
    if os.path.exists(os.path.join(RES, "oos.txt")):
        for line in open(os.path.join(RES, "oos.txt")):
            name, js = line.strip().split(" ", 1)
            oos[name] = json.loads(js)
    for name, key, ins, noise in (("default", "default", d0, "default"), ("propio", "propio", best, "tuned"),
                                  ("optuna", "optuna", None, None)):
        r = {"config": name}
        if ins is not None:
            r["subset_obj"] = ins.objective; r["subset_obj_sin_despacho"] = ins.objective_nodispatch
        elif ob:
            r["subset_obj"] = ob["objective"]; r["subset_obj_sin_despacho"] = ob["objective_nodispatch"]
        if noise:
            s = nz[nz.config == noise]
            r["lotes_nuevos_obj"] = s.objective.mean(); r["lotes_nuevos_sd"] = s.objective.std()
        if key in oos:
            r["fuera_obj"] = oos[key]["objective"]; r["fuera_obj_sin_despacho"] = oos[key]["objective_nodispatch"]
        rows.append(r)
    out = pd.DataFrame(rows)
    out.to_csv(os.path.join(OUT, "table_tuning.csv"), index=False)
    spread = {"sd_entre_configuraciones": float(tn.objective.std()),
              "rango_entre_configuraciones": float(tn.objective.max() - tn.objective.min()),
              "sd_ruido_semillas_default": float(nz[nz.config == "default"].objective.std()),
              "sd_ruido_semillas_propio": float(nz[nz.config == "tuned"].objective.std())}
    return out, spread


def ablation():
    """Operadores de búsqueda local uno a uno (none, 2opt, relocate, all) con la configuración
    sintonizada. Prueba de Wilcoxon pareada contra 'none' sobre la media por instancia, con Holm."""
    p = os.path.join(RES, "ablation_runs.csv")
    if not os.path.exists(p):
        return None
    a = pd.read_csv(p)
    a["nodisp"] = a.travel + a.lodging
    rows = []
    for col, name in (("final_cost", "costo total"), ("nodisp", "costo sin despacho")):
        m = a.groupby(["instance", "ls_moves"])[col].mean().unstack()
        for op in ("2opt", "relocate", "all"):
            diff = m[op] - m["none"]
            pv = 1.0 if np.allclose(diff, 0) else stats.wilcoxon(diff).pvalue
            rows.append(dict(metric=name, op=op, median_rel_diff_pct=float(np.median(diff / m["none"]) * 100),
                             mean_rel_diff_pct=float(np.mean(diff / m["none"]) * 100), p_raw=pv,
                             seconds_mean=float(a[a.ls_moves == op].seconds.mean()),
                             seconds_none=float(a[a.ls_moves == "none"].seconds.mean())))
    r = pd.DataFrame(rows)
    r["p_holm"] = holm(r.p_raw.values)
    r.to_csv(os.path.join(OUT, "table_ablation.csv"), index=False)
    return r


def exact_gap(d):
    """Brecha del ACS respecto del óptimo exacto (experiments/exact.cpp) en b10 y b15."""
    p = os.path.join(RES, "exact.csv")
    if not os.path.exists(p):
        return None
    ex = pd.read_csv(p).set_index("instance")
    x = d[d.instance.isin(ex.index)].copy()
    x["opt"] = x.instance.map(ex.opt_cost)
    x["gap"] = 100 * (x.final_cost - x.opt) / x.opt
    below = int((x.final_cost < x.opt - 0.5).sum())
    rows = []
    for (fam, v), g in x.groupby(["family", "variant"]):
        per = g.groupby("instance").agg(mean_gap=("gap", "mean"), best_gap=("gap", "min"))
        rows.append(dict(family=fam, variant=LABEL[v], n_instances=len(per),
                         mean_gap_pct=per.mean_gap.mean(), best_gap_pct=per.best_gap.mean(),
                         seeds_at_optimum_pct=100 * float((g.gap.abs() < 1e-4).mean()),
                         instances_with_optimum_in_20_seeds=int((per.best_gap.abs() < 1e-4).sum()),
                         max_gap_pct=float(g.gap.max())))
    r = pd.DataFrame(rows)
    r.to_csv(os.path.join(OUT, "table_exact_gap.csv"), index=False)
    r.attrs["below_optimum"] = below
    return r


# principal
def main():
    d = load()
    bad = int((d.valid != 1).sum())
    t = per_instance(d)
    fam, comp, fleet = tables(d, t)
    r = all_tests(t)
    fig_boxplots(d, t); fig_composition(comp); fig_fleet(fam)
    fig_convergence(); fig_solutions(); fig_tuning()
    eg = exact_gap(d)
    if eg is not None:
        print("\n== gap vs exact optimum (runs below optimum: %d)" % eg.attrs["below_optimum"])
        print(eg.round(4).to_string(index=False))
    ab = ablation()
    if ab is not None:
        print("\n== ablación de operadores (contra none)")
        print(ab.round(5).to_string(index=False))
    ts = tuning_summary()
    if ts:
        print("\n== sintonización: por defecto vs propio vs Optuna")
        print(ts[0].round(5).to_string(index=False))
        print({k: round(v, 5) for k, v in ts[1].items()})

    cfg = {}
    cp = os.path.join(RES, "tuned_config.json")
    if os.path.exists(cp):
        cfg = json.load(open(cp))
    s = {"corridas": int(len(d)), "invalidas": bad, "instancias": int(d.instance.nunique()),
         "semillas": sorted(map(int, d.seed.unique()))[:3] + ["..."] if d.seed.nunique() > 3 else sorted(map(int, d.seed.unique())),
         "config": cfg,
         "equipos_acs_en_cota": int(fleet.at_lb.sum()), "equipos_acs_menos_que_greedy": int(fleet.below_greedy.sum()),
         "equipos_acs_bajo_cota": int(fleet.below_lb.sum())}
    with open(os.path.join(OUT, "summary.json"), "w", encoding="utf-8") as f:
        json.dump(s, f, indent=2, ensure_ascii=False, default=str)
    print(json.dumps(s, indent=2, ensure_ascii=False, default=str))
    print("\n== pruebas globales")
    print(r[r.group == "global"][["subset", "metric", "comparison", "n", "median_rel_diff_pct", "rank_biserial", "p_holm", "significant"]]
          .to_string(index=False))
    print(f"\ntablas y figuras en {OUT}")


if __name__ == "__main__":
    main()
