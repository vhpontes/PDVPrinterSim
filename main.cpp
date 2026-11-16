// Descrição: PDVPrinterSim com captura local de texto ESC/POS, PDF e XPS.
// Derivado de PDVPrinterSim, copyright (c) 2025 Victor Hugo Manata Pontes; licença MIT em LICENSE.
#include <SFML/Graphics.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <memory>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <utility>
#include "enquadramento.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include "pdf_support.hpp"
#pragma comment(lib, "ws2_32.lib")

#define DEFAULT_PORT "9100"
#define DEFAULT_BUFLEN 512

// Estrutura para guardar o estado atual da formatação da impressora
struct PrinterState {
    enum Justification { LEFT, CENTER, RIGHT };
    Justification justification = LEFT;
    bool isBold = false;
    bool isUnderlined = false;
    int widthMultiplier = 1;
    int heightMultiplier = 1;
};

// Estrutura para enviar para a fila: contém o texto e a formatação daquela linha
struct PrintLine {
    std::string text;
    std::string leftText;
    std::string rightText; // Usado para a segunda coluna (ex: valor)    
    PrinterState state; // Uma cópia do estado no momento da impressão da linha
};

// --- DADOS GLOBAIS PARA COMUNICAÇÃO ENTRE THREADS ---
std::queue<PrintLine> g_printQueue; 
std::mutex g_queueMutex;
// ----------------------------------------------------

// Adicione estas duas funções antes de run_server()

// Retorna os dados do primeiro recibo de teste
std::string getTestReceipt1() {
    std::string msg;
    // O PHP agora envia os dados separados por '|' para o C++ alinhar
    msg += "\x1B\x61\x01"; // Centralizar
    msg += "\x1D\x21\x11"; // Fonte Grande
    msg += "CAFE DO BOM\n";
    msg += "\x1B\x40"; // Resetar
    msg += "----------------------------------------\n";
    msg += "ITEM|VALOR (R$)\n";
    msg += "Aluguel Quadra - Hora|100,00\n";
    msg += "----------------------------------------\n";
    msg += "\x1B\x45\x01"; // Negrito ON
    msg += "TOTAL:|R$ 100,00\n";
    msg += "\x1B\x40"; // Resetar
    msg += "\x1B\x61\x01"; // Centralizar
    msg += "Prefeitura Municipal de Pocos de Caldas\n";
    msg += "18.722.710/0001-80\n\n\n";
    msg += "\x1D\x56\x01"; // Cortar Papel
    return msg;
}

// Retorna os dados do segundo recibo de teste
std::string getTestReceipt2() {
    std::string msg;
    msg += "PROXIMA VENDA\n";
    msg += "Agua Mineral          1    R$ 4,00\n";
    msg += "\n\n\n"; // Pula algumas linhas
    msg += "\x1D\x56\x01"; // GS V 1 (Cortar Papel)
    return msg;
}

