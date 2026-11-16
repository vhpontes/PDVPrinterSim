// Descrição: valida a posição inicial do conteúdo em página centralizada ou vazia.
#include "enquadramento.hpp"

int main() {
    sf::Image pagina;pagina.create(210,100,sf::Color::White);
    if (inicioConteudo(pagina)!=std::make_pair(105,0)) return 1;
    if (!medirConteudo(pagina).vazio() || medirConteudo(pagina).sobraInferiorMm(300,100)!=300) return 4;
    for (unsigned y=30;y<=34;++y) for (unsigned x=150;x<=160;++x)
        pagina.setPixel(x,y,sf::Color::Black);
    if (inicioConteudo(pagina)!=std::make_pair(155,30)) return 2;
    if (medirConteudo(pagina).pixels!=55 || medirConteudo(pagina).sobraInferiorMm(300,100)!=195) return 5;
    pagina.setPixel(10,60,sf::Color::Black);
    if (inicioConteudo(pagina)!=std::make_pair(85,30)) return 3;
    pagina.setPixel(209,99,sf::Color::Black);
    if (medirConteudo(pagina).sobraInferiorMm(300,100)!=0) return 6;
    return 0;
}
