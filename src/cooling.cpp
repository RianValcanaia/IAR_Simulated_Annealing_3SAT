#include "cooling.hpp"

#include <cmath>
#include <stdexcept>

namespace cool {

namespace {

constexpr double pi = 3.14159265358979323846;

// informacoes sobre cada tipo de resfriamento conforme docs/
struct InfoResfriamento {
    Resfriamento resfriamento;
    int numero;
    const char *nome;
};

const InfoResfriamento Resfriamentos[] = {
    {Resfriamento::Linear, 0, "linear"},
    {Resfriamento::Geometrico, 1, "geometrico"},
    {Resfriamento::Inversa, 2, "inversa"},
    {Resfriamento::Potencia, 3, "potencia"},
    {Resfriamento::Sigmoide, 4, "sigmoide"},
    {Resfriamento::Cosseno, 5, "cosseno"},
    {Resfriamento::Tanh, 6, "tanh"},
    {Resfriamento::Cosh, 7, "cosh"},
    {Resfriamento::Exponencial, 8, "exponencial"},
    {Resfriamento::Gaussiana, 9, "gaussiana"},
};

const InfoResfriamento &infoDe(Resfriamento r) {
    for (const auto &info : Resfriamentos)
        if (info.resfriamento == r) return info;
    throw std::logic_error("função de resfriamento sem nome");
}

}

Resfriamento lerResfriamento(
    const std::string &nome_ou_numero
) {
    for (const auto &info : Resfriamentos)
        if (nome_ou_numero == info.nome || nome_ou_numero == std::to_string(info.numero)) 
            return info.resfriamento;
    
    throw std::invalid_argument(
        "resfriamento desconhecido '" + nome_ou_numero + "\n"
        "Use o número ou o nome do resfriamento: \n" + 
        "\t1 - linear\n" + 
        "\t2 - geometrico\n" +
        "\t3 - inversa\n" +
        "\t4 - potencia\n" +
        "\t5 - sigmoide\n" +
        "\t6 - cosseno\n" +
        "\t7 - tanh\n" +
        "\t8 - cosh\n" +
        "\t9 - exponencial\n" +
        "\t10 - gaussiana\n"
    );
}

void validarResfriamento(
    Resfriamento r, 
    double T0, 
    double TN, 
    long N
) {
    if (!(TN > 0 && TN < T0))
        throw std::invalid_argument(
            "temperaturas devem satisfazer 0 < TN < T0 (T0=" +
            std::to_string(T0) + ", TN=" + std::to_string(TN) + ")");

    if (N < 0) throw std::invalid_argument("N negativo");

    if (r == Resfriamento::Potencia && N >= 1 && !(T0 - TN > 1 && N >= 2))
        throw std::invalid_argument("resfriamento power exige T0 - TN > 1 e ao menos 3 ciclos");
}

double temperatura(
    Resfriamento r, 
    long i, 
    long N, 
    double T0, 
    double TN
) {
    if (N == 0) return T0;
    
    const double x = static_cast<double>(i), n = static_cast<double>(N), d = T0 - TN;
    
    switch (r) {
        case Resfriamento::Linear:
            return T0 - x * d / n;
        case Resfriamento::Geometrico:
            return T0 * std::pow(TN / T0, x / n);
        case Resfriamento::Inversa: {
            double A = d * (n + 1) / n;
            return A / (x + 1) + (T0 - A);
        }
        case Resfriamento::Potencia:
            if (i == 0) return T0;  
            return T0 - std::pow(x, std::log(d) / std::log(n));
        case Resfriamento::Sigmoide:
            return d / (1 + std::exp(0.3 * (x - n / 2))) + TN;
        case Resfriamento::Cosseno:
            return 0.5 * d * (1 + std::cos(x * pi / n)) + TN;
        case Resfriamento::Tanh:
            return 0.5 * d * (1 - std::tanh(10 * x / n - 5)) + TN;
        case Resfriamento::Cosh:
            return d / std::cosh(10 * x / n) + TN;
        case Resfriamento::Exponencial:
            return T0 * std::exp(-(std::log(T0 / TN) / n) * x);
        case Resfriamento::Gaussiana:
            return T0 * std::exp(-std::log(T0 / TN) * (x * x) / (n * n));
    }
    throw std::logic_error("função de resfriamento sem implementação");
}

int getNumeroResfriamento(Resfriamento r) { return infoDe(r).numero; }

std::string getNomeResfriamento(Resfriamento r) { return infoDe(r).nome; }

}
