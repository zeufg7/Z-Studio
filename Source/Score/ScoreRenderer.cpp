#include "ScoreRenderer.h"

#include "toolkit.h"
#include "vrv.h"

#include <algorithm>

namespace score
{
Renderer::Renderer (const std::string& resourcePath)
{
    vrv::EnableLog (vrv::LOG_ERROR);
    vrv::EnableLogToBuffer (true);
    vrv::logBuffer.clear();

    toolkit = std::make_unique<vrv::Toolkit> (false);
    ready = toolkit->SetResourcePath (resourcePath);

    if (! ready)
    {
        for (auto& line : vrv::logBuffer)
            error += line + " ";

        if (error.empty())
            error = "falha ao carregar as fontes";
    }

    vrv::logBuffer.clear();
    vrv::EnableLogToBuffer (false);
}

Renderer::~Renderer() = default;

std::string Renderer::render (const std::string& musicXml, int widthPx, int scalePercent)
{
    if (! ready)
        return {};

    scalePercent = std::clamp (scalePercent, 10, 200);
    const int pageWidth = std::clamp (widthPx * 100 / scalePercent, 500, 100000);

    const std::string options = "{"
        "\"inputFrom\": \"musicxml\","
        "\"pageWidth\": " + std::to_string (pageWidth) + ","
        "\"pageHeight\": 60000,"
        "\"adjustPageHeight\": true,"
        "\"breaks\": \"auto\","
        "\"scale\": " + std::to_string (scalePercent) + ","
        "\"header\": \"none\","
        "\"footer\": \"none\","
        "\"pageMarginLeft\": 60, \"pageMarginRight\": 60, \"pageMarginTop\": 60, \"pageMarginBottom\": 60,"
        "\"font\": \"Bravura\","
        "\"smuflTextFont\": \"none\","
        "\"svgViewBox\": false,"
        "\"svgRemoveXlink\": true"
        "}";

    toolkit->SetOptions (options);

    if (! toolkit->LoadData (musicXml))
        return {};

    return toolkit->RenderToSVG (1, false);
}
}
