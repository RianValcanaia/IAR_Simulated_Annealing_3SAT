/*
    geração de vizinhos por bit-flip.
    Um vizinho é a solução atual com alguns bits invertidos 
    escolhidos ao acaso e sem repetição. É assim que o SA anda
    de uma solução para outra.
        - bitsPorPorcentagem: converte a porcentagem pedida em nº de bits.
        - InversaoDeBits: gera os vizinhos invertendo sempre o mesmo nº de bits.
 */
#pragma once

#include <vector>

#include "cnf.hpp"
#include "random.hpp"

namespace sa {

// Nº de bits a inverter por vizinho: max(1, round(porcentagem * numVariaveis)),
// limitado a numVariaveis. porcentagem = 0 -> 1 bit.
int bitsPorPorcentagem(int numVariaveis, double porcentagem);

// Vizinhança bit-flip: o vizinho é a solução com numBits bits
// distintos, escolhidos aleatoriamente, invertidos.
class InversaoDeBits {
public:
    // Lança std::invalid_argument se numBits não estiver em [1, numVariaveis].
    InversaoDeBits(int numVariaveis, int numBits);

    int numBits() const { return numBits_; }

    void gerarVizinho(const cnf::Solucao &solucao, cnf::Solucao &vizinho, rng::GeradorAleatorio &gerador);

private:
    int numBits_;
    std::vector<int> indices_;
};

}
