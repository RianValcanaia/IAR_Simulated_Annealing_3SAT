#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
#include <string>

#include "cnf.hpp"
#include "cooling.hpp"
#include "neighbor.hpp"
#include "sa.hpp"
#include "utils.hpp"

#define endl '\n'

int main(int argc, char **argv) {
    try {

        // ======= Leitura inicial =======
        std::map<std::string, std::string> argumentos = util::lerArgumentos(argc, argv);
        auto tem = [&](const char *chave) { return argumentos.count(chave) > 0; };
        
        cnf::Formula formula = cnf::lerArquivoDimacs(argumentos["--in"]);

        uint64_t seed = tem("--seed") ? std::stoull(argumentos["--seed"])
                                      : (uint64_t{std::random_device{}()} << 32) | std::random_device{}();

        sa::ParametrosSA param;
        if (tem("--cooling")) param.resfriamento = cool::lerResfriamento(argumentos["--cooling"]);
        if (tem("--t0"))      param.T0 = std::stod(argumentos["--t0"]);
        if (tem("--tn"))      param.TN = std::stod(argumentos["--tn"]);
        if (tem("--samax"))   param.SAmax = static_cast<long>(std::stoll(argumentos["--samax"]));
        if (tem("--trace-every")) param.tracoACada = static_cast<long>(std::stoll(argumentos["--trace-every"]));
        if (tem("--max-evals")) param.maxAvaliacoes = static_cast<long>(std::stoll(argumentos["--max-evals"]));
        
        double porcentagem = tem("--bit-flip") ? std::stod(argumentos["--bit-flip"]) : 0.05;
        param.bitsPorVizinho = sa::bitsPorPorcentagem(formula.numVariaveis, porcentagem);

        if (tem("--estimate-t0")) {
            long amostras = static_cast<long>(std::stoll(argumentos["--estimate-t0"]));
            rng::GeradorAleatorio gerador(seed);
            sa::EstimativaT0 estimativa = sa::estimarT0(formula, 
                                                          param.bitsPorVizinho, 
                                                          amostras,
                                                          0.95,
                                                          gerador);
            
            std::cout << "instância: " << argumentos["--in"] << endl
                      << "número de variáveis: " << formula.numVariaveis << endl
                      << "número de clausulas: " << formula.numClausulas() << endl 
                      << "bits flipados: " << param.bitsPorVizinho << endl
                      << "seed: " << seed << endl
                      << "samples: " << amostras << endl
                      << "maior custo de vizinho: " << estimativa.maiorCustoVizinho << endl
                      << "maior delta: " << estimativa.maiorPiora << endl
                      << "t0 simulado: " << estimativa.t0Simulado << endl
                      << "aceitacao: " << estimativa.taxaAceitacao
                      << endl;                                         
            return 0;
        }

        // text vai para a tela; csv vai para arquivo. --out escolhe o arquivo em qualquer formato.
        const bool formatoTexto = tem("--format") && argumentos["--format"] == "text";
        const bool saidaEmArquivo = tem("--out") || !formatoTexto;
        std::filesystem::path caminhoSaida;
        if (saidaEmArquivo)
            caminhoSaida = tem("--out") ? argumentos["--out"]
                                        : util::caminhoCsvPadrao(argumentos["--in"], param);

        // trace grava o historico para mostrar no plot; sem --trace, sai ao lado da saída: <saida>_trace.csv
        std::filesystem::path caminhoTraco;
        if (tem("--trace")) {
            caminhoTraco = argumentos["--trace"];
        } else if (saidaEmArquivo) {
            caminhoTraco = caminhoSaida;
            caminhoTraco.replace_extension();
            caminhoTraco += "_trace.csv";
        }

        std::ofstream arquivoTraco;
        if (!caminhoTraco.empty()) {
            if (caminhoTraco.has_parent_path()) std::filesystem::create_directories(caminhoTraco.parent_path());
            arquivoTraco.open(caminhoTraco, std::ios::trunc);
            if (!arquivoTraco)
                throw std::runtime_error("não foi possível criar '" + caminhoTraco.string() + "'");
        }

        // ======= Resultado =======

        rng::GeradorAleatorio gerador(seed);
        auto inicio = std::chrono::steady_clock::now();
        sa::ResultadoSA resultado = sa::executarSA(formula, 
                                                   param, 
                                                   gerador, 
                                                   arquivoTraco.is_open() ? &arquivoTraco : nullptr);

        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - inicio).count();

        // FO tem que funcionar 
        if (sa::clausulasNaoSatisfeitas(formula, resultado.melhorSolucao) != resultado.foMelhor)
            throw std::logic_error("f(s*) reportada não confere com a solução s*");

        std::ofstream arquivoSaida;
        std::ostream *saida = &std::cout;
        if (saidaEmArquivo) {
            if (caminhoSaida.has_parent_path()) std::filesystem::create_directories(caminhoSaida.parent_path());
            arquivoSaida.open(caminhoSaida, std::ios::trunc);  // sobrescreve se já existir
            if (!arquivoSaida)
                throw std::runtime_error("não foi possível criar '" + caminhoSaida.string() + "'");
            saida = &arquivoSaida;
        }

        if (formatoTexto) {
            util::imprimirTexto(*saida,
                                argumentos["--in"], 
                                formula, 
                                param, 
                                seed,
                                resultado, 
                                static_cast<long>(ms + 0.5));
            return 0;
        }

        // saida em csv
        util::escreverCsv(*saida, argumentos["--in"], formula, param, seed, resultado, static_cast<long>(ms + 0.5));
    } catch (const std::invalid_argument &erro) {
        std::cerr << "erro: " << erro.what() << "\n\n";
        util::mostrarUso(argv[0]);
        return 1;
    } catch (const std::exception &erro) {
        std::cerr << "erro: " << erro.what() << '\n';
        return 1;
    }
    return 0;
}
