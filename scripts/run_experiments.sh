#!/usr/bin/env bash
# Roda o SA 10 vezes em paralelo.
#
# uso: scripts/run_experiments.sh --in <instancia.cnf> [opções do binário]
#
# Aceita as mesmas flags do binário (--help mostra a lista). Diferenças:
#   --out <pasta>    pasta onde ficam os experimentos
#                    (default: results/<instancia>_c<cooling>_t<t0>_sm<samax>_me<max-evals>/)
#   --trace <pasta>  pasta dos traces (default: a mesma pasta dos resultados)
#   --seed <num>     seed base; a execução i usa a base rotacionada i-1 bits à esquerda
#                    (default: a hora atual em nanossegundos)
#   --cooling        aceita número (0-9) ou nome
#   --format text    grava experimento<i>.txt em vez de .csv
#   --threads <num>  quantas execuções rodam ao mesmo tempo (default: 10, todas)
#   --estimate-t0    não é suportado aqui
#
# Arquivos: experimento<i>.csv e experimento<i>trace.csv (i = 1..10)

set -u

NUM_EXECUCOES=10
RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BINARIO="$RAIZ/bin/sa3sat"

[[ -x "$BINARIO" ]] || { echo "erro: binário não encontrado em $BINARIO (compile antes)" >&2; exit 1; }

# defaults iguais aos do binário; sobrescritos pelos argumentos
instancia=""; cooling=1; t0=1; samax=100; max_evals=200000
threads=$NUM_EXECUCOES
formato=csv; pasta_saida=""; pasta_trace=""; seed_base=""
repassar=()   # argumentos que vão direto para o binário

args=("$@")
for ((k = 0; k < ${#args[@]}; k++)); do
    chave="${args[k]}"
    valor="${args[k+1]:-}"
    case "$chave" in
        -h|--help)
            "$BINARIO" --help 2>&1 | sed 's/^uso: .*/uso: run_experiments.sh --in <arquivo.cnf> [opções]/'
            echo
            echo "run_experiments: --out e --trace são pastas; --seed é a seed base; --threads <num> limita as execuções simultâneas (default: 10)."
            exit 0 ;;
        --estimate-t0)
            echo "erro: --estimate-t0 não é suportado pelo run_experiments (use o binário direto)" >&2; exit 1 ;;
        --out)   pasta_saida="$valor" ;;
        --trace) pasta_trace="$valor" ;;
        --seed)  seed_base="$valor" ;;
        --threads) threads="$valor" ;;
        *)
            case "$chave" in
                --in)        instancia="$valor" ;;
                --cooling)   cooling="$valor" ;;
                --t0)        t0="$valor" ;;
                --samax)     samax="$valor" ;;
                --max-evals) max_evals="$valor" ;;
                --format)    formato="$valor" ;;
            esac
            repassar+=("$chave")
            [[ $((k + 1)) -lt ${#args[@]} ]] && repassar+=("$valor")
            ;;
    esac
    ((k++))   # toda flag do binário tem um valor
done

[[ -n "$instancia" ]] || { echo "erro: --in <instancia.cnf> é obrigatório" >&2; exit 1; }
[[ "$threads" =~ ^[1-9][0-9]*$ ]] || { echo "erro: --threads deve ser um inteiro >= 1 (recebido '$threads')" >&2; exit 1; }
[[ "$formato" == csv || "$formato" == text ]] || { echo "erro: --format deve ser csv ou text" >&2; exit 1; }

# o nome da pasta usa o número do cooling, mesmo se foi passado o nome
case "$cooling" in
    linear) cooling=0 ;;      geometrico) cooling=1 ;;   inversa) cooling=2 ;;
    potencia) cooling=3 ;;    sigmoide) cooling=4 ;;     cosseno) cooling=5 ;;
    tanh) cooling=6 ;;        cosh) cooling=7 ;;         exponencial) cooling=8 ;;
    gaussiana) cooling=9 ;;
    [0-9]) ;;
    *) echo "erro: cooling desconhecido '$cooling' (use 0-9 ou o nome)" >&2; exit 1 ;;
esac

nome="$(basename "$instancia")"
nome="${nome%.cnf}"
[[ -n "$pasta_saida" ]] || pasta_saida="$RAIZ/results/${nome}_c${cooling}_t${t0}_sm${samax}_me${max_evals}"
[[ -n "$pasta_trace" ]] || pasta_trace="$pasta_saida"
mkdir -p "$pasta_saida" "$pasta_trace"

extensao=csv; [[ "$formato" == text ]] && extensao=txt

# seed base = --seed ou a hora atual em nanossegundos; cada execução usa a base
# rotacionada i bits à esquerda (i = 0..9), então todas as seeds diferem.
base="${seed_base:-$(date +%s%N)}"
[[ "$base" =~ ^[0-9]+$ ]] || { echo "erro: --seed deve ser um número (recebido '$base')" >&2; exit 1; }
base=$((base))   # a aritmética do bash trabalha com 64 bits (envolve se passar de 2^63)

seed_da_execucao() {
    local i=$1
    if ((i == 0)); then
        printf '%u' "$base"
    else
        printf '%u' $(( (base << i) | ((base >> (64 - i)) & ((1 << i) - 1)) ))
    fi
}

inicio=$(date +%s%N)
falhas=0
rodando=0
for ((i = 1; i <= NUM_EXECUCOES; i++)); do
    # com `threads` execuções em andamento, espera uma terminar antes de lançar a próxima
    if ((rodando >= threads)); then
        wait -n || { echo "uma execução falhou" >&2; ((falhas++)); }
        ((rodando--))
    fi
    seed=$(seed_da_execucao $((i - 1)))
    "$BINARIO" "${repassar[@]}" \
        --seed "$seed" \
        --out "$pasta_saida/experimento${i}.${extensao}" \
        --trace "$pasta_trace/experimento${i}trace.csv" &
    ((rodando++))
    echo "experimento $i: seed $seed (pid $!)"
done

while ((rodando > 0)); do
    wait -n || { echo "uma execução falhou" >&2; ((falhas++)); }
    ((rodando--))
done

fim=$(date +%s%N)
ms=$(( (fim - inicio) / 1000000 ))
printf 'tempo total decorrido: %d.%03d s\n' $((ms / 1000)) $((ms % 1000))

((falhas == 0)) && echo "ok: resultados em $pasta_saida" || { echo "$falhas execuções falharam" >&2; exit 1; }
