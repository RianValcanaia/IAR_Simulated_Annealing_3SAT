/*
    Campanha de experimentos em paralelo com MPI (mestre e operários).

    Para cada instância, resfriamento e SAmax, sobe o max-evals (10^3 ... 10^7) para toda a
    instância de uma vez e PARA assim que algum resfriamento chega a 10/10 (as 10
    execuções com 0 cláusulas não satisfeitas) num nível. As instâncias rodam uma por vez, na ordem da lista (as mais pesadas primeiro).

    - Mestre (rank 0): guarda a fila de tarefas (uma tarefa = uma execução do SA),
      entrega uma a cada operário livre, grava os arquivos e decide, quando todas as
      configurações de uma instância terminam um nível, se ela para ou sobe o próximo.
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
    std::vector<std::string> instancias = {"uf250-01", "uf100-01", "uf20-01"};   // as mais pesadas primeiro
    std::vector<int> coolings = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<int> samaxes = {1, 100};
    std::vector<long> maxiters = {1000, 10000, 100000, 1000000, 10000000};
    double tn = 0.1;
    double bitFlip = 0.0;      // fração de bits; 0 = o mínimo, 1 bit por vizinho
    int execucoes = 10;
    int pontosTrace = 1000;  // cada trace fica com ~1000 pontos
    bool instanciasJuntas = false;   // false: uma instância por vez, na ordem da lista
    std::string pastaExemplos = "exemples";
    std::string pastaResultados = "results";

    double t0DaInstancia(const std::string &nome) const {
        static const std::map<std::string, double> t0 = {{"uf20-01", 15}, {"uf100-01", 15}, {"uf250-01", 15}};
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
        "  --instances <a,b,..>   default: uf250-01,uf100-01,uf20-01 (a ordem é a prioridade)\n"
        "  --coolings <0,1,..>    default: 0 a 9\n"
        "  --samaxes <1,100>      default: 1,100\n"
        "  --maxiters <n,..>      default: 10^3 a 10^7\n"
        "  --tn <num>             default: 0.1\n"
        "  --bit-flip <num>       fração de bits por vizinho; 0 = 1 bit (default: 0)\n"
        "  --execucoes <num>      execuções por configuração (default: 10)\n"
        "  --trace-points <num>   pontos por trace (default: 1000)\n"
        "  --juntas               roda todas as instâncias ao mesmo tempo (default: uma por vez, na ordem)\n"
        "  --exemples <pasta>     pasta dos .cnf (default: exemples)\n"
        "  --results <pasta>      pasta de saída (default: results)\n";
}

Config lerConfig(int argc, char **argv) {
    Config cfg;
    for (int i = 1; i < argc; i += 2) {
        const std::string chave = argv[i];
        if (chave == "-h" || chave == "--help") { mostrarUso(); std::exit(0); }
        if (chave == "--juntas") { cfg.instanciasJuntas = true; --i; continue; }
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

// Campos de um CSV de resultado já gravado. fMelhor = -1 se o arquivo não está legível.
struct CsvExistente {
    int fMelhor = -1;
    int bits = 0;     // coluna 9
    double t0 = 0;    // coluna 5
};

CsvExistente lerCsvExistente(const fs::path &caminho) {
    CsvExistente r;
    std::ifstream entrada(caminho);
    std::string linha;
    if (!std::getline(entrada, linha) || !std::getline(entrada, linha)) return r;
    std::vector<std::string> campos;
    std::stringstream ss(linha);
    for (std::string campo; std::getline(ss, campo, ',');) campos.push_back(campo);
    if (campos.size() < 17) return r;
    try {
        r.t0 = std::stod(campos[4]);
        r.bits = std::stoi(campos[8]);
        r.fMelhor = std::stoi(campos[12]);
    } catch (...) { r.fMelhor = -1; }
    return r;
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

// Uma cadeia = (instância, cooling, samax). Em cada nível de max-evals ela roda `execucoes` execuções.
struct Cadeia {
    int instancia, cooling, samax;
    int pendentes = 0;   // execuções do nível atual ainda não terminadas
    int sucessos = 0;    // execuções do nível atual com f_melhor = 0
};

// Os níveis de max-evals andam juntos para todas as cadeias de uma instância: o nível só avança
// quando todas terminaram, e a instância PARA assim que alguma cadeia chega a 10/10 num nível.
struct EstadoInstancia {
    std::size_t nivel = 0;
    int pendentes = 0;       // execuções do nível atual (de todas as cadeias) ainda não terminadas
    bool concluida = false;
};

struct Mestre {
    const Config &cfg;
    std::vector<Cadeia> cadeias;
    std::vector<EstadoInstancia> instancias;
    std::vector<std::string> resumo;           // linhas do resumo_campanha.csv desta rodada
    std::vector<std::string> resumoAnterior;   // linhas de instâncias que não fazem parte desta rodada
    std::size_t proximaInstancia = 0;          // próxima instância a iniciar
    std::vector<int> ociosos;                  // ranks de operários livres

    struct ItemFila { Tarefa tarefa; int cadeia; };
    struct MenorPrioridade {  // instâncias na ordem da lista (as pesadas primeiro) e, dentro delas, a ordem de criação
        bool operator()(const ItemFila &a, const ItemFila &b) const {
            if (a.tarefa.instancia != b.tarefa.instancia) return a.tarefa.instancia > b.tarefa.instancia;
            return a.tarefa.id > b.tarefa.id;
        }
    };
    std::priority_queue<ItemFila, std::vector<ItemFila>, MenorPrioridade> fila;
    std::unordered_map<int, ItemFila> emVoo;   // id da tarefa -> tarefa
    uint64_t sementeBase;
    int proximoId = 0;
    // progresso: o total começa no máximo possível e diminui quando uma instância para antes do
    // último max-evals (os níveis que ela não precisou rodar saem da conta)
    long total = 0;
    long feitas = 0;

    explicit Mestre(const Config &c) : cfg(c), instancias(c.instancias.size()) {
        sementeBase = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
        for (std::size_t i = 0; i < cfg.instancias.size(); ++i)
            for (int cooling : cfg.coolings)
                for (int samax : cfg.samaxes) cadeias.push_back({static_cast<int>(i), cooling, samax});
        total = static_cast<long>(cadeias.size()) * static_cast<long>(cfg.maxiters.size()) * cfg.execucoes;
        carregarResumoAnterior();
    }

    // mantém no resumo as linhas de instâncias que não estão nesta rodada (rodadas separadas por --instances)
    void carregarResumoAnterior() {
        std::ifstream entrada(fs::path(cfg.pastaResultados) / "resumo_campanha.csv");
        std::string linha;
        std::getline(entrada, linha);   // cabeçalho
        while (std::getline(entrada, linha)) {
            const std::string nome = linha.substr(0, linha.find(','));
            if (!nome.empty() && std::find(cfg.instancias.begin(), cfg.instancias.end(), nome) == cfg.instancias.end())
                resumoAnterior.push_back(linha);
        }
    }

    // bits por vizinho esperados em cada instância (para validar resultados já gravados)
    int bitsEsperados(int instancia) const {
        const cnf::Formula f = cnf::lerArquivoDimacs(cfg.pastaExemplos + "/" + cfg.instancias[instancia] + ".cnf");
        return sa::bitsPorPorcentagem(f.numVariaveis, cfg.bitFlip);
    }

    long maxEvalsDoNivel(int instancia) const { return cfg.maxiters[instancias[instancia].nivel]; }

    fs::path pastaDaCadeia(const Cadeia &c) const {
        return pastaDaConfiguracao(cfg, cfg.instancias[c.instancia], c.cooling, c.samax, maxEvalsDoNivel(c.instancia));
    }

    int cadeiasPorInstancia() const { return static_cast<int>(cfg.coolings.size() * cfg.samaxes.size()); }

    void gravarResumo() const {
        std::ostringstream ss;
        ss << "instancia,cooling,samax,max_evals,sucessos,execucoes\n";
        for (const auto &linha : resumoAnterior) ss << linha << '\n';
        for (const auto &linha : resumo) ss << linha << '\n';
        fs::create_directories(cfg.pastaResultados);
        gravarArquivo(fs::path(cfg.pastaResultados) / "resumo_campanha.csv", ss.str().data(), ss.str().size());
    }

    // Todas as execuções de uma cadeia no nível atual terminaram.
    void fecharCadeia(const Cadeia &c) {
        const std::string &nome = cfg.instancias[c.instancia];
        const long me = maxEvalsDoNivel(c.instancia);
        resumo.push_back(nome + "," + std::to_string(c.cooling) + "," + std::to_string(c.samax) + "," +
                         std::to_string(me) + "," + std::to_string(c.sucessos) + "," + std::to_string(cfg.execucoes));
        gravarResumo();
        std::cout << "[mestre] " << nome << " cooling=" << c.cooling << " samax=" << c.samax << " max-evals=" << me
                  << ": " << c.sucessos << "/" << cfg.execucoes << " chegaram a 0" << std::endl;
    }

    int cadeiasConvergidas(int instancia) const {
        int n = 0;
        for (const Cadeia &c : cadeias)
            if (c.instancia == instancia && c.sucessos == cfg.execucoes) ++n;
        return n;
    }

    // A instância vai parar neste nível? (alguma cadeia chegou a 10/10, ou era o último nível)
    bool instanciaParaAqui(int instancia) const {
        return cadeiasConvergidas(instancia) > 0 || instancias[instancia].nivel + 1 >= cfg.maxiters.size();
    }

    // Tira do total os níveis que não serão rodados, quando a instância para antes do último.
    void descontarNiveisPulados(int instancia) {
        const EstadoInstancia &e = instancias[instancia];
        const long pulados = static_cast<long>(cfg.maxiters.size() - 1 - e.nivel) * cadeiasPorInstancia() * cfg.execucoes;
        if (pulados == 0) return;
        total -= pulados;
        std::cout << "[mestre] " << cfg.instancias[instancia] << ": " << cadeiasConvergidas(instancia)
                  << " configuração(ões) chegaram a 10/10 em max-evals=" << cfg.maxiters[e.nivel] << ", "
                  << "não sobe mais (" << pulados << " execuções saem da conta; total " << total + pulados
                  << " -> " << total << ")" << std::endl;
    }

    // Encerra o nível atual da instância. Devolve true se ela segue para o próximo nível.
    bool fecharNivel(int instancia) {
        EstadoInstancia &e = instancias[instancia];
        if (cadeiasConvergidas(instancia) > 0) { e.concluida = true; return false; }
        if (++e.nivel >= cfg.maxiters.size()) {
            std::cout << "[mestre] " << cfg.instancias[instancia] << ": nenhuma configuração chegou a "
                      << cfg.execucoes << "/" << cfg.execucoes << " até max-evals=" << cfg.maxiters.back() << std::endl;
            e.concluida = true;
            return false;
        }
        return true;
    }

    // Inicia o nível atual da instância: enfileira o que falta e conta o que já está em disco.
    // Se o nível inteiro já existia, fecha-o e segue para o próximo.
    void avancar(int instancia) {
        EstadoInstancia &e = instancias[instancia];
        const int bits = bitsEsperados(instancia);
        const double t0 = cfg.t0DaInstancia(cfg.instancias[instancia]);
        while (!e.concluida) {
            e.pendentes = 0;
            for (std::size_t k = 0; k < cadeias.size(); ++k) {
                Cadeia &c = cadeias[k];
                if (c.instancia != instancia) continue;
                c.pendentes = 0;
                c.sucessos = 0;
                const fs::path pasta = pastaDaCadeia(c);
                for (int ex = 1; ex <= cfg.execucoes; ++ex) {
                    CsvExistente existente;
                    if (fs::exists(arquivoTrace(pasta, ex)) && fs::exists(arquivoCsv(pasta, ex)))
                        existente = lerCsvExistente(arquivoCsv(pasta, ex));
                    if (existente.fMelhor >= 0 && (existente.bits != bits || existente.t0 != t0))
                        throw std::runtime_error(
                            "resultado já gravado em '" + arquivoCsv(pasta, ex).string() + "' foi feito com " +
                            std::to_string(existente.bits) + " bit(s) e T0=" + numeroParaTexto(existente.t0) +
                            ", mas esta execução usa " + std::to_string(bits) + " bit(s) e T0=" +
                            numeroParaTexto(t0) + ". Mova ou apague essa pasta de resultados antes de seguir.");
                    if (existente.fMelhor >= 0) {
                        if (existente.fMelhor == 0) ++c.sucessos;
                        ++feitas;   // já estava em disco (retomada)
                        continue;
                    }
                    Tarefa t{proximoId, c.instancia, c.cooling, c.samax, maxEvalsDoNivel(instancia), ex,
                             splitmix64(sementeBase + static_cast<uint64_t>(proximoId))};
                    ++proximoId;
                    fila.push({t, static_cast<int>(k)});
                    ++c.pendentes;
                    ++e.pendentes;
                }
                if (c.pendentes == 0) fecharCadeia(c);   // a cadeia inteira já estava em disco
            }
            if (e.pendentes > 0) return;
            if (instanciaParaAqui(instancia)) descontarNiveisPulados(instancia);
            if (!fecharNivel(instancia)) return;
        }
    }

    // Inicia as instâncias que já podem começar: todas de uma vez (--juntas) ou só a próxima, quando a
    // anterior terminou. Uma instância toda em disco conclui na hora, então o laço segue para a próxima.
    void iniciarInstancias() {
        while (proximaInstancia < cfg.instancias.size()) {
            if (!cfg.instanciasJuntas && proximaInstancia > 0 && !instancias[proximaInstancia - 1].concluida) return;
            avancar(static_cast<int>(proximaInstancia++));
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
        EstadoInstancia &e = instancias[item.tarefa.instancia];
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
        --c.pendentes;
        --e.pendentes;
        ++feitas;

        const bool nivelFechou = (e.pendentes == 0);
        // se a instância para neste nível, os níveis seguintes saem do total antes de imprimir o progresso
        if (nivelFechou && instanciaParaAqui(item.tarefa.instancia)) descontarNiveisPulados(item.tarefa.instancia);
        std::cout << feitas << "/" << total << " (faltam " << total - feitas << ")  " << cfg.instancias[item.tarefa.instancia]
                  << " cooling=" << item.tarefa.cooling << " samax=" << item.tarefa.samax
                  << " max-evals=" << item.tarefa.maxEvals << " execucao=" << item.tarefa.execucao
                  << " f_melhor=" << cab.fMelhor << std::endl;

        if (c.pendentes == 0) fecharCadeia(c);
        if (nivelFechou && fecharNivel(item.tarefa.instancia)) avancar(item.tarefa.instancia);
        if (nivelFechou && e.concluida) iniciarInstancias();   // a próxima instância pode começar
        return operario;
    }

    void executar(int tamanhoMundo) {
        for (int rank = 1; rank < tamanhoMundo; ++rank) ociosos.push_back(rank);
        std::cout << "[mestre] " << cfg.instancias.size() << " instância(s), " << cadeias.size() << " configurações, "
                  << (tamanhoMundo - 1) << " operários, seed base " << sementeBase << std::endl;

        iniciarInstancias();
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
        std::cerr.flush();
        MPI_Abort(MPI_COMM_WORLD, 1);   // o mestre parou: encerra os operários também
        status = 1;
    }
    MPI_Finalize();
    return status;
}
