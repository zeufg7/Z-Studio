#pragma once

#include <string>
#include <vector>
#include <memory>

namespace vrv { class Toolkit; }

namespace score
{
    /** Desenha MusicXML como SVG usando o Verovio (uma página alta, com quebras automáticas). */
    class Renderer
    {
    public:
        explicit Renderer (const std::string& resourcePath);
        ~Renderer();

        bool isReady() const    { return ready; }
        const std::string& getError() const    { return error; }

        /** Retorna o SVG da partitura com a largura dada (px) e escala (%). */
        std::string render (const std::string& musicXml, int widthPx, int scalePercent);

    private:
        std::unique_ptr<vrv::Toolkit> toolkit;
        bool ready = false;
        std::string error;
    };
}
