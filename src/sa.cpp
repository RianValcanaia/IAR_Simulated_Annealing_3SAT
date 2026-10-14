#include "sa.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <stdexcept>
#include <string>

#include "neighbor.hpp"
#include "cooling.hpp"
#include "random.hpp"

namespace sa {

namespace {

void validarParametros(
    const cnf::Formula &formula, 
    const ParametrosSA &param
) {
    if (param.SAmax < 1) 
        throw std::invalid_argument("SAmax deve ser >= 1");

    if (param.maxAvaliacoes < 1) 
        throw std::invalid_argument("--max-evals deve ser >= 1");

    if (param.bitsPorVizinho < 1 || param.bitsPorVizinho > formula.numVariaveis)
        throw std::invalid_argument("bitsPorVizinho deve estar em [1, " + std::to_string(formula.numVariaveis) + "]");

    if (!(param.T0 > 0)) 
        throw std::invalid_argument("T0 deve ser > 0");
}

void escreverLinhaTraco(
    std::ostream &saida, 
    long avaliacao, 
    int foAtual, 
    int foMelhor, 
    double T
) {
    saida << avaliacao << ',' << foAtual << ',' << foMelhor << ',' << T << '\n';
}

cnf::Solucao solucaoAleatoria(
    int numVariaveis, 
    rng::GeradorAleatorio &gerador
) {
    cnf::Solucao solucao(numVariaveis);
    // usa o bit menos significativo direto do gerador para fazer o and com 1 e gerar um bit aleatorio
    for (auto &bit : solucao) bit = static_cast<uint8_t>(gerador() & 1u);
    return solucao;
}

}  // namespace

ResultadoSA executarSA(
    const cnf::Formula &formula, 
    const ParametrosSA &param, 
    rng::GeradorAleatorio &gerador,
    std::ostream *traco
) {
    // ======= Validar parâmetros =======
    validarParametros(formula, param);
    
    ResultadoSA resultado;

    resultado.ciclos = (param.maxAvaliacoes - 1 + param.SAmax - 1) / param.SAmax;
    // indice da ultima temperatura
    const long N = resultado.ciclos > 0 ? resultado.ciclos - 1 : 0;  

    cool::validarResfriamento(
        param.resfriamento, 
        param.T0, 
        param.TN, 
        N
    );

    // ======= Simulated Annealing =======                              
    // solução inicial aleatória
    InversaoDeBits vizinhanca(formula.numVariaveis, param.bitsPorVizinho);
    cnf::Solucao atual = solucaoAleatoria(formula.numVariaveis, gerador);
    cnf::Solucao vizinho;

    int foAtual = clausulasNaoSatisfeitas(formula, atual);

    resultado.foInicial = foAtual;
    resultado.melhorSolucao = atual;
    resultado.foMelhor = foAtual;
    resultado.avaliacoes = 1;
    resultado.avaliacaoDaMelhor = 1;
    resultado.TN = param.TN;

    if (traco) {
        *traco << "avaliacao,f_atual,f_melhor,temperatura\n" << std::setprecision(10);
        escreverLinhaTraco(*traco, resultado.avaliacoes, foAtual, resultado.foMelhor, param.T0);
    }

    double T = param.T0;

    for (long ciclo = 0; ciclo < resultado.ciclos; ++ciclo) {
        T = cool::temperatura(param.resfriamento, 
                        ciclo, 
                        N, 
                        param.T0,
                        param.TN);
        
        // SAmax iterações nesta temperatura
        for (long iteracao = 0; 
            iteracao < param.SAmax && resultado.avaliacoes < param.maxAvaliacoes; 
            ++iteracao
        ) {
            vizinhanca.gerarVizinho(atual, vizinho, gerador);
            int foVizinho = clausulasNaoSatisfeitas(formula, vizinho);
            int delta = foVizinho - foAtual;
            ++resultado.avaliacoes;

            // criterio de Metropolis: melhore sempre, piora com probabilidade e^(-delta/T)
            bool aceitar;
            if (delta < 0) {
                aceitar = true;
            } else {
                aceitar = rng::sortearReal01(gerador) < std::exp(-delta / T);
            }

            if (aceitar) {
                atual.swap(vizinho);  // troca sem copiar o vetor
                foAtual = foVizinho;
                if (foAtual < resultado.foMelhor) {  // nova melhor solução 
                    resultado.melhorSolucao = atual;
                    resultado.foMelhor = foAtual;
                    resultado.avaliacaoDaMelhor = resultado.avaliacoes;
                }
            }

            if (traco && (resultado.avaliacoes % param.tracoACada == 0 ||
                          resultado.avaliacoes == param.maxAvaliacoes))
                escreverLinhaTraco(*traco, resultado.avaliacoes, foAtual, resultado.foMelhor, T);
        }
    }

    resultado.foFinal = foAtual;
    return resultado;
}

EstimativaT0 estimarT0(
    const cnf::Formula &formula, 
    int bitsPorVizinho, 
    long amostras, 
    double taxaAlvo, 
    rng::GeradorAleatorio &gerador,
    double tInicial,
    double crescimento
) {
    if (amostras < 1) 
        throw std::invalid_argument("amostras deve ser >= 1");
    if (!(taxaAlvo > 0 && taxaAlvo < 1))
        throw std::invalid_argument("taxa de aceitação alvo deve estar em (0, 1)");
    if (!(tInicial > 0 && crescimento > 1)) 
        throw std::invalid_argument("tInicial > 0 e crescimento > 1");

    InversaoDeBits vizinhanca(formula.numVariaveis, bitsPorVizinho);
    const cnf::Solucao inicial = solucaoAleatoria(formula.numVariaveis, gerador);
    const int foInicial = clausulasNaoSatisfeitas(formula, inicial);
    cnf::Solucao atual, vizinho;

    // olha amostras vizinhos da solução inicial
    EstimativaT0 estimativa;
    for (long i = 0; i < amostras; ++i) {
        vizinhanca.gerarVizinho(inicial, vizinho, gerador);
        int foVizinho = clausulasNaoSatisfeitas(formula, vizinho);
        estimativa.maiorCustoVizinho = std::max(estimativa.maiorCustoVizinho, foVizinho);
        estimativa.maiorPiora = std::max(estimativa.maiorPiora, foVizinho - foInicial);
    }

    // aumenta T até aceitar taxaAlvo dos vizinhos;
    // cada rodada recomeça da solução inicial, para as temperaturas serem comparáveis
    for (double T = tInicial; T < 1e9; T *= crescimento) {
        atual = inicial;
        int foAtual = foInicial;
        long aceitos = 0;
        for (long i = 0; i < amostras; ++i) {
            vizinhanca.gerarVizinho(atual, vizinho, gerador);
            int foVizinho = clausulasNaoSatisfeitas(formula, vizinho), delta = foVizinho - foAtual;
            if (delta < 0 || rng::sortearReal01(gerador) < std::exp(-delta / T)) {
                atual.swap(vizinho);
                foAtual = foVizinho;
                ++aceitos;
            }
        }
        double taxa = double(aceitos) / amostras;
        if (taxa >= taxaAlvo) {
            estimativa.t0Simulado = T;
            estimativa.taxaAceitacao = taxa;
            return estimativa;
        }
    }
    throw std::runtime_error("estimarT0: taxa de aceitação alvo não atingida");
}

int clausulasNaoSatisfeitas(
    const cnf::Formula &formula, 
    const cnf::Solucao &solucao
) {
    int naoSatisfeitas = 0;
    for (const cnf::Clausula &clausula : formula.clausulas) {
        bool satisfeita = false;
        for (int literal : clausula) {
            if (solucao[std::abs(literal) - 1] == (literal > 0)) {
                satisfeita = true;
                break;  // basta um literal verdadeiro
            }
        }
        naoSatisfeitas += satisfeita ? 0 : 1 ;
    }
    return naoSatisfeitas;
}

}
