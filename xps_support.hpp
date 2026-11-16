// Descrição: leitura XPS isolada com timeout; preserva escala e página do arquivo original.
#pragma once
#include "pdf_support.hpp"
#include <filesystem>
#include <fstream>
#include <locale>

inline std::vector<PdfPageView> renderXps(const std::filesystem::path& path) {
    namespace fs = std::filesystem;
    wchar_t own[MAX_PATH];
    DWORD length = GetModuleFileNameW(nullptr, own, MAX_PATH);
    if (!length || length >= MAX_PATH) throw std::runtime_error("Caminho do simulador indisponivel.");
    fs::path helper = fs::path(own).parent_path() / "RenderXps.exe";
    std::wstring command = L"\"" + helper.wstring() + L"\" \"" + fs::absolute(path).wstring() + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, helper.parent_path().c_str(), &startup, &process))
        throw std::runtime_error("Nao foi possivel iniciar o renderizador XPS.");
    DWORD wait = WaitForSingleObject(process.hProcess, 30000), code = 1;
    if (wait != WAIT_OBJECT_0) { TerminateProcess(process.hProcess, 1); WaitForSingleObject(process.hProcess, 2000); }
    else GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    if (code != 0 || wait != WAIT_OBJECT_0) throw std::runtime_error("XPS invalido ou renderizacao excedeu o limite; original preservado.");
    fs::path root = path; root.replace_extension();
    std::ifstream metadata(root.string() + ".medidas"); metadata.imbue(std::locale::classic());
    int count = 0; metadata >> count;
    if (!metadata || count < 1 || count > 20) throw std::runtime_error("Metadados XPS invalidos.");
    std::vector<PdfPageView> pages;
    for (int i = 1; i <= count; ++i) {
        PdfPageView page; page.format = "XPS";
        metadata >> page.widthMm >> page.heightMm;
        if (!metadata || !std::isfinite(page.widthMm) || !std::isfinite(page.heightMm) || page.widthMm <= 0 || page.heightMm <= 0)
            throw std::runtime_error("Dimensoes XPS invalidas.");
        if (!page.image.loadFromFile(root.string() + "-p" + std::to_string(i) + ".png"))
            throw std::runtime_error("Preview XPS indisponivel.");
        pages.push_back(std::move(page));
    }
    return pages;
}
