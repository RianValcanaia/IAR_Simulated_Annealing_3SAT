"""Plota o trace de uma execução do SA-3SAT.

uso: python3 scripts/plot.py <trace.csv> <resultado.csv> <saida.png> [--temperature]
"""
import argparse
import csv
import re
import matplotlib
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path

matplotlib.use("Agg")

# ====== Carrega dados ======
def load_result(result_path):
    """Carrega o resultado de uma execução do SA-3SAT."""
    with open(result_path, newline="") as f:
        return next(csv.DictReader(f))

def load_trace(trace_path):
    """Carrega o traço de uma execução do SA-3SAT."""
    data = np.loadtxt(trace_path, delimiter=",", skiprows=1, ndmin=2)
    return data[:, 0], data[:, 1], data[:, 2], data[:, 3]

def title_lines(r, suffix="", seed=True):
    """As duas linhas do título, a partir do CSV de resultado (seed=False omite a seed)."""
    first = f"SA: {Path(r['instancia']).name} - {r['n_variaveis']} variáveis, {r['n_clausulas']} cláusulas{suffix}"

    parts = [f"Cooling Schedule: {r['cooling_num']}",
             f"T0={float(r['t0']):g}", f"TN={float(r['tn']):g}",
             f"SAmax={r['samax']}"]
    if seed:
        parts.append(f"seed={r['seed']}")
    return first + "\n" + ", ".join(parts)

def setup_line(r):
    """"Iterações: <n> | Bit-flip: <%> (<k> de <n> bits)"."""
    k, n = int(r["bit"]), int(r["n_variaveis"])
    return f"Iterações: {int(r['max_aval'])} | Bit-flip: {100 * k / n:.3g}% ({k} de {n} bits)"


def numero_execucao(caminho):
    """Nº da execução a partir do nome: experimento7.csv ou experimento7trace.csv -> 7.
    Devolve None se o nome não tem esse padrão (execução avulsa)."""
    achou = re.search(r"experimento(\d+)", Path(caminho).name)
    return int(achou.group(1)) if achou else None


def run_label(r, numero):
    """"Execução <nº> - <satisfeitas>/<total>" (sem o nº, só "<satisfeitas>/<total>")."""
    m = int(r["n_clausulas"])
    satisfeitas = f"{m - int(r['f_melhor'])}/{m}"
    return f"Execução {numero} - {satisfeitas}" if numero is not None else f"Cláusulas satisfeitas: {satisfeitas}"


def draw_run(ax, trace, r, legend=True, linewidth=0.6, legend_size=None):
    """Desenha uma execução no eixo `ax` (solução atual em azul, melhor em vermelho)."""
    m = int(r["n_clausulas"])
    evals, f_cur, f_best, temp = trace
    ax.plot(evals, m - f_cur, color="tab:blue", alpha=0.45, linewidth=linewidth, label="Solução atual")
    ax.plot(evals, m - f_best, color="tab:red", linewidth=2 * linewidth / 0.6, drawstyle="steps-post",
            label="Melhor solução")
    ax.grid(alpha=0.3)
    if legend:
        ax.legend(loc="lower right", fontsize=legend_size)


def plotar(trace_path, result_path, out_path, temperature=False):
    result = load_result(result_path)
    trace = load_trace(trace_path)
    evals, temp = trace[0], trace[3]

    if temperature:
        fig, (ax, axt) = plt.subplots(2, 1, figsize=(10, 7), sharex=True, gridspec_kw={"height_ratios": [3, 1]})
    else:
        fig, ax = plt.subplots(figsize=(10, 5.5))

    draw_run(ax, trace, result)
    ax.set_ylabel("Cláusulas satisfeitas")
    ax.set_title(run_label(result, numero_execucao(trace_path)), fontsize=11)
    fig.suptitle(title_lines(result), fontsize=12)

    if temperature:
        axt.plot(evals, temp, color="tab:orange", linewidth=1.2)
        axt.set_yscale("log")
        axt.set_ylabel("Temperatura")
        axt.grid(alpha=0.3, which="both")
        axt.set_xlabel("Iterações")
    else:
        ax.set_xlabel("Iterações")

    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)

def main():
    args = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    args.add_argument("trace")
    args.add_argument("result")
    args.add_argument("out")
    args.add_argument("--temperature", action="store_true", help="painel extra com a temperatura (escala log)")
    a = args.parse_args()

    plotar(a.trace, a.result, a.out, a.temperature)

    print(f"Salvo em: {a.out}")

if __name__ == "__main__":
    main()