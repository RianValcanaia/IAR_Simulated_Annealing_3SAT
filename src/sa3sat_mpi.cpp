/*
    Campanha de experimentos em paralelo com MPI (mestre e operários).

    Para cada instância, resfriamento e SAmax, sobe o max-evals (10^4 ... 10^8) e
    PARA de subir quando as 10 execuções do nível chegam a 0 cláusulas não
    satisfeitas (f_melhor = 0). É a mesma campanha de scripts/run_experiments2.sh, que roda só nesta máquina.

    - Mestre (rank 0): guarda a fila de tarefas (uma tarefa = uma execução do SA),
      entrega uma a cada operário livre, grava os arquivos e decide o próximo nível
      de cada cadeia quando as 10 execuções do nível terminam.
    - Operários (rank 1..N-1): rodam o SA no próprio processo e devolvem o CSV e o
      trace prontos. Os arquivos saem no mesmo layout de run_experiments.sh:
          results/<instancia>_c<cooling>_t<t0>_sm<samax>_me<max-evals>/
              experimento<i>.csv  e  experimento<i>trace.csv
      mais results/resumo_campanha.csv.
    - Retomável: execuções que já têm os dois arquivos em results/ são puladas.

    compilar:  make mpi
    executar:  mpirun -np 27 --machinefile hosts.txt ./bin/sa3sat_mpi        (1 mestre + 26 operários)
    testar:    mpirun -np 3 ./bin/sa3sat_mpi --instances uf20-01 --coolings 0,1 --samaxes 1 --maxiters 1000,20000
*/
#include <mpi.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "cnf.hpp"
#include "cooling.hpp"
#include "neighbor.hpp"
#include "random.hpp"
#include "sa.hpp"
#include "utils.hpp"

namespace fs = std::filesystem;

namespace {

enum Tag { TAG_TAREFA = 1, TAG_RESULTADO = 2, TAG_FIM = 3 };

// ----------------------------------------------------------------------------
// Configuração (todos os ranks leem os mesmos argumentos, então ficam iguais)
// ----------------------------------------------------------------------------
struct Config {
    std::vector<std::string> instancias = {"uf20-01", "uf100-01", "uf250-01"};
    std::vector<int> coolings = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<int> samaxes = {1, 100};
    std::vector<long> maxiters = {10000, 100000, 1000000, 10000000, 100000000};
    double tn = 0.1;
    double bitFlip = 0.05;
    int execucoes = 10;
    int pontosTrace = 1000;  // cada trace fica com ~1000 pontos
    std::string pastaExemplos = "exemples";
    std::string pastaResultados = "results";

    double t0DaInstancia(const std::string &nome) const {
        static const std::map<std::string, double> t0 = {{"uf20-01", 15}, {"uf100-01", 35}, {"uf250-01", 50}};
        auto it = t0.find(nome);
        if (it == t0.end()) throw std::invalid_argument("instância sem T0 definido: " + nome);
        return it->second;
    }
};

template <typename T>
std::vector<T> lerLista(const std::string &texto) {
    std::vector<T> itens;
    std::stringstream ss(texto);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (item.empty()) continue;
        std::stringstream conv(item);
        T valor;
        if (!(conv >> valor)) throw std::invalid_argument("valor inválido na lista: '" + item + "'");
        itens.push_back(valor);
    }
    if (itens.empty()) throw std::invalid_argument("lista vazia: '" + texto + "'");
    return itens;
}

void mostrarUso() {
    std::cerr <<
        "uso: mpirun -np <N> sa3sat_mpi [opções]   (N >= 2: 1 mestre + operários)\n"
        "  --instances <a,b,..>   default: uf20-01,uf100-01,uf250-01\n"
        "  --coolings <0,1,..>    default: 0 a 9\n"
        "  --samaxes <1,100>      default: 1,100\n"
        "  --maxiters <n,..>      default: 10^4 a 10^8\n"
        "  --tn <num>             default: 0.1\n"
        "  --bit-flip <num>       default: 0.05\n"
        "  --execucoes <num>      execuções por configuração (default: 10)\n"
        "  --trace-points <num>   pontos por trace (default: 1000)\n"
        "  --exemples <pasta>     pasta dos .cnf (default: exemples)\n"
        "  --results <pasta>      pasta de saída (default: results)\n";
}

