/*
    A fórmula 3-SAT e a solução.
    - Formula: a instância lida do arquivo .cnf (nº de variáveis + cláusulas).
    - Solucao: um bit (0/1) por variável; é o vetor que o SA modifica.
    - lerArquivoDimacs: lê o arquivo .cnf (formato DIMACS) para a memória.
 */
#pragma once

#include <array>
#include <cstdint>
#include <istream>
#include <string>
#include <vector>


namespace cnf {

// uma clausula contem 3 literais no formato DIMACS: 
// +k = x_k, -k = ¬x_k (k de 1 a numVariaveis).
using Clausula = std::array<int, 3>;

struct Formula {
    int numVariaveis = 0;
    std::vector<Clausula> clausulas;

    int numClausulas() const { return static_cast<int>(clausulas.size()); }
};

// Lê uma instância 3-SAT em DIMACS CNF.
// uint8_t em vez de bool, pois copia e acessa bem mais rapido
using Solucao = std::vector<uint8_t>;

Formula lerDimacs(std::istream &entrada);
Formula lerArquivoDimacs(const std::string &caminho);
}
