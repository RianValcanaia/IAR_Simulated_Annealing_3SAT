#pragma once

#include <cstdint>
#include <map>
#include <ostream>
#include <string>

#include "cnf.hpp"
#include "sa.hpp"

namespace util {
    void mostrarUso(const char *programa);

    void imprimirTexto(
        std::ostream &saida,
        const std::string &instancia,
        const cnf::Formula &formula, 
        const sa::ParametrosSA &param, 
        uint64_t seed, 
        const sa::ResultadoSA &resultado, 
        long ms
    );

    std::map<std::string, std::string> lerArgumentos(int argc, char **argv);

    // results/<instancia sem extensão>_c<cooling>_t<t0>_sm<samax>_me<max-evals>.csv
    std::string caminhoCsvPadrao(const std::string &instancia, const sa::ParametrosSA &param);

    std::string solucaoComoTexto(const cnf::Solucao &solucao);
}
