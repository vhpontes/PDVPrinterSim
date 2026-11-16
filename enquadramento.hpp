// Descrição: localiza o conteúdo do PDF para enquadrar recortes virtuais.
#pragma once
#include <SFML/Graphics/Image.hpp>
#include <algorithm>
#include <cstddef>
#include <utility>

// O driver pode centralizar o cupom numa página maior; mantém-se a dimensão real.
struct ConteudoPagina {
    unsigned left = 0, top = 0, right = 0, bottom = 0;
    size_t pixels = 0;
    bool vazio() const { return pixels == 0; }
    float sobraInferiorMm(float alturaMm, unsigned alturaPixels) const {
        return vazio() ? alturaMm : alturaMm * (alturaPixels - 1 - bottom) / alturaPixels;
    }
};

inline ConteudoPagina medirConteudo(const sf::Image& imagem) {
    const auto size = imagem.getSize();
    const auto* pixels = imagem.getPixelsPtr();
    ConteudoPagina result; result.left = size.x; result.top = size.y;
    for (unsigned y = 0; y < size.y; ++y) for (unsigned x = 0; x < size.x; ++x) {
        const auto* p = pixels + (static_cast<size_t>(y) * size.x + x) * 4;
        if (p[3] > 0 && (p[0] < 230 || p[1] < 230 || p[2] < 230)) {
            result.left = std::min(result.left, x); result.right = std::max(result.right, x);
            result.top = std::min(result.top, y); result.bottom = std::max(result.bottom, y); ++result.pixels;
        }
    }
    return result;
}

inline std::pair<int,int> inicioConteudo(const sf::Image& imagem) {
    auto c = medirConteudo(imagem);
    return !c.vazio() ? std::make_pair(static_cast<int>((c.left+c.right)/2),static_cast<int>(c.top))
                     : std::make_pair(static_cast<int>(imagem.getSize().x/2),0);
}
