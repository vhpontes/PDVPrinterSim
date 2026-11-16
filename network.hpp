// Descrição: recebe trabalhos completos em loopback, registra texto ESC/POS, PDF e XPS.
#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include "xps_support.hpp"

std::queue<std::shared_ptr<std::vector<PdfPageView>>> g_pdfQueue;
std::atomic<bool> g_listening(false);
std::string g_serverError;
bool g_headless = false;

inline void serverError(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_queueMutex); g_serverError = message;
}

inline void queueLine(const std::string& text, const PrinterState& state) {
    if (g_headless) return;
    PrintLine line; line.state = state; line.text = text;
    size_t split = text.find('|');
    if (split == std::string::npos) line.leftText = text;
    else { line.leftText = text.substr(0, split); line.rightText = text.substr(split + 1); }
    std::lock_guard<std::mutex> lock(g_queueMutex); g_printQueue.push(line);
}

inline std::string safeName() {
    static std::atomic<unsigned long> sequence(0);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return "trabalho-" + std::to_string(ms) + "-" + std::to_string(++sequence);
}

inline void writeBytes(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Nao foi possivel gravar captura local.");
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!f) throw std::runtime_error("Captura local incompleta.");
}

inline void processJob(const std::vector<unsigned char>& bytes) {
    namespace fs = std::filesystem;
    fs::create_directories("capturas");
    const std::string id = safeName();
    const bool pdf = bytes.size() >= 5 && std::equal(bytes.begin(), bytes.begin() + 5, "%PDF-");
    const bool xps = bytes.size() >= 4 && bytes[0] == 'P' && bytes[1] == 'K' && bytes[2] == 3 && bytes[3] == 4;
    std::ostringstream info;
    info << "{\"id\":\"" << id << "\",\"bytes\":" << bytes.size();
    if (pdf || xps) {
        auto original = fs::path("capturas") / (id + (xps ? ".xps" : ".pdf"));
        writeBytes(original, bytes);
        auto pages = std::make_shared<std::vector<PdfPageView>>(xps ? renderXps(original) : renderPdf(bytes));
        for (size_t i = 0; i < pages->size(); ++i) {
            auto path = fs::path("capturas") / (id + "-p" + std::to_string(i + 1) + ".png");
            if (!(*pages)[i].image.saveToFile(path.string())) throw std::runtime_error("Falha ao salvar preview da pagina.");
        }
        info << ",\"formato\":\"" << (xps ? "xps" : "pdf") << "\",\"paginas\":" << pages->size() << ",\"medidas_mm\":[";
        for (size_t i = 0; i < pages->size(); ++i) {
            if (i) info << ',';
            auto& page = (*pages)[i];
            auto c = medirConteudo(page.image);
            page.vazio = c.vazio(); page.sobraInferiorMm = c.sobraInferiorMm(page.heightMm, page.image.getSize().y);
            info << "{\"largura\":" << page.widthMm << ",\"altura\":" << page.heightMm
                 << ",\"vazio\":" << (page.vazio ? "true" : "false")
                 << ",\"sobraInferiorMm\":" << page.sobraInferiorMm << '}';
        }
        info << ']';
        if (!g_headless) {
            std::lock_guard<std::mutex> lock(g_queueMutex);
            g_pdfQueue.push(pages); PrintLine marker; marker.text = "--[PDF]--"; g_printQueue.push(marker);
        }
    } else {
        writeBytes(fs::path("capturas") / (id + ".raw"), bytes);
        PrinterState state; std::string line; std::vector<std::string> lines;
        queueLine("--[NEW]--", state);
        auto flush = [&]() { lines.push_back(line); queueLine(line, state); line.clear(); };
        for (size_t i = 0; i < bytes.size(); ++i) {
            unsigned char c = bytes[i];
            if (c == 0x1b && i + 1 < bytes.size()) {
                unsigned char cmd = bytes[++i];
                if (cmd == '@') state = PrinterState();
                else if ((cmd == 'a' || cmd == 'E' || cmd == '-') && i + 1 < bytes.size()) {
                    unsigned char arg = bytes[++i];
                    if (cmd == 'a') state.justification = (arg == 1 || arg == 49) ? PrinterState::CENTER : (arg == 2 || arg == 50) ? PrinterState::RIGHT : PrinterState::LEFT;
                    if (cmd == 'E') state.isBold = arg != 0;
                    if (cmd == '-') state.isUnderlined = arg != 0;
                }
            } else if (c == 0x1d && i + 1 < bytes.size()) {
                unsigned char cmd = bytes[++i];
                if (cmd == '!' && i + 1 < bytes.size()) {
                    unsigned char arg = bytes[++i]; state.widthMultiplier = (arg & 15) + 1; state.heightMultiplier = ((arg >> 4) & 15) + 1;
                } else if (cmd == 'V') {
                    if (!line.empty()) flush();
                    if (i + 1 < bytes.size()) {
                        unsigned char mode = bytes[++i];
                        if ((mode == 65 || mode == 66) && i + 1 < bytes.size()) ++i;
                    }
                    queueLine("--[CUT]--", state);
                }
            } else if (c == '\n') flush();
            else if (c == '\f') { if (!line.empty()) flush(); queueLine("--[CUT]--", state); }
            else if (c != '\r' && c != 0) line.push_back(static_cast<char>(c));
        }
        if (!line.empty()) flush();
        std::ofstream out(fs::path("capturas") / (id + ".txt"), std::ios::binary);
        for (const auto& entry : lines) out << entry << '\n';
        if (!out) throw std::runtime_error("Falha ao salvar texto capturado.");
        info << ",\"formato\":\"texto\",\"linhas\":" << lines.size();
    }
    info << '}';
    std::ofstream last(fs::path("capturas") / "ultimo.json", std::ios::binary | std::ios::trunc);
    last << info.str();
    if (!last) throw std::runtime_error("Falha ao salvar estado da captura.");
}

void run_server() {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) { serverError("Falha ao iniciar rede local."); return; }
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) { serverError("Falha ao criar porta local."); WSACleanup(); return; }
    BOOL exclusive = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(9100);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR || listen(listener, 8) == SOCKET_ERROR) {
        serverError("Porta 9100 ocupada; feche o outro simulador."); closesocket(listener); WSACleanup(); return;
    }
    g_listening = true;
    while (true) {
        SOCKET client = accept(listener, nullptr, nullptr);
        if (client == INVALID_SOCKET) break;
        DWORD timeout = 15000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        std::vector<unsigned char> bytes; char buf[65536]; int count;
        bool oversized = false;
        while ((count = recv(client, buf, sizeof(buf), 0)) > 0) {
            if (bytes.size() + static_cast<size_t>(count) > 32u * 1024u * 1024u) { oversized = true; break; }
            bytes.insert(bytes.end(), buf, buf + count);
        }
        closesocket(client);
        if (oversized || count == SOCKET_ERROR) {
            serverError(oversized ? "Trabalho excede o limite de 32 MiB." : "Conexao interrompida ou timeout; trabalho incompleto descartado.");
            continue;
        }
        if (!bytes.empty()) {
            try { processJob(bytes); serverError(""); }
            catch (const std::exception& e) { serverError(e.what()); }
        }
    }
    closesocket(listener); WSACleanup(); g_listening = false;
}