Config lerConfig(int argc, char **argv) {
    Config cfg;
    for (int i = 1; i < argc; i += 2) {
        const std::string chave = argv[i];
        if (chave == "-h" || chave == "--help") { mostrarUso(); std::exit(0); }
        if (i + 1 >= argc) throw std::invalid_argument("faltou o valor de '" + chave + "'");
        const std::string valor = argv[i + 1];
        if (chave == "--instances") cfg.instancias = lerLista<std::string>(valor);
        else if (chave == "--coolings") cfg.coolings = lerLista<int>(valor);
        else if (chave == "--samaxes") cfg.samaxes = lerLista<int>(valor);
        else if (chave == "--maxiters") cfg.maxiters = lerLista<long>(valor);
        else if (chave == "--tn") cfg.tn = std::stod(valor);
        else if (chave == "--bit-flip") cfg.bitFlip = std::stod(valor);
        else if (chave == "--execucoes") cfg.execucoes = std::stoi(valor);
        else if (chave == "--trace-points") cfg.pontosTrace = std::stoi(valor);
        else if (chave == "--exemples") cfg.pastaExemplos = valor;
        else if (chave == "--results") cfg.pastaResultados = valor;
        else throw std::invalid_argument("argumento desconhecido '" + chave + "'");
    }
    if (cfg.execucoes < 1 || cfg.pontosTrace < 1) throw std::invalid_argument("--execucoes e --trace-points devem ser >= 1");
    for (const auto &nome : cfg.instancias) cfg.t0DaInstancia(nome);  // valida os nomes
    return cfg;
}

// ----------------------------------------------------------------------------
// Mensagens
// ----------------------------------------------------------------------------
struct Tarefa {
    int id;
    int instancia;   // índice em cfg.instancias
    int cooling;
    int samax;
    long maxEvals;
    int execucao;    // 1..execucoes
    uint64_t seed;
};

// o resultado é: cabeçalho + bytes do CSV + bytes do trace
struct CabecalhoResultado {
    int id;
    int fMelhor;           // -1 = erro; o "CSV" então é a mensagem de erro
    uint64_t tamanhoCsv;
    uint64_t tamanhoTrace;
};

// ----------------------------------------------------------------------------
// Caminhos e arquivos
// ----------------------------------------------------------------------------
std::string numeroParaTexto(double valor) {
    std::ostringstream ss;
    ss << valor;
    return ss.str();
}

fs::path pastaDaConfiguracao(const Config &cfg, const std::string &instancia, int cooling, int samax, long maxEvals) {
    return fs::path(cfg.pastaResultados) / (instancia + "_c" + std::to_string(cooling) + "_t" +
                                            numeroParaTexto(cfg.t0DaInstancia(instancia)) + "_sm" +
                                            std::to_string(samax) + "_me" + std::to_string(maxEvals));
}

fs::path arquivoCsv(const fs::path &pasta, int execucao) { return pasta / ("experimento" + std::to_string(execucao) + ".csv"); }
fs::path arquivoTrace(const fs::path &pasta, int execucao) { return pasta / ("experimento" + std::to_string(execucao) + "trace.csv"); }

// grava em arquivo temporário e renomeia, para uma queda no meio não deixar arquivo pela metade
void gravarArquivo(const fs::path &caminho, const char *dados, std::size_t tamanho) {
    const fs::path temporario = caminho.string() + ".tmp";
    {
        std::ofstream saida(temporario, std::ios::binary | std::ios::trunc);
        saida.write(dados, static_cast<std::streamsize>(tamanho));
        if (!saida) throw std::runtime_error("não foi possível gravar '" + temporario.string() + "'");
    }
    fs::rename(temporario, caminho);
}

// f_melhor (coluna 13) de um CSV já gravado; -1 se o arquivo não está legível
int lerFMelhor(const fs::path &caminho) {
    std::ifstream entrada(caminho);
    std::string linha;
    if (!std::getline(entrada, linha) || !std::getline(entrada, linha)) return -1;
    std::stringstream ss(linha);
    std::string campo;
    for (int k = 0; k < 13; ++k)
        if (!std::getline(ss, campo, ',')) return -1;
    try { return std::stoi(campo); } catch (...) { return -1; }
}

// ----------------------------------------------------------------------------
// Operário
// ----------------------------------------------------------------------------
void enviarResultado(int id, int fMelhor, const std::string &csv, const std::string &trace) {
    const CabecalhoResultado cab{id, fMelhor, csv.size(), trace.size()};
    std::vector<char> mensagem(sizeof(cab) + csv.size() + trace.size());
    std::copy(reinterpret_cast<const char *>(&cab), reinterpret_cast<const char *>(&cab) + sizeof(cab), mensagem.begin());
    std::copy(csv.begin(), csv.end(), mensagem.begin() + sizeof(cab));
    std::copy(trace.begin(), trace.end(), mensagem.begin() + sizeof(cab) + csv.size());
    MPI_Send(mensagem.data(), static_cast<int>(mensagem.size()), MPI_BYTE, 0, TAG_RESULTADO, MPI_COMM_WORLD);
}

