#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "cnf.hpp"
#include "sa.hpp"

namespace util {
    void mostrarUso(const char *programa);

    void imprimirTexto(
        const std::string &instancia,
        const cnf::Formula &formula, 
        const sa::ParametrosSA &param, 
        uint64_t seed, 
        const sa::ResultadoSA &resultado, 
        long ms
    );

    std::map<std::string, std::string> lerArgumentos(int argc, char **argv);

    std::string solucaoComoTexto(const cnf::Solucao &solucao);
}
