#include "neighbor.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>

namespace sa {

int bitsPorPorcentagem(
    int numVariaveis, 
    double porcentagem
) {
    if (!(porcentagem >= 0.0 && porcentagem <= 1.0))
        throw std::invalid_argument("porcentagem de bits deve estar em [0, 1], recebido " + std::to_string(porcentagem));
    
    // arredonda para o inteiro mais próximo
    long bits = std::lround(porcentagem * numVariaveis);  
    
    // no mínimo 1, no máximo todos
    return static_cast<int>(std::clamp<long>(bits, 1, numVariaveis));
}

InversaoDeBits::InversaoDeBits(
    int numVariaveis, 
    int numBits
) : numBits_(numBits), indices_(numVariaveis) {
    if (numBits < 1 || numBits > numVariaveis)
        throw std::invalid_argument("numBits = " + std::to_string(numBits) + " fora de [1, " + std::to_string(numVariaveis) + "]");
    // preenche indices_ com 0, 1, ..., numVariaveis-1
    std::iota(indices_.begin(), indices_.end(), 0);
}

void InversaoDeBits::gerarVizinho(const cnf::Solucao &solucao, cnf::Solucao &vizinho, rng::GeradorAleatorio &gerador) {
    vizinho = solucao;
    const int n = static_cast<int>(indices_.size());
    for (int j = 0; j < numBits_; ++j) {
        // sorteia uma posição entre as ainda não usadas nesta chamada (j até n-1)
        // e a traz para a posição j; assim as j primeiras são as já escolhidas
        int sorteada = j + static_cast<int>(rng::sortearInteiro(gerador, n - j));
        std::swap(indices_[j], indices_[sorteada]);
        vizinho[indices_[j]] ^= 1u;  // inverte o bit (0 -> 1, 1 -> 0)
    }
}

}
