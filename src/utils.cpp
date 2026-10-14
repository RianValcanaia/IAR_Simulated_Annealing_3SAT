#include "utils.hpp"

#include <cstdlib>
#include <iostream>
#include <set>
#include <stdexcept>

#include "cooling.hpp"
#define endl '\n'

namespace util{
    void mostrarUso(const char *programa){
        std::cerr <<
            "uso: " << programa << " --in <arquivo.cnf> [opções]\n"
            << "  --in <.cnf>             instância DIMACS CNF (obrigatório)\n"
            << "  --cooling <num>         número do resfriamento (default: 1)\n"
            << "  --t0 <num>              temperatura inicial (default: 1)\n"
            << "  --tn <num>              temperatura final (default: 0.001)\n"
            << "  --samax <num>           iterações por temperatura (default: 100)\n"
            << "  --seed <num>            semente do gerador (default: aleatória, sai no CSV)\n"
            << "  --max-evals <num>       número de avaliações (default: 200000)\n"
            << "  --bit-flip <num>        fração de bits invertidos por vizinho, entre [0, 1] (default: 0.05)\n"
            << "  --nodes <num>           número de threads que serão utilizadas (default: 1)\n"
            << "  --format <csv|text>     formato da saída (default: csv)\n"
            << "  --out  <path_arquivo>   arquivo de saída (default: results/)\n" 
            << "  --trace <path_arquivo>  arquivo de trace (default: não gera)\n";
    }

    std::map<std::string, std::string> lerArgumentos(int argc, char **argv) {
        const std::set<std::string> conhecidas = {
            "--in", "--seed", "--cooling",
            "--t0", "--tn", "--samax", "--max-evals",
            "--bit-flip", "--nodes", "--format", "--out",
            "--trace", "--estimate-t0"
        };
        std::map<std::string, std::string> argumentos;
        for (int i = 1; i < argc; i += 2) {
            std::string chave = argv[i];
            
            // help
            if (chave == "-h" || chave == "--help") {
                mostrarUso(argv[0]);
                std::exit(0);
            }
            
            // diferente das cadastradas
            if (!conhecidas.count(chave)) throw std::invalid_argument("argumento desconhecido '" + chave + "'");
            
            // faltou o valor da chave
            if (i + 1 >= argc) throw std::invalid_argument("faltou o valor de '" + chave + "'");
            argumentos[chave] = argv[i + 1];
        }

        if (!argumentos.count("--in")) throw std::invalid_argument("--in é obrigatório");


        // aceita so csv ou text
        if (argumentos.count("--format") && argumentos["--format"] != "csv" && argumentos["--format"] != "text")
            throw std::invalid_argument("--format deve ser csv ou text");
        return argumentos;
    }

    void imprimirTexto(
        const std::string &instancia,
        const cnf::Formula &formula, 
        const sa::ParametrosSA &param, 
        uint64_t seed, 
        const sa::ResultadoSA &resultado, 
        long ms
    ) {

        std::cout << "instância: " << instancia << endl
                  << "número de variáveis: " << formula.numVariaveis << endl
                  << "número de cláusulas: " << formula.numClausulas() << endl
                  << "cooling schedule: " << cool::getNumeroResfriamento(param.resfriamento) << endl
                  << "seed: " << seed << endl
                  << "t0: " << param.T0 << endl
                  << "tn: " << resultado.TN << endl
                  << "SAmax: " << param.SAmax << endl
                  << "max-evals: " << param.maxAvaliacoes << endl
                  << "bits flipados: " << param.bitsPorVizinho << endl
                  << "FO final f(s*): " << resultado.foMelhor << endl
                  << "Clausulas satisfeitas: " << formula.numClausulas() - resultado.foMelhor << endl
                  << "Cláusulas não satisfeitas: " << resultado.foMelhor << endl
                  << "Encontrada na avaliação: "  << resultado.avaliacaoDaMelhor << endl
                  << "Total de avaliações: " << resultado.avaliacoes << endl
                  << "Tempo de execução: " << ms << " ms" << endl
                  << "Solução final: " << endl;
        
        for (auto bit : resultado.melhorSolucao)
            std::cout << static_cast<int>(bit) << " ";
        
        std::cout << endl;
    }

    std::string solucaoComoTexto(const cnf::Solucao &solucao) {
        std::string bits;
        for (uint8_t bit : solucao) bits += static_cast<char>('0' + bit);
        return bits;
    }
}