#!/usr/bin/env bash
# para cada instância, resfriamento e SAmax, sobe o
# max-evals (10^3, 10^4, ... 10^8) e para de subir quando as 10 execuções
# chegam a 0 cláusulas não satisfeitas (f_melhor = 0).
#
# uso: scripts/run_experiments2.sh [--threads N] [instancia ...]
#      instancia: uf20-01 uf100-01 uf250-01 (default: as três)
#
# Cada configuração roda 10 execuções via run_experiments.sh, que grava em
#   results/<instancia>_c<cooling>_t<t0>_sm<samax>_me<max-evals>/
# O resumo (nº de execuções que chegaram a 0 por configuração) vai em
#   results/resumo_campanha.csv
# Configurações cuja pasta já tem as 10 execuções são puladas (dá para retomar).
#
# Para testes rápidos, sobrescreva por variável de ambiente, por exemplo:
#   MAXITERS="1000 5000" COOLINGS="0 1" SAMAXES="1" scripts/run_experiments2.sh uf20-01

set -u

RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXPERIMENTOS="$RAIZ/scripts/run_experiments.sh"

[[ -x "$RAIZ/bin/sa3sat" ]] || { echo "erro: bin/sa3sat não encontrado (rode 'make build')" >&2; exit 1; }

COOLINGS="${COOLINGS:-0 1 2 3 4 5 6 7 8 9}"
SAMAXES="${SAMAXES:-1 100}"
MAXITERS="${MAXITERS:-1000 10000 100000 1000000 10000000 100000000}"
TN=0.1
BITFLIP=0.05
PONTOS_TRACE=1000   # cada trace fica com ~1000 pontos
EXECUCOES=10

declare -A T0_DA_INSTANCIA=( [uf20-01]=15 [uf100-01]=35 [uf250-01]=50 )

threads=10
instancias=()
while (($#)); do
    case "$1" in
        --threads) threads="${2:-}"; shift 2 ;;
        -h|--help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) instancias+=("$1"); shift ;;
    esac
done
((${#instancias[@]})) || instancias=(uf20-01 uf100-01 uf250-01)

resumo="$RAIZ/results/resumo_campanha.csv"
mkdir -p "$RAIZ/results"
[[ -f "$resumo" ]] || echo "instancia,cooling,samax,max_evals,sucessos,execucoes" > "$resumo"

# nº de execuções com f_melhor = 0 na pasta (f_melhor é a coluna 13 do CSV)
contar_sucessos() {
    local pasta=$1 n=0 f
    for f in "$pasta"/experimento*[0-9].csv; do
        [[ -f "$f" ]] || continue
        [[ "$(awk -F, 'NR==2 {print $13}' "$f")" == "0" ]] && ((n++))
    done
    echo "$n"
}

contar_execucoes() {
    local n=0 f
    for f in "$1"/experimento*[0-9].csv; do [[ -f "$f" ]] && ((n++)); done
    echo "$n"
}

# progresso: o total começa no máximo possível e diminui quando uma configuração converge
# antes do último max-evals (os níveis que ela não precisou rodar saem da conta)
niveis=($MAXITERS)
total=$(( ${#instancias[@]} * $(wc -w <<< "$COOLINGS") * $(wc -w <<< "$SAMAXES") * ${#niveis[@]} * EXECUCOES ))
feitas=0

for inst in "${instancias[@]}"; do
    t0="${T0_DA_INSTANCIA[$inst]:-}"
    [[ -n "$t0" ]] || { echo "erro: instância desconhecida '$inst'" >&2; exit 1; }
    arquivo="$RAIZ/exemples/$inst.cnf"
    [[ -f "$arquivo" ]] || { echo "erro: $arquivo não encontrado" >&2; exit 1; }

    for cooling in $COOLINGS; do
        for samax in $SAMAXES; do
            convergiu=0
            nivel=0
            for me in $MAXITERS; do
                ((nivel++))
                pasta="$RAIZ/results/${inst}_c${cooling}_t${t0}_sm${samax}_me${me}"
                ja_existia=1
                if [[ "$(contar_execucoes "$pasta")" -ne $EXECUCOES ]]; then
                    ja_existia=0
                    every=$((me / PONTOS_TRACE)); ((every < 1)) && every=1
                    echo ">> $inst cooling=$cooling samax=$samax max-evals=$me"
                    "$EXPERIMENTOS" --in "$arquivo" --cooling "$cooling" --t0 "$t0" --tn "$TN" \
                        --samax "$samax" --max-evals "$me" --bit-flip "$BITFLIP" \
                        --trace-every "$every" --threads "$threads" \
                        > "$RAIZ/results/campanha.log" 2>&1 \
                        || { echo "erro: configuração falhou (veja results/campanha.log)" >&2; exit 1; }
                fi
                ok="$(contar_sucessos "$pasta")"
                # se convergiu, os níveis seguintes saem da conta antes de imprimir o progresso
                if ((ok == EXECUCOES)); then ((total -= (${#niveis[@]} - nivel) * EXECUCOES)); fi
                if ((ja_existia)); then
                    ((feitas += EXECUCOES))
                    echo "   $feitas/$total ($inst c=$cooling sm=$samax me=$me: já existiam)"
                else
                    for ((e = 1; e <= EXECUCOES; e++)); do ((feitas++)); echo "$feitas/$total"; done
                fi
                # regrava a linha desta configuração (evita duplicar ao retomar)
                grep -v "^$inst,$cooling,$samax,$me," "$resumo" > "$resumo.tmp"; mv "$resumo.tmp" "$resumo"
                echo "$inst,$cooling,$samax,$me,$ok,$EXECUCOES" >> "$resumo"
                echo "   $ok/$EXECUCOES chegaram a 0"
                if ((ok == EXECUCOES)); then convergiu=1; break; fi
            done
            ((convergiu)) || echo "   (sem 10/10 até max-evals=${me}: sigo para a próxima configuração)"
        done
    done
done
echo "campanha concluída. Resumo em $resumo"
