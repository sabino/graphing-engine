#pragma once

#include "GraphingInterfaces/IMathSolver.h"
#include <cstdint>
#include <vector>

namespace graphing
{
struct Point
{
    double x = 0;
    double y = 0;
};
struct Stroke
{
    std::vector<Point> points;
    Graphing::Color color;
    float width = 1;
    Graphing::Renderer::LineStyle style = Graphing::Renderer::LineStyle::Solid;
    int equation_id = -1;
};
struct Fill
{
    std::vector<Point> polygon;
    Graphing::Color color;
};
struct Label
{
    Point position;
    std::wstring text;
    Graphing::Color color;
};
struct Scene
{
    unsigned width = 640;
    unsigned height = 480;
    Graphing::Color background{255, 255, 255};
    std::vector<Stroke> strokes;
    std::vector<Fill> fills;
    std::vector<Label> labels;
    bool has_missing_data = false;
};

// Geometry stays independent of native drawing contexts.
GRAPHINGAPI HRESULT GetScene(const Graphing::Renderer::IGraphRenderer &renderer, Scene &scene_out);
GRAPHINGAPI std::vector<std::uint8_t> Rasterize(const Scene &scene);
GRAPHINGAPI std::vector<BYTE> EncodeBitmap(const Scene &scene);
GRAPHINGAPI std::vector<BYTE> EncodePng(const Scene &scene);
GRAPHINGAPI std::wstring SerializeSvg(const Scene &scene);
} // namespace graphing