void operario(const Config &cfg) {
    std::vector<cnf::Formula> formulas(cfg.instancias.size());
    std::vector<bool> carregada(cfg.instancias.size(), false);

    for (;;) {
        Tarefa t;
        MPI_Status status;
        MPI_Recv(&t, sizeof(t), MPI_BYTE, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        if (status.MPI_TAG == TAG_FIM) return;

        try {
            const std::string &nome = cfg.instancias[t.instancia];
            const std::string caminho = cfg.pastaExemplos + "/" + nome + ".cnf";
            if (!carregada[t.instancia]) {
                formulas[t.instancia] = cnf::lerArquivoDimacs(caminho);
                carregada[t.instancia] = true;
            }
            const cnf::Formula &formula = formulas[t.instancia];

            sa::ParametrosSA param;
            param.resfriamento = cool::lerResfriamento(std::to_string(t.cooling));
            param.T0 = cfg.t0DaInstancia(nome);
            param.TN = cfg.tn;
            param.SAmax = t.samax;
            param.maxAvaliacoes = t.maxEvals;
            param.bitsPorVizinho = sa::bitsPorPorcentagem(formula.numVariaveis, cfg.bitFlip);
            param.tracoACada = std::max<long>(1, t.maxEvals / cfg.pontosTrace);

            rng::GeradorAleatorio gerador(t.seed);
            std::ostringstream trace;
            const auto inicio = std::chrono::steady_clock::now();
            const sa::ResultadoSA resultado = sa::executarSA(formula, param, gerador, &trace);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - inicio).count();

            if (sa::clausulasNaoSatisfeitas(formula, resultado.melhorSolucao) != resultado.foMelhor)
                throw std::logic_error("f(s*) reportada não confere com a solução s*");

            std::ostringstream csv;
            util::escreverCsv(csv, caminho, formula, param, t.seed, resultado, static_cast<long>(ms + 0.5));
            enviarResultado(t.id, resultado.foMelhor, csv.str(), trace.str());
        } catch (const std::exception &erro) {
            enviarResultado(t.id, -1, erro.what(), "");
        }
    }
}

// ----------------------------------------------------------------------------
// Mestre
// ----------------------------------------------------------------------------
uint64_t splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// Uma cadeia = (instância, cooling, samax): sobe o max-evals até as execuções chegarem a 0.
struct Cadeia {
    int instancia, cooling, samax;
    std::size_t nivel = 0;   // índice em cfg.maxiters
    int pendentes = 0;       // execuções do nível ainda não terminadas
    int sucessos = 0;        // execuções do nível com f_melhor = 0
    bool concluida = false;
    bool niveisDescontados = false;   // os níveis pulados já saíram do total
};

struct Mestre {
    const Config &cfg;
    std::vector<Cadeia> cadeias;
    std::vector<std::string> resumo;           // linhas do resumo_campanha.csv
    std::vector<int> ociosos;                  // ranks de operários livres

    struct ItemFila { Tarefa tarefa; int cadeia; };
    struct MenorPrioridade {  // maior max-evals sai primeiro (as execuções longas começam cedo)
        bool operator()(const ItemFila &a, const ItemFila &b) const {
            if (a.tarefa.maxEvals != b.tarefa.maxEvals) return a.tarefa.maxEvals < b.tarefa.maxEvals;
            return a.tarefa.id > b.tarefa.id;
        }
    };
    std::priority_queue<ItemFila, std::vector<ItemFila>, MenorPrioridade> fila;
    std::unordered_map<int, ItemFila> emVoo;   // id da tarefa -> tarefa
    uint64_t sementeBase;
    int proximoId = 0;
    // progresso: o total começa no máximo possível e diminui quando uma cadeia converge
    // antes do último max-evals (os níveis que ela não precisou rodar saem da conta)
    long total = 0;
    long feitas = 0;

    explicit Mestre(const Config &c) : cfg(c) {
        sementeBase = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
        for (std::size_t i = 0; i < cfg.instancias.size(); ++i)
            for (int cooling : cfg.coolings)
                for (int samax : cfg.samaxes) cadeias.push_back({static_cast<int>(i), cooling, samax});
        total = static_cast<long>(cadeias.size()) * static_cast<long>(cfg.maxiters.size()) * cfg.execucoes;
    }

