"""Verifica a solução gravada pelo binário contra a instância CNF.

uso: python3 scripts/verify_solution.py <instancia.cnf> <resultado.csv>

"""
import argparse
import csv
import sys


def load_cnf(path):
    """Lê um arquivo DIMACS CNF e devolve (num_variaveis, lista de cláusulas)."""
    num_vars = None
    clauses = []
    pending = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("c"):
                continue
            if line.startswith("%"):
                break
            if line.startswith("p"):
                _, fmt, n, _ = line.split()[:4]
                if fmt != "cnf":
                    raise ValueError(f"formato '{fmt}' não suportado, esperado cnf")
                num_vars = int(n)
                continue
            for token in line.split():
                literal = int(token)
                if literal == 0:
                    clauses.append(pending)
                    pending = []
                else:
                    pending.append(literal)
    if num_vars is None:
        raise ValueError("cabeçalho 'p cnf' não encontrado")
    if pending:
        raise ValueError("última cláusula sem o terminador 0")
    return num_vars, clauses


def load_result(path):
    """Lê a primeira linha de dados do CSV de resultado do binário."""
    with open(path, newline="") as f:
        return next(csv.DictReader(f))


def count_unsatisfied(clauses, bits):
    """Cláusulas em que nenhum literal é verdadeiro sob a atribuição `bits`."""
    unsatisfied = 0
    for clause in clauses:
        if not any(bits[abs(lit) - 1] == (lit > 0) for lit in clause):
            unsatisfied += 1
    return unsatisfied


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("instance", help="arquivo .cnf (DIMACS)")
    ap.add_argument("result", help="CSV de saída do binário")
    a = ap.parse_args()

    num_vars, clauses = load_cnf(a.instance)
    r = load_result(a.result)
    problems = []

    if int(r["n_variaveis"]) != num_vars:
        problems.append(f"n_variaveis: CSV={r['n_variaveis']}, instância={num_vars}")
    if int(r["n_clausulas"]) != len(clauses):
        problems.append(f"n_clausulas: CSV={r['n_clausulas']}, instância={len(clauses)}")

    text = r["solucao"].strip()
    if len(text) != num_vars or set(text) - {"0", "1"}:
        problems.append(f"solução inválida: esperado {num_vars} bits 0/1, lido '{text[:40]}' ({len(text)} caracteres)")
        unsat = None
    else:
        bits = [c == "1" for c in text]
        unsat = count_unsatisfied(clauses, bits)
        if unsat != int(r["f_melhor"]):
            problems.append(f"f_melhor: CSV={r['f_melhor']}, recalculado={unsat}")

    if unsat is not None:
        print(f"cláusulas satisfeitas: {len(clauses) - unsat}/{len(clauses)} (não satisfeitas: {unsat})")
        print("instância SATISFEITA" if unsat == 0 else "instância NÃO satisfeita por esta solução")

    if problems:
        print("INCONSISTÊNCIAS:", *problems, sep="\n  - ")
        sys.exit(1)
    print("OK: o resultado do binário confere com a instância.")


if __name__ == "__main__":
    main()
