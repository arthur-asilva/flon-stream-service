#include "WellMasker.hpp"

#include "Logger.hpp"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
const std::unordered_map<int, std::string> layoutFiles =
{
{6, PROJECT_ROOT_DIR "/config/wells_6.json"},
{12, PROJECT_ROOT_DIR "/config/wells_12.json"},
{24, PROJECT_ROOT_DIR "/config/wells_24.json"},
{96, PROJECT_ROOT_DIR "/config/wells_96.json"}
};
}

bool WellMasker::initialize()
{
layouts_.clear();

int loadedLayouts = 0;

for (const auto& [wellsCount, path] : layoutFiles)
{
    if (loadLayout(wellsCount))
    {
        loadedLayouts++;
    }
}

std::cout
    << "[MÁSCARA] "
    << loadedLayouts
    << " layout(s) carregado(s).\n";

return loadedLayouts > 0;

}

bool WellMasker::loadLayout(int wellsCount)
{
auto fileIt = layoutFiles.find(wellsCount);

if (fileIt == layoutFiles.end())
{
    return false;
}

std::ifstream file(fileIt->second);

// Arquivo inexistente não é erro.
// Apenas significa que este layout ainda não foi criado.
if (!file.is_open())
{
    return false;
}

try
{
    json layoutJson;
    file >> layoutJson;

    WellLayout layout;

    layout.referenceWidth =
        layoutJson.at("reference_width").get<int>();

    layout.referenceHeight =
        layoutJson.at("reference_height").get<int>();

    layout.radius =
        layoutJson.at("radius").get<int>();

    for (const auto& item : layoutJson.at("wells"))
    {
        Well well;

        well.id = item.at("id").get<int>();
        well.x = item.at("x").get<int>();
        well.y = item.at("y").get<int>();

        layout.wells.push_back(well);
    }

    layout.mask = cv::Mat::zeros(
        layout.referenceHeight,
        layout.referenceWidth,
        CV_8UC1
    );

    for (const Well& well : layout.wells)
    {
        cv::circle(
            layout.mask,
            cv::Point(well.x, well.y),
            layout.radius,
            cv::Scalar(255),
            cv::FILLED,
            cv::LINE_AA
        );
    }

    layouts_[wellsCount] = std::move(layout);

    std::cout
        << "[MÁSCARA] Layout "
        << wellsCount
        << " carregado ("
        << layouts_[wellsCount].wells.size()
        << " poços).\n";

    return true;
}
catch (const std::exception& e)
{
    Logger::instance().logError(
        "[MASCARA] Erro ao ler " + fileIt->second + ": " + e.what()
    );

    return false;
}

}

bool WellMasker::apply(cv::Mat& frame, int wellsCount)
{
if (frame.empty())
{
return false;
}

if (wellsCount == 0)
{
    return true;
}

auto layoutIt = layouts_.find(wellsCount);

if (layoutIt == layouts_.end())
{
    Logger::instance().logError(
        "[MASCARA] Layout para " + std::to_string(wellsCount) +
        " pocos nao foi carregado."
    );

    return false;
}


cv::Mat maskedFrame;

cv::bitwise_and(
    frame,
    frame,
    maskedFrame,
    layoutIt->second.mask
);

frame = std::move(maskedFrame);

return true;

}

const WellLayout* WellMasker::getLayout(int wellsCount) const
{
auto layoutIt = layouts_.find(wellsCount);

if (layoutIt == layouts_.end())
{
    return nullptr;
}

return &layoutIt->second;
}