    // a cadeia convergiu no nível atual: os níveis seguintes não serão rodados
    void descontarNiveisPulados(Cadeia &c) {
        if (c.niveisDescontados) return;
        c.niveisDescontados = true;
        const long pulados = static_cast<long>(cfg.maxiters.size() - 1 - c.nivel) * cfg.execucoes;
        total -= pulados;
        if (pulados > 0)
            std::cout << "[mestre] " << cfg.instancias[c.instancia] << " cooling=" << c.cooling << " samax=" << c.samax
                      << " convergiu em max-evals=" << cfg.maxiters[c.nivel] << ": " << pulados
                      << " execuções dos níveis seguintes não serão rodadas (total " << total + pulados << " -> "
                      << total << ")" << std::endl;
    }

    long maxEvalsDoNivel(const Cadeia &c) const { return cfg.maxiters[c.nivel]; }

    fs::path pastaDaCadeia(const Cadeia &c) const {
        return pastaDaConfiguracao(cfg, cfg.instancias[c.instancia], c.cooling, c.samax, maxEvalsDoNivel(c));
    }

    void gravarResumo() const {
        std::ostringstream ss;
        ss << "instancia,cooling,samax,max_evals,sucessos,execucoes\n";
        for (const auto &linha : resumo) ss << linha << '\n';
        fs::create_directories(cfg.pastaResultados);
        gravarArquivo(fs::path(cfg.pastaResultados) / "resumo_campanha.csv", ss.str().data(), ss.str().size());
    }

    // Encerra o nível atual da cadeia. Devolve true se ela deve seguir para o próximo nível.
    bool fecharNivel(Cadeia &c) {
        const std::string &nome = cfg.instancias[c.instancia];
        resumo.push_back(nome + "," + std::to_string(c.cooling) + "," + std::to_string(c.samax) + "," +
                         std::to_string(maxEvalsDoNivel(c)) + "," + std::to_string(c.sucessos) + "," +
                         std::to_string(cfg.execucoes));
        gravarResumo();
        std::cout << "[mestre] " << nome << " cooling=" << c.cooling << " samax=" << c.samax
                  << " max-evals=" << maxEvalsDoNivel(c) << ": " << c.sucessos << "/" << cfg.execucoes
                  << " chegaram a 0" << std::endl;
        if (c.sucessos == cfg.execucoes) { descontarNiveisPulados(c); c.concluida = true; return false; }
        if (++c.nivel >= cfg.maxiters.size()) {
            std::cout << "[mestre]   sem " << cfg.execucoes << "/" << cfg.execucoes << " até o último max-evals" << std::endl;
            c.concluida = true;
            return false;
        }
        return true;
    }

    // Inicia o nível atual: enfileira o que falta e conta o que já está em disco.
    // Se o nível inteiro já existia, fecha-o e segue para o próximo.
    void avancar(int indice) {
        Cadeia &c = cadeias[indice];
        while (!c.concluida) {
            c.pendentes = 0;
            c.sucessos = 0;
            const fs::path pasta = pastaDaCadeia(c);
            for (int e = 1; e <= cfg.execucoes; ++e) {
                const int fMelhor = (fs::exists(arquivoTrace(pasta, e)) && fs::exists(arquivoCsv(pasta, e)))
                                        ? lerFMelhor(arquivoCsv(pasta, e)) : -1;
                if (fMelhor >= 0) {
                    if (fMelhor == 0) ++c.sucessos;
                    ++feitas;   // já estava em disco (retomada)
                    continue;
                }
                Tarefa t{proximoId, c.instancia, c.cooling, c.samax, maxEvalsDoNivel(c), e,
                         splitmix64(sementeBase + static_cast<uint64_t>(proximoId))};
                ++proximoId;
                fila.push({t, indice});
                ++c.pendentes;
            }
            if (c.pendentes > 0) return;
            if (!fecharNivel(c)) return;
        }
    }

    void despachar() {
        while (!ociosos.empty() && !fila.empty()) {
            ItemFila item = fila.top();
            fila.pop();
            const int operario = ociosos.back();
            ociosos.pop_back();
            MPI_Send(&item.tarefa, sizeof(Tarefa), MPI_BYTE, operario, TAG_TAREFA, MPI_COMM_WORLD);
            emVoo[item.tarefa.id] = item;
        }
    }

