// funcões de resfriamento para simulated annealing
#pragma once

#include <string>

namespace cool {

// ver docs/FormulasDeResfriamento_SA.pdf para entender cada formula
enum class Resfriamento {
    Linear,      
    Geometrico,  
    Inversa,     
    Potencia,    
    Sigmoide,    
    Cosseno,     
    Tanh,        
    Cosh,        
    Exponencial, 
    Gaussiana,   
};

/*
    Aceita o numero do docs/FormulasDeResfriamento_SA.pdf (0 a 9) ou o nome 
    usado na linha de comando: linear, geometrico, inversa, potencia, sigmoide,
    cosseno, tanh, cosh, exponencial, gaussiana.
*/
Resfriamento lerResfriamento(const std::string &nomeOuNumero);

/*
    Valida se T0, TN, N servem para a formula exige:
    - 0 < TN < T0 e N >= 0;
    - Potencia exige tambem: T0 - TN > 1 e N >= 2
*/
void validarResfriamento(
    Resfriamento r, 
    double T0, 
    double TN, 
    long N
);


// Temperatura do ciclo i, com i de 0 a N. 
double temperatura(
    Resfriamento r, 
    long i, 
    long N, 
    double T0, 
    double TN
);

int getNumeroResfriamento(
    Resfriamento r
);

std::string getNomeResfriamento(
    Resfriamento r
);

}