// Adicione esta função também, ela será o nosso "PowerShell" interno
void sendDataToServer(const std::string& data) {
    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "[CLIENTE_TESTE] Erro ao criar socket." << std::endl;
        return;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(9100);
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    if (connect(clientSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "[CLIENTE_TESTE] Nao foi possivel conectar ao servidor. O servidor esta rodando?" << std::endl;
        closesocket(clientSocket);
        return;
    }

    send(clientSocket, data.c_str(), data.length(), 0);
    
    closesocket(clientSocket);
}

#include "network.hpp"



int main(int argc, char** argv) {
    wchar_t exePath[32768]{};
    DWORD chars = GetModuleFileNameW(nullptr, exePath, 32768);
    if (chars == 0 || chars >= 32768) return 1;
    std::filesystem::current_path(std::filesystem::path(exePath).parent_path());
    if (argc > 1 && std::string(argv[1]) == "--headless") {
        g_headless = true; run_server(); return g_listening ? 0 : 1;
    }
    std::thread serverThread(run_server);
    serverThread.detach();

    // --- CONFIGURAÇÃO GRÁFICA ---
    sf::Font font;
    wchar_t windowsDirectory[MAX_PATH]{};
    if (!GetWindowsDirectoryW(windowsDirectory, MAX_PATH) ||
        !font.loadFromFile((std::filesystem::path(windowsDirectory) / "Fonts" / "consola.ttf").string())) {
        std::cerr << "[GRAFICOS] Erro: Nao foi possivel carregar a fonte." << std::endl;
        return -1;
    }
    std::cout << "[GRAFICOS] Fonte carregada com sucesso." << std::endl;

    sf::Texture printerTexture;
    if (!printerTexture.loadFromFile("assets/printer2.png")) {
        std::cerr << "[GRAFICOS] Erro: Nao foi possivel carregar a imagem 'printer2.png'." << std::endl;
        return -1;
    }
    sf::Sprite printerSprite(printerTexture);
    
    float larguraDesejada = 581.0f; // <-- MUDE A LARGURA AQUI
    float alturaDesejada = 521.0f;  // <-- MUDE A ALTURA AQUI

    float fatorEscalaX = larguraDesejada / printerTexture.getSize().x;
    float fatorEscalaY = alturaDesejada / printerTexture.getSize().y;

    printerSprite.setScale(fatorEscalaX, fatorEscalaY);

    sf::RenderWindow window(sf::VideoMode(larguraDesejada, alturaDesejada + 100), "PDVPrinterSim - PDF / XPS / ESC-POS");
    window.setFramerateLimit(60);

    //sf::RectangleShape paper(sf::Vector2f(300, 210));
    sf::RectangleShape paper(sf::Vector2f(384, 315));
    paper.setFillColor(sf::Color::White);
    paper.setPosition(140, 79);

    std::vector<std::pair<sf::Text, sf::Text>> lines;
    unsigned int characterSize = 14;
    float paperLeftMargin = paper.getPosition().x + 5;
    float paperRightMargin = paper.getPosition().x + paper.getSize().x - 5;
    float paperTopMargin = paper.getPosition().y + 5;

    // --- BOTÃO DE TESTE ---
    sf::RectangleShape testButton(sf::Vector2f(200, 40));
    testButton.setFillColor(sf::Color(100, 100, 100));
    testButton.setPosition((window.getSize().x - testButton.getSize().x) / 2.0f, 560);

    sf::Text buttonText;
    buttonText.setFont(font);
    buttonText.setString("Imprimir Teste");
    buttonText.setCharacterSize(20);
    buttonText.setFillColor(sf::Color::White);
    buttonText.setPosition(
        testButton.getPosition().x + (testButton.getSize().x - buttonText.getLocalBounds().width) / 2.0f,
        testButton.getPosition().y + (testButton.getSize().y - buttonText.getLocalBounds().height) / 2.0f - 5
    );    

    // --- NOVAS VARIÁVEIS PARA ANIMAÇÃO ---
    sf::Clock deltaClock;
    float scrollTarget = 0.0f;
    float animationSpeed = 120.0f;
    const float printHeadY = paper.getPosition().y + paper.getSize().y - 60; 
    
    bool receiptHasBeenCut = false;
    std::shared_ptr<std::vector<PdfPageView>> pdfPages;
    sf::Texture pdfTexture;
    sf::RenderTexture paperCanvas;
    if (!paperCanvas.create(384,315)) return 1;
    size_t pageIndex = 0;
    float pdfScroll = 0.f, targetWidth = 0.f;
    int cropLeft = -1;
    sf::Text status("Aguardando impressao local", font, 12);
    status.setFillColor(sf::Color::White); status.setPosition(12, 500);
    sf::Text aviso("Porta RAW local: 127.0.0.1:9100", font, 12);
    aviso.setFillColor(sf::Color(255,205,135)); aviso.setPosition(12, 518);
    sf::Text help("Mouse=rolar  setas=paginas  0=original  5/8=recorte virtual", font, 11);
    help.setFillColor(sf::Color::White); help.setPosition(12, 538);
    
    // --- LOOP PRINCIPAL DA APLICAÇÃO ---
    while (window.isOpen()) {
        sf::Time dt = deltaClock.restart();
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed)
                window.close();

            if (event.type == sf::Event::MouseWheelScrolled && pdfPages) {
                pdfScroll = std::max(0.f, pdfScroll - event.mouseWheelScroll.delta * 65.f);
            }
            if (event.type == sf::Event::KeyPressed && pdfPages) {
                auto key = event.key.code;
                if (key == sf::Keyboard::Num5 || key == sf::Keyboard::Numpad5) { targetWidth = 58.f; pdfScroll = 0; cropLeft = -1; }
                if (key == sf::Keyboard::Num8 || key == sf::Keyboard::Numpad8) { targetWidth = 80.f; pdfScroll = 0; cropLeft = -1; }
                if (key == sf::Keyboard::Num0 || key == sf::Keyboard::Numpad0) { targetWidth = 0.f; pdfScroll = 0; cropLeft = -1; }
                if (targetWidth > 0 && (key == sf::Keyboard::A || key == sf::Keyboard::D)) {
                    int passo = std::max(1, static_cast<int>(pdfTexture.getSize().x / 25));
                    cropLeft = std::max(0, cropLeft + (key == sf::Keyboard::A ? -passo : passo));
                }
                if (key == sf::Keyboard::Right && pageIndex + 1 < pdfPages->size()) { ++pageIndex; pdfTexture.loadFromImage((*pdfPages)[pageIndex].image); pdfScroll = 0; cropLeft = -1; }
                if (key == sf::Keyboard::Left && pageIndex > 0) { --pageIndex; pdfTexture.loadFromImage((*pdfPages)[pageIndex].image); pdfScroll = 0; cropLeft = -1; }
            }
            if (event.type == sf::Event::MouseButtonPressed) {
                if (event.mouseButton.button == sf::Mouse::Left) {
                    sf::Vector2f mousePos = window.mapPixelToCoords(sf::Mouse::getPosition(window));
                    if (testButton.getGlobalBounds().contains(mousePos)) {
                        std::cout << "[BOTAO] Botao de teste clicado!" << std::endl;
                        
                        std::thread testThread([]() {
                            sendDataToServer(getTestReceipt1());
                            std::this_thread::sleep_for(std::chrono::seconds(2));
                            //sendDataToServer(getTestReceipt2());
                        });
                        testThread.detach();
                    }
                }
            }
        }
        
        // --- LÓGICA DE PROCESSAMENTO DE DADOS (ATUALIZADA) ---
        PrintLine lineData;
        bool hasData = false;
        {
            std::lock_guard<std::mutex> lock(g_queueMutex);
            if (!g_printQueue.empty()) {
                lineData = g_printQueue.front();
                g_printQueue.pop();
                hasData = true;
            }
        }

        if (hasData && lineData.text == "--[PDF]--") {
            std::lock_guard<std::mutex> lock(g_queueMutex);
            if (!g_pdfQueue.empty()) {
                pdfPages = g_pdfQueue.front(); g_pdfQueue.pop(); pageIndex = 0; pdfScroll = 0; targetWidth = 0; cropLeft = -1;
                pdfTexture.loadFromImage((*pdfPages)[0].image); lines.clear(); receiptHasBeenCut = false;
            }
        } else if (hasData && lineData.text == "--[NEW]--") {
            pdfPages.reset(); lines.clear(); receiptHasBeenCut = false;
        } else if (hasData) {
            if (receiptHasBeenCut) {
                lines.clear();
                receiptHasBeenCut = false;
            }

            // Lida com o comando de corte de forma visual
            if (lineData.text == "--[CUT]--") {
                receiptHasBeenCut = true;
                lineData.text = "--------------------------------"; // Substitui o comando por uma linha visual
                lineData.state.justification = PrinterState::CENTER; // Centraliza a linha de corte
            }
            else {
                sf::Text leftText, rightText;
                sf::Text newLine;
                float textWidth = newLine.getGlobalBounds().width;
                float xPos = paperLeftMargin;

                // Configura ambos os textos (esquerda e direita) com o mesmo estilo
                sf::Uint32 style = sf::Text::Regular;
                if (lineData.state.isBold) style |= sf::Text::Bold;
                if (lineData.state.isUnderlined) style |= sf::Text::Underlined;
                sf::Vector2f scale(lineData.state.widthMultiplier, lineData.state.heightMultiplier);                

                // Configura o texto da esquerda
                leftText.setFont(font);
                leftText.setString(lineData.leftText);
                leftText.setCharacterSize(characterSize);
                leftText.setFillColor(sf::Color::Black);
                leftText.setStyle(style);
                leftText.setScale(scale);

                // Configura o texto da direita, se existir
                if (!lineData.rightText.empty()) {
                    rightText.setFont(font);
                    rightText.setString(lineData.rightText);
                    rightText.setCharacterSize(characterSize);
                    rightText.setFillColor(sf::Color::Black);
                    rightText.setStyle(style);
                    rightText.setScale(scale);
                }

                // --- LÓGICA DE POSICIONAMENTO COM ALTURA FIXA E VARIÁVEL ---
                float yPos = paperTopMargin;
                float currentLineHeight = std::max(leftText.getGlobalBounds().height, rightText.getGlobalBounds().height);

                if (!lines.empty()) {
                    const auto& lastLine = lines.back().first;
                    float lastLineHeight = lastLine.getGlobalBounds().height;

                    // Lógica para adicionar espaço extra após linhas grandes
                    if (lastLine.getScale().y > 1.0f) {
                        lastLineHeight += 10.0f; // Adiciona um respiro
                    } else {
                        // Para linhas normais, garante uma altura mínima para evitar sobreposição
                        lastLineHeight = std::max(lastLineHeight, 20.0f);
                    }
                    yPos = lastLine.getPosition().y + lastLineHeight;
                }
                newLine.setPosition(xPos, yPos);

                // Posiciona a parte esquerda do texto
                float xPosLeft = paperLeftMargin;
                if (lineData.state.justification == PrinterState::CENTER) {
                    xPosLeft = paper.getPosition().x + (paper.getSize().x - leftText.getGlobalBounds().width) / 2.0f;
                } else if (lineData.state.justification == PrinterState::RIGHT) {
                    xPosLeft = paperRightMargin - leftText.getGlobalBounds().width;
                }
                leftText.setPosition(xPosLeft, yPos);

                // Posiciona a parte direita do texto (sempre alinhada à direita)
                if (!lineData.rightText.empty()) {
                    rightText.setPosition(paperRightMargin - rightText.getGlobalBounds().width, yPos);
                }

                // Lógica de rolagem instantânea
                float paperBottom = paper.getPosition().y + paper.getSize().y;
                if (yPos + currentLineHeight > paperBottom) {
                    float scrollOffset = (yPos + currentLineHeight) - paperBottom;
                    for (auto& linePair : lines) {
                        linePair.first.move(0, -scrollOffset);
                        linePair.second.move(0, -scrollOffset);
                    }
                    leftText.move(0, -scrollOffset);
                    rightText.move(0, -scrollOffset);
                }
                lines.push_back({leftText, rightText});
            }
        }

        // --- SEÇÃO DE DESENHO ATUALIZADA ---
        window.clear(sf::Color(50, 50, 50));
        window.draw(printerSprite);
        window.draw(paper);
        if (pdfPages && !pdfPages->empty()) {
            const auto& page = (*pdfPages)[pageIndex];
            unsigned iw = pdfTexture.getSize().x, ih = pdfTexture.getSize().y;
            float shownMm = targetWidth > 0 ? std::min(targetWidth, page.widthMm) : page.widthMm;
            int crop = std::max(1, std::min(static_cast<int>(iw), static_cast<int>(std::round(iw * shownMm / page.widthMm))));
            float scale = 384.f / crop;
            if (cropLeft < 0) {
                auto conteudo = inicioConteudo(page.image);
                cropLeft = conteudo.first - crop / 2;
                pdfScroll = std::max(0.f, conteudo.second * scale - 10.f);
            }
            cropLeft = std::clamp(cropLeft, 0, static_cast<int>(iw) - crop);
            pdfScroll = std::min(pdfScroll, std::max(0.f, ih * scale - 315.f));
            sf::Sprite image(pdfTexture, sf::IntRect(cropLeft, 0, crop, static_cast<int>(ih)));
            image.setScale(scale, scale); image.setPosition(0, -pdfScroll);
            paperCanvas.clear(sf::Color::White); paperCanvas.draw(image); paperCanvas.display();
            sf::Sprite canvas(paperCanvas.getTexture()); canvas.setPosition(paper.getPosition()); window.draw(canvas);
            std::ostringstream label; label << page.format << " real: " << static_cast<int>(std::round(page.widthMm)) << " x "
                << static_cast<int>(std::round(page.heightMm)) << " mm | pagina " << (pageIndex + 1) << "/" << pdfPages->size();
            if (targetWidth > 0 && targetWidth < page.widthMm) label << " | corte virtual " << static_cast<int>(targetWidth) << " mm";
            status.setString(label.str());
            std::ostringstream diag;
            if (page.vazio) diag << "PAGINA EM BRANCO - " << static_cast<int>(page.heightMm) << " mm de papel";
            else diag << "Papel apos o conteudo: " << static_cast<int>(std::round(page.sobraInferiorMm)) << " mm (arquivo original)";
            aviso.setString(diag.str());
        } else {
            for (const auto& linePair : lines) {
                if(linePair.first.getGlobalBounds().intersects(paper.getGlobalBounds())) window.draw(linePair.first);
                if(linePair.second.getGlobalBounds().intersects(paper.getGlobalBounds())) window.draw(linePair.second);
            }
            status.setString(g_listening ? "Aguardando trabalho de impressao em 127.0.0.1:9100" : "Iniciando servidor local...");
            aviso.setString("Porta RAW local: 127.0.0.1:9100");
        }
        { std::lock_guard<std::mutex> lock(g_queueMutex); if (!g_serverError.empty()) status.setString(g_serverError); }
        window.draw(status); window.draw(aviso); window.draw(help);
        window.draw(testButton);
        window.draw(buttonText);
        window.display();
    }
    return 0;
}
