#!/usr/bin/env python3
"""Sintonización con Optuna (muestreador TPE) sobre el mismo subset, semillas y objetivo que el
sintonizador propio de exp.py, con el mismo número de evaluaciones de configuración.

Optuna corre en Windows; cada evaluación llama a WSL (exp.py evalcfg), que ejecuta el binario
en paralelo. Escribe results/optuna_trials.csv y results/optuna_best.json.

Uso:  python experiments/optuna_tune.py [n_trials]
"""
import json
import os
import subprocess
import sys

import optuna

EXP = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(EXP, "results")
WSL_EXP = "/mnt/c/" + EXP.replace("\\", "/")[3:] + "/exp.py"
N_TRIALS = int(sys.argv[1]) if len(sys.argv) > 1 else 81      # 1 por defecto + 60 + 20 del propio


def cfg_text(c):
    return ",".join(f"{k}={v}" for k, v in c.items())


def evaluate(cfg, on="subset"):
    p = subprocess.run(["wsl", "-d", "Ubuntu", "--", "python3", WSL_EXP, "evalcfg", "--on", on,
                        "--cfg", cfg_text(cfg)], capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError(p.stderr)
    return json.loads(p.stdout.strip().splitlines()[-1])


def objective(trial):
    cfg = dict(alpha=round(trial.suggest_float("alpha", 0.5, 2.0), 2),
               beta=round(trial.suggest_float("beta", 1.0, 5.0), 2),
               rho=round(trial.suggest_float("rho", 0.05, 0.5), 2),
               xi=round(trial.suggest_float("xi", 0.05, 0.3), 2),
               q0=round(trial.suggest_float("q0", 0.5, 0.98), 2),
               ants=trial.suggest_categorical("ants", [5, 10, 20, 40]),
               ls_moves=trial.suggest_categorical("ls_moves", ["none", "2opt", "relocate", "all"]),
               ls_prob=trial.suggest_categorical("ls_prob", [0.25, 0.5, 1.0]))
    r = evaluate(cfg)
    trial.set_user_attr("objective_nodispatch", r["objective_nodispatch"])
    trial.set_user_attr("teams", r["teams"])
    print(f"[optuna] trial {trial.number}: obj={r['objective']:.5f} {cfg}", flush=True)
    return r["objective"]


if __name__ == "__main__":
    optuna.logging.set_verbosity(optuna.logging.WARNING)
    study = optuna.create_study(direction="minimize", sampler=optuna.samplers.TPESampler(seed=7))
    study.optimize(objective, n_trials=N_TRIALS)
    df = study.trials_dataframe()
    df.to_csv(os.path.join(RES, "optuna_trials.csv"), index=False)
    best = study.best_trial
    out = dict(best.params, objective=best.value, n_trials=N_TRIALS,
               objective_nodispatch=best.user_attrs.get("objective_nodispatch"))
    for k in ("alpha", "beta", "rho", "xi", "q0"):
        out[k] = round(out[k], 2)
    with open(os.path.join(RES, "optuna_best.json"), "w") as f:
        json.dump(out, f, indent=2)
    print("mejor (Optuna):", out)
