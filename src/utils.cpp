#include "utils.hpp"

#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <set>
#include <stdexcept>

#include "cooling.hpp"
#define endl '\n'

namespace util{
    void mostrarUso(const char *programa){
        std::cerr <<
            "uso: " << programa << " --in <arquivo.cnf> [opções]\n"
            << "  --in <.cnf>             instância DIMACS CNF (obrigatório)\n"
            << "  --cooling <num|nome>    resfriamento 0-9 ou nome, ex. geometrico (default: 1)\n"
            << "  --t0 <num>              temperatura inicial (default: 1)\n"
            << "  --tn <num>              temperatura final (default: 0.001)\n"
            << "  --samax <num>           iterações por temperatura (default: 100)\n"
            << "  --seed <num>            semente do gerador (default: aleatória, sai no CSV)\n"
            << "  --max-evals <num>       número de avaliações (default: 200000)\n"
            << "  --bit-flip <num>        fração de bits invertidos por vizinho, entre [0, 1] (default: 0.05)\n"
            << "  --format <csv|text>     formato da saída (default: csv)\n"
            << "  --out  <path_arquivo>   arquivo de saída (default: csv em results/<instancia>_c<cooling>_t<t0>_sm<samax>_me<max-evals>.csv;\n"
            << "                          text sai na tela)\n" 
            << "  --trace <path_arquivo>  arquivo de trace (default: <saida sem .csv>_trace.csv; não gera se a saída é text na tela)\n"
            << "  --trace-every <num>     grava uma linha do trace a cada <num> avaliações (default: 1)\n"
            << "  --estimate-t0 <num>     não executa o SA: estima T0 com <num> amostras e imprime na tela\n";
    }

    std::map<std::string, std::string> lerArgumentos(int argc, char **argv) {
        const std::set<std::string> conhecidas = {
            "--in", "--seed", "--cooling",
            "--t0", "--tn", "--samax", "--max-evals",
            "--bit-flip", "--format", "--out",
            "--trace", "--trace-every", "--estimate-t0"
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
        std::ostream &saida,
        const std::string &instancia,
        const cnf::Formula &formula, 
        const sa::ParametrosSA &param, 
        uint64_t seed, 
        const sa::ResultadoSA &resultado, 
        long ms
    ) {

        saida << "instância: " << instancia << endl
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
            saida << static_cast<int>(bit) << " ";
        
        saida << endl;
    }

    void escreverCsv(
        std::ostream &saida,
        const std::string &instancia,
        const cnf::Formula &formula,
        const sa::ParametrosSA &param,
        uint64_t seed,
        const sa::ResultadoSA &resultado,
        long ms
    ) {
        saida << std::setprecision(10)
              << "instancia,n_variaveis,"
                 "n_clausulas,cooling_num,"
                 "t0,tn,"
                 "samax,max_aval,"
                 "bit,seed,"
                 "ciclos,f_inicial,"
                 "f_melhor,f_final,"
                 "avaliacoes_do_melhor,tempo_ms,"
                 "solucao\n"
              << instancia << ',' << formula.numVariaveis << ','
              << formula.numClausulas() << ',' << cool::getNumeroResfriamento(param.resfriamento) << ','
              << param.T0 << ',' << resultado.TN << ','
              << param.SAmax << ',' << param.maxAvaliacoes << ','
              << param.bitsPorVizinho << ',' << seed << ','
              << resultado.ciclos << ',' << resultado.foInicial << ','
              << resultado.foMelhor << ',' << resultado.foFinal << ','
              << resultado.avaliacaoDaMelhor << ',' << ms << ','
              << solucaoComoTexto(resultado.melhorSolucao) << '\n';
    }

    std::string caminhoCsvPadrao(const std::string &instancia, const sa::ParametrosSA &param) {
        std::ostringstream nome;
        nome << "results/" << std::filesystem::path(instancia).stem().string()
             << "_c" << cool::getNumeroResfriamento(param.resfriamento)
             << "_t" << param.T0
             << "_sm" << param.SAmax
             << "_me" << param.maxAvaliacoes << ".csv";
        return nome.str();
    }

    std::string solucaoComoTexto(const cnf::Solucao &solucao) {
        std::string bits;
        for (uint8_t bit : solucao) bits += static_cast<char>('0' + bit);
        return bits;
    }
}