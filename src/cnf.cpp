#include "cnf.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace cnf {

namespace {

// Interrompe a leitura com uma mensagem que diz em que linha está o problema.
[[noreturn]] void erroNaLinha(int numLinha, const std::string &mensagem) {
    throw std::runtime_error("DIMACS linha " + std::to_string(numLinha) + ": " + mensagem);
}

}  // namespace

Formula lerDimacs(std::istream &entrada) {
    Formula formula;
    int clausulasDeclaradas = -1;    // m do cabeçalho; -1 = cabeçalho ainda não lido
    std::vector<int> literaisPendentes;  // literais da cláusula em construção
    std::string linha;
    int numLinha = 0;

    while (std::getline(entrada, linha)) {
        ++numLinha;
        std::size_t inicio = linha.find_first_not_of(" \t\r");
        
        // se a linha está vazia ele pula
        if (inicio == std::string::npos) continue;
        char primeiro = linha[inicio];

        // comentário pula para proxima iteracao
        if (primeiro == 'c') continue;   
        if (primeiro == '%') break;      

        std::istringstream leitor(linha.substr(inicio));
        if (primeiro == 'p') {  // cabeçalho "p cnf <n> <m>"
            if (clausulasDeclaradas != -1) erroNaLinha(numLinha, "cabeçalho 'p' repetido");

            std::string p, formato;
            if (!(leitor >> p >> formato >> formula.numVariaveis >> clausulasDeclaradas) || formato != "cnf" ||
                formula.numVariaveis <= 0 || clausulasDeclaradas < 0)
                erroNaLinha(numLinha, "cabeçalho inválido, esperado 'p cnf <n> <m>'");
            formula.clausulas.reserve(clausulasDeclaradas);
            continue;
        }

        if (clausulasDeclaradas == -1) erroNaLinha(numLinha, "cláusula antes do cabeçalho 'p cnf'");

        // lê número por número; o 0 fecha a cláusula (ela pode ocupar várias linhas)
        std::string token;
        while (leitor >> token) {
            char *fim = nullptr;
            long literal = std::strtol(token.c_str(), &fim, 10);
            if (*fim != '\0') erroNaLinha(numLinha, "token inválido '" + token + "'");

            if (literal == 0) {
                if (literaisPendentes.size() != 3)
                    erroNaLinha(numLinha, "cláusula com " + std::to_string(literaisPendentes.size()) + " literais (esperado 3)");
                formula.clausulas.push_back({literaisPendentes[0], literaisPendentes[1], literaisPendentes[2]});
                literaisPendentes.clear();
                continue;
            }
            if (std::labs(literal) > formula.numVariaveis)
                erroNaLinha(numLinha, "literal " + token + " fora de [1, " + std::to_string(formula.numVariaveis) + "]");
            literaisPendentes.push_back(static_cast<int>(literal));
        }
    }

    if (clausulasDeclaradas == -1) erroNaLinha(numLinha, "cabeçalho 'p cnf' não encontrado");
    if (!literaisPendentes.empty()) erroNaLinha(numLinha, "última cláusula sem o terminador 0");
    if (formula.numClausulas() != clausulasDeclaradas)
        erroNaLinha(numLinha, "cabeçalho declara " + std::to_string(clausulasDeclaradas) +
                                  " cláusulas, lidas " + std::to_string(formula.numClausulas()));
    return formula;
}

Formula lerArquivoDimacs(const std::string &caminho) {
    std::ifstream arquivo(caminho);
    if (!arquivo) throw std::runtime_error("não foi possível abrir '" + caminho + "'");
    return lerDimacs(arquivo);
}

}
