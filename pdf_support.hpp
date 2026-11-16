// Descrição: converte PDF do driver virtual em imagens, preservando o tamanho real da página.
#pragma once
#include <SFML/Graphics/Image.hpp>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

struct PdfPageView {
    sf::Image image;
    std::string format = "PDF";
    float sobraInferiorMm = 0;
    bool vazio = false;
    float widthMm = 0;
    float heightMm = 0;
};

inline std::vector<PdfPageView> renderPdf(const std::vector<unsigned char>& data) {
    HMODULE lib = LoadLibraryW(L"pdfium.dll");
    if (!lib) throw std::runtime_error("PDFium indisponivel na pasta do simulador.");
    auto symbol = [lib](const char* name) {
        auto p = GetProcAddress(lib, name);
        if (!p) throw std::runtime_error(std::string("PDFium sem funcao ") + name);
        return p;
    };
    using Init = void (*)();
    using Load = void* (*)(const void*, size_t, const char*);
    using Count = int (*)(void*);
    using Page = void* (*)(void*, int);
    using Measure = float (*)(void*);
    using Bitmap = void* (*)(int, int, int, void*, int);
    using Fill = int (*)(void*, int, int, int, int, unsigned long);
    using Render = void (*)(void*, void*, int, int, int, int, int, int);
    using Close = void (*)(void*);
    auto init = reinterpret_cast<Init>(symbol("FPDF_InitLibrary"));
    auto destroy = reinterpret_cast<Init>(symbol("FPDF_DestroyLibrary"));
    auto load = reinterpret_cast<Load>(symbol("FPDF_LoadMemDocument64"));
    auto count = reinterpret_cast<Count>(symbol("FPDF_GetPageCount"));
    auto loadPage = reinterpret_cast<Page>(symbol("FPDF_LoadPage"));
    auto width = reinterpret_cast<Measure>(symbol("FPDF_GetPageWidthF"));
    auto height = reinterpret_cast<Measure>(symbol("FPDF_GetPageHeightF"));
    auto makeBitmap = reinterpret_cast<Bitmap>(symbol("FPDFBitmap_CreateEx"));
    auto fill = reinterpret_cast<Fill>(symbol("FPDFBitmap_FillRect"));
    auto render = reinterpret_cast<Render>(symbol("FPDF_RenderPageBitmap"));
    auto closeBitmap = reinterpret_cast<Close>(symbol("FPDFBitmap_Destroy"));
    auto closePage = reinterpret_cast<Close>(symbol("FPDF_ClosePage"));
    auto closeDoc = reinterpret_cast<Close>(symbol("FPDF_CloseDocument"));
    init();
    void* doc = load(data.data(), data.size(), nullptr);
    if (!doc) { destroy(); FreeLibrary(lib); throw std::runtime_error("PDF recebido nao pode ser aberto."); }
    std::vector<PdfPageView> pages;
    try {
        int n = count(doc);
        if (n < 1 || n > 20) throw std::runtime_error("PDF excede o limite de 20 paginas.");
        for (int i = 0; i < n; ++i) {
            void* page = loadPage(doc, i);
            if (!page) throw std::runtime_error("Pagina PDF invalida.");
            float w = width(page), h = height(page);
            if (!std::isfinite(w) || !std::isfinite(h) || w <= 0 || h <= 0 || w > 4000 || h > 20000) {
                closePage(page); throw std::runtime_error("Tamanho de pagina PDF invalido.");
            }
            float scale = std::min(2.f, std::min(1600.f / w, 10000.f / h));
            int pw = std::max(1, static_cast<int>(std::ceil(w * scale)));
            int ph = std::max(1, static_cast<int>(std::ceil(h * scale)));
            if (static_cast<int64_t>(pw) * ph > 12000000) {
                closePage(page); throw std::runtime_error("Pagina PDF grande demais para o simulador.");
            }
            std::vector<unsigned char> pixels(static_cast<size_t>(pw) * ph * 4, 255);
            void* bitmap = makeBitmap(pw, ph, 4, pixels.data(), pw * 4); // BGRA
            if (!bitmap) { closePage(page); throw std::runtime_error("Falha ao preparar pagina PDF."); }
            fill(bitmap, 0, 0, pw, ph, 0xffffffffUL);
            render(bitmap, page, 0, 0, pw, ph, 0, 0);
            closeBitmap(bitmap); closePage(page);
            for (size_t p = 0; p < pixels.size(); p += 4) std::swap(pixels[p], pixels[p + 2]);
            PdfPageView out; out.widthMm = w * 25.4f / 72.f; out.heightMm = h * 25.4f / 72.f;
            out.image.create(static_cast<unsigned>(pw), static_cast<unsigned>(ph), pixels.data());
            pages.push_back(std::move(out));
        }
    } catch (...) { closeDoc(doc); destroy(); FreeLibrary(lib); throw; }
    closeDoc(doc); destroy(); FreeLibrary(lib);
    return pages;
}
