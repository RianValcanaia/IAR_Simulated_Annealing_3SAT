/*
    o Simulated Annealing (SA)
    Parte de uma solução aleatória, a cada iteração, gera um vizinho:
        - se o vizinho e melhor ou igual, aceita;
        - se é pior, aceita com probabilidade e^(-delta/T), onde delta é a piora e T a
        temperatura atual. T começa em T0 e cai ate TN conforme a função de
        resfriamento escolhida; quanto menor T, menos pioras são aceitas.
    Guarda sempre a melhor solução já vista (s*), que é o resultado final.
    Para depois de um número fixo de avaliações da função objetivo.
    Tambem tem estimarT0, que calcula uma temperatura inicial pelos metodos
    do slide 38 da disciplina.
 */
#pragma once

#include <ostream>

#include "cnf.hpp"
#include "cooling.hpp"
#include "random.hpp"

namespace sa {

// Parametros de uma execucao do SA.
struct ParametrosSA {
    cool::Resfriamento resfriamento = cool::Resfriamento::Geometrico;
    double T0 = 1.0;
    double TN = 1e-3;
    long SAmax = 100;
    long maxAvaliacoes = 200000;
    int bitsPorVizinho = 1;
    long tracoACada = 1;     // gera um trace a cada 1 iteracao
};

// O que uma execução do SA devolve.
struct ResultadoSA {
    cnf::Solucao melhorSolucao;      
    int foInicial = 0;          
    int foMelhor = 0;           
    int foFinal = 0;            
    long avaliacoes = 0;   
    long avaliacaoDaMelhor = 0; 
    long ciclos = 0;      
    double TN = 0;
};

struct EstimativaT0 {
    int maiorCustoVizinho = 0;
    int maiorPiora = 0;
    double t0Simulado = 0;
    double taxaAceitacao = 0; 
};

ResultadoSA executarSA(
    const cnf::Formula &formula, 
    const ParametrosSA &param, 
    rng::GeradorAleatorio &gerador,
    std::ostream *traco = nullptr
);

EstimativaT0 estimarT0(
    const cnf::Formula &formula, 
    int bitsPorVizinho, 
    long amostras, 
    double taxaAlvo,
    rng::GeradorAleatorio &gerador, 
    double tInicial = 0.01, 
    double crescimento = 1.1
);

int clausulasNaoSatisfeitas(
    const cnf::Formula &formula, 
    const cnf::Solucao &solucao
);

}
