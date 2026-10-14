/*
    random.hpp — sorteios aleatórios do projeto.
    
    Define o gerador de numeros aleatorios usado em todo o
    SA e duas funções de sorteio:
       - sortearInteiro(gerador, n): inteiro entre 0 e n-1 
       - sortearReal01(gerador): número real entre 0 e 1 
    
    A mesma seed sempre gera a mesma sequência de sorteios, então qualquer
    execução pode ser repetida exatamente.
 */
#pragma once

#include <cstdint>
#include <random>

namespace rng {

using GeradorAleatorio = std::mt19937_64;

// Inteiro uniforme em [0, limite),
inline uint64_t sortearInteiro(GeradorAleatorio &gerador, uint64_t limite) {
    const uint64_t maiorAceito = UINT64_MAX - (UINT64_MAX % limite + 1) % limite;
    uint64_t sorteado;
    do {
        sorteado = gerador();
    } while (sorteado > maiorAceito);
    return sorteado % limite;
}

// Real uniforme em [0, 1): os 53 bits mais altos viram a mantissa do double.
inline double sortearReal01(GeradorAleatorio &gerador) {
    return static_cast<double>(gerador() >> 11) * 0x1.0p-53;
}

}