    // Trata uma mensagem de resultado e devolve o rank do operário que a enviou.
    int receberResultado() {
        MPI_Status status;
        MPI_Probe(MPI_ANY_SOURCE, TAG_RESULTADO, MPI_COMM_WORLD, &status);
        int tamanho = 0;
        MPI_Get_count(&status, MPI_BYTE, &tamanho);
        std::vector<char> mensagem(static_cast<std::size_t>(tamanho));
        MPI_Recv(mensagem.data(), tamanho, MPI_BYTE, status.MPI_SOURCE, TAG_RESULTADO, MPI_COMM_WORLD, &status);

        CabecalhoResultado cab;
        std::copy(mensagem.begin(), mensagem.begin() + sizeof(cab), reinterpret_cast<char *>(&cab));
        const char *corpo = mensagem.data() + sizeof(cab);
        const std::size_t tamanhoCorpo = mensagem.size() - sizeof(cab);

        const ItemFila item = emVoo.at(cab.id);
        emVoo.erase(cab.id);

        if (cab.fMelhor < 0) {
            std::cerr << "[mestre] erro no operário " << status.MPI_SOURCE << ": "
                      << std::string(corpo, tamanhoCorpo) << std::endl;
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        return finalizarTarefa(item, cab, corpo, tamanhoCorpo, status.MPI_SOURCE);
    }

    int finalizarTarefa(const ItemFila &item, const CabecalhoResultado &cab, const char *corpo,
                        std::size_t tamanhoCorpo, int operario) {
        Cadeia &c = cadeias[item.cadeia];
        const fs::path pasta = pastaDaConfiguracao(cfg, cfg.instancias[item.tarefa.instancia], item.tarefa.cooling,
                                                   item.tarefa.samax, item.tarefa.maxEvals);
        fs::create_directories(pasta);

        if (cab.tamanhoCsv + cab.tamanhoTrace != tamanhoCorpo)
            throw std::runtime_error("mensagem de resultado com tamanho inconsistente");
        const std::size_t fimCsv = cab.tamanhoCsv;
        // trace primeiro e CSV por último: o CSV existir significa que a execução terminou inteira
        gravarArquivo(arquivoTrace(pasta, item.tarefa.execucao), corpo + fimCsv, tamanhoCorpo - fimCsv);
        gravarArquivo(arquivoCsv(pasta, item.tarefa.execucao), corpo, fimCsv);

        if (cab.fMelhor == 0) ++c.sucessos;
        ++feitas;
        const bool nivelFechou = (c.pendentes == 1);   // esta era a última do nível
        if (nivelFechou && c.sucessos == cfg.execucoes) descontarNiveisPulados(c);   // antes de imprimir
        std::cout << feitas << "/" << total << " (faltam " << total - feitas << ")  " << cfg.instancias[item.tarefa.instancia]
                  << " cooling=" << item.tarefa.cooling << " samax=" << item.tarefa.samax
                  << " max-evals=" << item.tarefa.maxEvals << " execucao=" << item.tarefa.execucao
                  << " f_melhor=" << cab.fMelhor << std::endl;
        if (--c.pendentes == 0 && fecharNivel(c)) avancar(item.cadeia);
        return operario;
    }

    void executar(int tamanhoMundo) {
        for (int rank = 1; rank < tamanhoMundo; ++rank) ociosos.push_back(rank);
        std::cout << "[mestre] " << cadeias.size() << " cadeias, " << (tamanhoMundo - 1)
                  << " operários, seed base " << sementeBase << std::endl;

        for (std::size_t i = 0; i < cadeias.size(); ++i) avancar(static_cast<int>(i));
        if (feitas > 0) std::cout << "[mestre] retomando: " << feitas << " execuções já estavam em disco" << std::endl;
        despachar();

        while (!emVoo.empty()) {
            ociosos.push_back(receberResultado());
            despachar();
        }

        for (int rank = 1; rank < tamanhoMundo; ++rank) {
            int vazio = 0;
            MPI_Send(&vazio, sizeof(vazio), MPI_BYTE, rank, TAG_FIM, MPI_COMM_WORLD);
        }
        std::cout << "[mestre] campanha concluída. Resumo em "
                  << (fs::path(cfg.pastaResultados) / "resumo_campanha.csv").string() << std::endl;
    }
};

}  // namespace

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);
    int rank = 0, tamanho = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &tamanho);

    int status = 0;
    try {
        const Config cfg = lerConfig(argc, argv);
        if (tamanho < 2) throw std::invalid_argument("precisa de pelo menos 2 processos (1 mestre + 1 operário)");
        if (rank == 0) Mestre(cfg).executar(tamanho);
        else operario(cfg);
    } catch (const std::exception &erro) {
        if (rank == 0) std::cerr << "erro: " << erro.what() << "\n\n";
        if (rank == 0) mostrarUso();
        status = 1;
    }
    MPI_Finalize();
    return status;
}
