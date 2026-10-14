"""Gera todos os gráficos de uma pasta criada por run_experiments.sh.

Para cada pasta de experimentos grava, ao lado dos CSVs:
    experimento<i>.png  convergência de cada execução (plot.py)
    media.png           média da melhor solução até a iteração, com faixa de
                        +- desvio amostral
    completo.png        todas as execuções e a média numa figura só

uso: python3 scripts/plot_all.py <pasta> [<pasta> ...]
"""
import argparse
import os
import sys
import numpy as np
import matplotlib
import matplotlib.pyplot as plt

from concurrent.futures import ProcessPoolExecutor
from pathlib import Path
from plot import (draw_run, 
                  load_result, 
                  load_trace, 
                  numero_execucao, 
                  plotar, 
                  run_label, 
                  setup_line,
                  title_lines)

matplotlib.use("Agg")

GREEN = "#2e8b57"

def scenario_dirs(paths):
    dirs = set()
    for p in map(Path, paths):
        dirs.update(t.parent for t in p.rglob("experimento*trace.csv"))
    return sorted(dirs)


def runs_of(scenario):
    """Pares (trace, resultado) das execuções do cenário, em ordem numérica."""
    runs = []
    for trace in scenario.glob("experimento*trace.csv"):
        result = trace.with_name(trace.name.replace("trace.csv", ".csv"))
        if result.exists():
            runs.append((trace, result))
    return sorted(runs, key=lambda par: numero_execucao(par[0]))


def load_scenario(scenario):
    """(traces, resultados) de todas as execuções do cenário."""
    runs = runs_of(scenario)
    traces = [load_trace(t) for t, _ in runs]
    results = [load_result(r) for _, r in runs]
    if len({len(t[0]) for t in traces}) != 1:
        raise SystemExit(f"erro: traces de tamanhos diferentes em {scenario}")
    return traces, results


def draw_mean(ax, traces, results, legend=True):
    """Média da melhor solução até a iteração ± desvio amostral."""
    m = int(results[0]["n_clausulas"])
    evals = traces[0][0]
    best = np.array([m - t[2] for t in traces])
    mean = best.mean(axis=0)
    std = best.std(axis=0, ddof=1) if len(best) > 1 else np.zeros_like(mean)

    ax.fill_between(evals, mean - std, np.minimum(mean + std, m), color=GREEN, alpha=0.2, linewidth=0,
                    label="Média ± desvio amostral")
    ax.plot(evals, mean, color=GREEN, linewidth=2, drawstyle="steps-post",
            label="Média da melhor solução até a iteração")
    ax.grid(alpha=0.3)
    if legend:
        reached = sum(int(r["f_melhor"]) == 0 for r in results)
        ax.legend(loc="lower right", title_fontsize=9, title=f"final: {reached}/{len(results)} atingiram a meta")


def plot_mean(scenario, out_path):
    traces, results = load_scenario(scenario)
    fig, ax = plt.subplots(figsize=(10, 5.5))
    draw_mean(ax, traces, results)
    ax.set_title(title_lines(results[0], suffix=f" | {len(results)} execuções", seed=False))
    ax.set_xlabel("Iterações")
    ax.set_ylabel("Cláusulas satisfeitas")
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)


def plot_complete(scenario, out_path, cols=4):
    """Grade com uma execução por painel e a média ocupando 2 células no fim."""
    traces, results = load_scenario(scenario)
    m = int(results[0]["n_clausulas"])
    n = len(results)
    start = n if n % cols != cols - 1 else n + 1  # a média precisa de 2 células na mesma linha
    rows = (start + 2 + cols - 1) // cols

    fig = plt.figure(figsize=(5 * cols, 3.6 * rows + 1))
    grid = fig.add_gridspec(rows, cols)
    # mesma escala vertical em todas as execuções, para comparar
    low = min(float(np.min(m - t[1])) for t in traces)
    pad = 0.03 * (m - low)

    numeros = [numero_execucao(t) for t, _ in runs_of(scenario)]  # mesma ordem de load_scenario
    for k, (trace, r) in enumerate(zip(traces, results)):
        ax = fig.add_subplot(grid[k // cols, k % cols])
        draw_run(ax, trace, r, linewidth=0.4, legend_size=8)
        ax.set_title(run_label(r, numeros[k]), fontsize=10)
        ax.set_ylim(low - pad, m + pad)
        ax.tick_params(labelsize=8)
        if k % cols == 0:
            ax.set_ylabel("Cláusulas satisfeitas", fontsize=9)
        if k // cols == rows - 1 or k + cols >= start:
            ax.set_xlabel("Iterações", fontsize=9)

    ax = fig.add_subplot(grid[start // cols, start % cols:start % cols + 2])
    draw_mean(ax, traces, results)
    ax.set_title(f"Média das {n} execuções", fontsize=11, fontweight="bold")
    ax.set_xlabel("Iterações", fontsize=9)
    ax.tick_params(labelsize=8)

    fig.suptitle(title_lines(results[0], suffix=f" | {n} execuções", seed=False) + "\n" + setup_line(results[0]),
                 fontsize=14)
    fig.tight_layout(rect=(0, 0, 1, 0.96))
    fig.savefig(out_path, dpi=110)
    plt.close(fig)


def job(task):
    kind, args = task
    {"run": plotar, "mean": plot_mean, "complete": plot_complete}[kind](*args)
    return args[-1]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+", help="pasta(s) gerada(s) pelo run_experiments.sh")
    a = ap.parse_args()

    tasks = []
    for scenario in scenario_dirs(a.paths):
        runs = runs_of(scenario)
        for trace, result in runs:
            tasks.append(("run", (trace, result, scenario / f"experimento{numero_execucao(trace)}.png")))
        if len(runs) > 1:  # com 1 execução só, basta o gráfico dela
            tasks.append(("mean", (scenario, scenario / "media.png")))
            tasks.append(("complete", (scenario, scenario / "completo.png")))
    if not tasks:
        sys.exit("erro: nenhum experimento*trace.csv (com o experimento*.csv correspondente) encontrado")

    with ProcessPoolExecutor(max_workers=os.cpu_count()) as pool:
        list(pool.map(job, tasks))
    counts = {k: sum(1 for kind, _ in tasks if kind == k) for k in ("run", "mean", "complete")}
    print(f"gráficos: {counts['run']} execuções + {counts['mean']} médias + {counts['complete']} completos")


if __name__ == "__main__":
    main()
