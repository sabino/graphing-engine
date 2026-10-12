#include "Core.h"
#include "GraphingInterfaces/IBitmap.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
void Require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
graphing::detail::NodePtr Expression(const wchar_t *text)
{
    const auto parsed = graphing::detail::Parse(text, Graphing::FormatType::Linear, Graphing::LocalizationType::DecimalPointAndListComma);
    Require(bool(parsed.root), "Renderer test expression must parse");
    return parsed.root;
}
std::shared_ptr<graphing::detail::GraphState> State()
{
    auto state = std::make_shared<graphing::detail::GraphState>();
    state->options = graphing::detail::CreateGraphOptions();
    state->options->SetShowGrid(false);
    state->options->SetShowAxis(false);
    state->options->SetShowBox(false);
    state->options->SetForceProportional(false);
    state->options->ResetMarkKeyGraphFeaturesData();
    return state;
}
graphing::detail::Plot Plot(graphing::detail::PlotKind kind, const wchar_t *first, const wchar_t *second = nullptr)
{
    graphing::detail::Plot plot;
    plot.kind = kind;
    plot.first = Expression(first);
    if (second)
        plot.second = Expression(second);
    plot.id = 7;
    plot.options = graphing::detail::CreateEquationOptions({220, 20, 30});
    return plot;
}
std::uint32_t Read32(const std::vector<BYTE> &data, std::size_t offset)
{
    return std::uint32_t(data[offset]) | (std::uint32_t(data[offset + 1]) << 8) | (std::uint32_t(data[offset + 2]) << 16) |
           (std::uint32_t(data[offset + 3]) << 24);
}
std::uint32_t ReadBigEndian32(const std::vector<BYTE> &data, std::size_t offset)
{
    Require(offset + 4 <= data.size(), "PNG integer must fit inside its buffer");
    return (std::uint32_t(data[offset]) << 24) | (std::uint32_t(data[offset + 1]) << 16) | (std::uint32_t(data[offset + 2]) << 8) |
           data[offset + 3];
}
std::vector<BYTE> DecodeStoredPng(const std::vector<BYTE> &png, unsigned width, unsigned height)
{
    const std::vector<BYTE> magic{137, 80, 78, 71, 13, 10, 26, 10};
    Require(png.size() >= 57 && std::equal(magic.begin(), magic.end(), png.begin()), "PNG signature missing");
    std::vector<BYTE> zlib;
    bool hasHeader = false, hasEnd = false;
    for (std::size_t offset = 8; offset < png.size();)
    {
        const auto length = ReadBigEndian32(png, offset);
        Require(length <= png.size() - offset - 12, "PNG chunk length must fit in the encoded image");
        std::uint32_t crc = 0xffffffffu;
        for (std::size_t index = offset + 4; index < offset + 8 + length; ++index)
        {
            crc ^= png[index];
            for (unsigned bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
        }
        Require((crc ^ 0xffffffffu) == ReadBigEndian32(png, offset + 8 + length), "PNG chunk CRC must protect its type and contents");
        const std::string type(png.begin() + offset + 4, png.begin() + offset + 8);
        if (type == "IHDR")
        {
            Require(offset == 8 && length == 13 && ReadBigEndian32(png, offset + 8) == width && ReadBigEndian32(png, offset + 12) == height,
                    "PNG header dimensions failed");
            Require(png[offset + 16] == 8 && png[offset + 17] == 6 && png[offset + 18] == 0 && png[offset + 19] == 0 &&
                        png[offset + 20] == 0,
                    "PNG must declare standard noninterlaced RGBA8");
            hasHeader = true;
        }
        else if (type == "IDAT")
            zlib.insert(zlib.end(), png.begin() + offset + 8, png.begin() + offset + 8 + length);
        else if (type == "IEND")
        {
            Require(length == 0 && offset + 12 == png.size(), "PNG must end at IEND");
            hasEnd = true;
        }
        offset += length + 12;
    }
    Require(hasHeader && hasEnd && zlib.size() > 6 && ((unsigned(zlib[0]) * 256 + zlib[1]) % 31) == 0,
            "PNG must carry a standard zlib stream");
    std::vector<BYTE> raw;
    std::size_t offset = 2;
    bool final = false;
    while (!final)
    {
        Require(offset + 5 <= zlib.size() - 4, "Stored DEFLATE block must contain its header");
        const BYTE flags = zlib[offset++];
        Require((flags & 0xfe) == 0, "PNG encoder must emit stored DEFLATE blocks");
        final = flags & 1;
        const unsigned length = zlib[offset] | (unsigned(zlib[offset + 1]) << 8),
                       inverse = zlib[offset + 2] | (unsigned(zlib[offset + 3]) << 8);
        offset += 4;
        Require((length ^ inverse) == 65535 && length <= zlib.size() - 4 - offset, "Stored DEFLATE length and complement must agree");
        raw.insert(raw.end(), zlib.begin() + offset, zlib.begin() + offset + length);
        offset += length;
    }
    Require(offset + 4 == zlib.size() && raw.size() == std::size_t(height) * (width * 4 + 1), "PNG scanline length and final block failed");
    std::uint32_t low = 1, high = 0;
    for (auto byte : raw)
    {
        low = (low + byte) % 65521;
        high = (high + low) % 65521;
    }
    Require(((high << 16) | low) == ReadBigEndian32(zlib, offset), "PNG zlib Adler32 must protect uncompressed pixels");
    std::vector<BYTE> pixels;
    for (unsigned row = 0; row < height; ++row)
    {
        const auto start = std::size_t(row) * (width * 4 + 1);
        Require(raw[start] == 0, "PNG must use filter-zero scanlines");
        pixels.insert(pixels.end(), raw.begin() + start + 1, raw.begin() + start + 1 + width * 4);
    }
    return pixels;
}
void TestRangesAndHits()
{
    using namespace graphing::detail;
    auto state = State();
    state->plots.push_back(Plot(PlotKind::ExplicitY, L"x"));
    auto renderer = CreateRenderer(state);
    Require(renderer->SetGraphSize(320, 240) == S_OK, "Valid graph size failed");
    Require(renderer->SetGraphSize(0, 240) == E_INVALIDARG && renderer->SetGraphSize(100000, 100000) == E_INVALIDARG,
            "Invalid graph sizes must be bounded");
    Require(renderer->SetDpi(0, 96) == E_INVALIDARG && renderer->SetDpi(144, 144) == S_OK, "DPI validation failed");
    Require(renderer->SetDisplayRanges(-2, 2, -2, 2) == S_OK, "Valid bounds failed");
    Require(renderer->SetDisplayRanges(2, -2, -2, 2) == E_INVALIDARG, "Reversed bounds must fail");
    Require(renderer->SetDisplayRanges(0, std::numeric_limits<double>::infinity(), -2, 2) == E_INVALIDARG, "Nonfinite bounds must fail");
    int id = 0;
    float sx = 0, sy = 0;
    double x = 0, y = 0, rho = 0, theta = 0, t = 0;
    Require(renderer->GetClosePointData(160, 120, 0.001, id, sx, sy, x, y, rho, theta, t) == S_OK && id == 7, "Segment hit testing failed");
    Require(std::abs(x - y) < 1e-9 && std::hypot(sx - 160, sy - 120) < 4, "Nearest segment point must lie on the line");
    Require(renderer->GetClosePointData(10, 10, 0.001, id, sx, sy, x, y, rho, theta, t) == S_OK && id == 7,
            "World precision must not act as a tiny screen hit radius");
    Require(std::abs(x - y) < 1e-9, "A distant pointer must trace a point on the curve");
    state->plots[0].first = Expression(L"100");
    Require(renderer->GetClosePointData(10, 10, 0.001, id, sx, sy, x, y, rho, theta, t) == S_FALSE && id == -1 && std::isnan(x),
            "A graph without visible curve segments must initialize missed outputs");
    state->plots[0].first = Expression(L"x");
    Require(renderer->GetClosePointData(0, 0, -1, id, sx, sy, x, y, rho, theta, t) == E_INVALIDARG && id == -1,
            "Invalid precision must initialize outputs");
    Require(renderer->ScaleRange(0, 0, 0.5) == S_OK, "Zoom failed");
    Require(renderer->MoveRangeByRatio(0.5, -0.5) == S_OK, "Pan failed");
    double xmin, xmax, ymin, ymax;
    renderer->GetDisplayRanges(xmin, xmax, ymin, ymax);
    Require(std::abs(xmin + 0.5) < 1e-12 && std::abs(xmax - 1.5) < 1e-12 && std::abs(ymin + 1.5) < 1e-12 && std::abs(ymax - 0.5) < 1e-12,
            "Zoom and pan must preserve their mathematical ranges");
    Require(renderer->ChangeRange(Graphing::Renderer::ChangeRangeAction::WidenZ) == E_NOTIMPL,
            "Unsupported Z ranges must report E_NOTIMPL");
    Require(renderer->ResetRange() == S_OK, "Default range reset failed");
    bool missing = true;
#ifndef _WIN32
    Require(renderer->DrawD2D1(nullptr, nullptr, missing) == E_NOTIMPL && !missing,
            "Portable renderer must initialize D2D output and report unsupported API");
#endif
    std::shared_ptr<Graphing::IBitmap> bitmap;
    Require(renderer->GetBitmap(bitmap, missing) == S_OK && bitmap && !missing, "Portable bitmap failed");
    state->initialization_error = E_INVALIDARG;
    graphing::Scene retained;
    Require(renderer->PrepareGraph() == S_OK && graphing::GetScene(*renderer, retained) == S_OK && !retained.strokes.empty(),
            "A rejected initialization attempt must preserve the previously accepted renderable scene");
    Require(renderer->GetBitmap(bitmap, missing) == S_OK && bitmap, "A rejected initialization attempt must preserve bitmap rendering");
    auto emptyState = State();
    emptyState->initialization_error = E_INVALIDARG;
    auto emptyRenderer = CreateRenderer(emptyState);
    Require(emptyRenderer->PrepareGraph() == E_INVALIDARG, "An initialization error without retained plots must remain observable");
    emptyState->initialization_error = S_OK;
    Require(emptyRenderer->PrepareGraph() == E_FAIL, "An empty graph must not claim it prepared a renderable scene");
    const auto bytes = bitmap->GetData();
    Require(DecodeStoredPng(bytes, 320, 240).size() == 320 * 240 * 4, "GetBitmap must return a complete interoperable PNG");
    const auto bmp = graphing::EncodeBitmap(retained);
    Require(bmp.size() > 122 && bmp[0] == 'B' && bmp[1] == 'M', "Optional BMP exporter signature missing");
    Require(Read32(bmp, 2) == bmp.size() && Read32(bmp, 10) == 122 && Read32(bmp, 14) == 108,
            "BMP headers must describe the complete encoded file");
    Require(Read32(bmp, 18) == 320 && static_cast<std::int32_t>(Read32(bmp, 22)) == -240, "BMP dimensions and top-down rows failed");
}
void TestNormalizedTransformsAndTracingPrecision()
{
    using namespace graphing::detail;
    auto state = State();
    state->plots.push_back(Plot(PlotKind::ExplicitY, L"x*x"));
    auto renderer = CreateRenderer(state);
    renderer->SetGraphSize(401, 301);
    const auto checkRanges = [&](double expectedXMin, double expectedXMax, double expectedYMin, double expectedYMax) {
        double xmin, xmax, ymin, ymax;
        renderer->GetDisplayRanges(xmin, xmax, ymin, ymax);
        Require(std::abs(xmin - expectedXMin) < 1e-10 && std::abs(xmax - expectedXMax) < 1e-10 && std::abs(ymin - expectedYMin) < 1e-10 &&
                    std::abs(ymax - expectedYMax) < 1e-10,
                "Asymmetric ranges must follow normalized center and half-span translation");
    };
    renderer->SetDisplayRanges(2, 10, -8, 4);
    Require(renderer->ScaleRange(0, 0, 0.5) == S_OK, "Viewport-center zoom failed");
    checkRanges(4, 8, -5, 1);
    renderer->SetDisplayRanges(2, 10, -8, 4);
    Require(renderer->ScaleRange(0.5, -0.5, 0.5) == S_OK, "Off-center normalized zoom failed");
    checkRanges(5, 9, -6.5, -0.5);
    renderer->SetDisplayRanges(2, 10, -8, 4);
    Require(renderer->MoveRangeByRatio(1, -1) == S_OK, "Half-screen pan failed");
    checkRanges(6, 14, -14, -2);
    renderer->SetDisplayRanges(2, 10, -8, 4);
    Require(renderer->ChangeRange(Graphing::Renderer::ChangeRangeAction::ZoomIn) == S_OK, "ChangeRange normalized center failed");
    checkRanges(2.8, 9.2, -6.8, 2.8);
    renderer->SetDisplayRanges(-2, 2, -1, 4);
    int id;
    float sx, sy;
    double x, y, rho, theta, t;
    Require(renderer->GetClosePointData(310, 140, 0.000001, id, sx, sy, x, y, rho, theta, t) == S_OK,
            "Small source-derived world precision must still find a visible curve");
    Require(id == 7 && std::abs(y - x * x) < 1e-12, "Refined tracing output must evaluate the original curve");
    Require(std::abs(double(sx) - (x + 2) / 4 * 400) < 0.0001 && std::abs(double(sy) - (4 - y) / 5 * 300) < 0.0001,
            "Tracing screen coordinates must describe its returned mathematical point");
    state->plots[0] = Plot(PlotKind::Parametric, L"t", L"t*t");
    Require(renderer->GetClosePointData(310, 140, 0.000001, id, sx, sy, x, y, rho, theta, t) == S_OK, "Parametric refinement failed");
    Require(std::isfinite(t) && std::abs(x - t) < 1e-12 && std::abs(y - t * t) < 1e-12,
            "Parametric tracing must return one consistent parameter and point");
}
void TestGeometry()
{
    using namespace graphing::detail;
    auto state = State();
    state->plots.push_back(Plot(PlotKind::ExplicitY, L"1/x"));
    auto scene = BuildScene(*state, {-2, 2, -2, 2}, 321, 241);
    Require(!scene.strokes.empty() && !scene.has_missing_data, "A rational domain gap must not report incomplete rendering");
    for (const auto &stroke : scene.strokes)
        for (std::size_t index = 1; index < stroke.points.size(); ++index)
            Require(!((stroke.points[index - 1].x < 160 && stroke.points[index].x > 160) ||
                      (stroke.points[index - 1].x > 160 && stroke.points[index].x < 160)),
                    "A pole must disconnect segments");
    state->plots[0] = Plot(PlotKind::ExplicitY, L"sqrt(x)");
    scene = BuildScene(*state, {-2, 2, -2, 2}, 320, 240);
    Require(!scene.strokes.empty() && !scene.has_missing_data, "Ordinary real-domain cropping must not report missing data");
    state->plots[0] = Plot(PlotKind::Implicit, L"x*x+y*y", L"1");
    scene = BuildScene(*state, {-2, 2, -2, 2}, 320, 240);
    Require(!scene.strokes.empty() && !scene.has_missing_data, "Implicit circle contour failed");
    for (const auto &stroke : scene.strokes)
        for (auto point : stroke.points)
        {
            const auto x = -2 + point.x / 319 * 4, y = 2 - point.y / 239 * 4;
            Require(std::abs(x * x + y * y - 1) < 0.03, "Contour vertices must approximate the implicit equation");
        }
    state->plots[0] = Plot(PlotKind::Inequality, L"y", L"x");
    state->plots[0].relation = L">";
    scene = BuildScene(*state, {-2, 2, -2, 2}, 320, 240);
    Require(!scene.fills.empty(), "Inequality region must have real fill geometry");
    const auto image = graphing::Rasterize(scene);
    const auto upper = (std::size_t(20) * 320 + 160) * 4, lower = (std::size_t(220) * 320 + 160) * 4;
    Require(image[upper + 1] < 255 && image[lower + 1] == 255, "Inequality fill must stay on the correct side of its boundary");
    auto polar = Plot(PlotKind::Polar, L"2");
    auto point = EvaluatePlot(polar, state->context, 3.14159265358979323846 / 2);
    Require(point && std::abs(point->x) < 1e-10 && std::abs(point->y - 2) < 1e-10, "Polar coordinate conversion failed");
    state->context.trig_unit = Graphing::EvalTrigUnitMode::Degrees;
    point = EvaluatePlot(polar, state->context, 90);
    Require(point && std::abs(point->x) < 1e-10 && std::abs(point->y - 2) < 1e-10, "Polar coordinate conversion must respect angle units");
    auto parametric = Plot(PlotKind::Parametric, L"t", L"t*t");
    point = EvaluatePlot(parametric, state->context, 2);
    Require(point && point->x == 2 && point->y == 4, "Parametric coordinates must share the same parameter");
    state->plots[0] = Plot(PlotKind::ExplicitY, L"a*x");
    scene = BuildScene(*state, {-2, 2, -2, 2}, 320, 240);
    Require(scene.has_missing_data && scene.strokes.empty(), "Unbound parameter must report missing data");
    state->plots[0] = Plot(PlotKind::ExplicitY, L"1000000*x");
    scene = BuildScene(*state, {-2, 2, -2, 2}, 320, 240);
    Require(!scene.has_missing_data && !scene.strokes.empty(), "A steep clipped line must remain complete");
}
void TestOptionsAndRaster()
{
    using namespace graphing::detail;
    auto state = State();
    state->plots.push_back(Plot(PlotKind::ExplicitY, L"x*x"));
    state->plots[0].selected = true;
    state->plots[0].options->SetSelectedEquationLineWidth(5);
    state->plots[0].options->SetLineStyle(Graphing::Renderer::LineStyle::DashDot);
    auto scene = BuildScene(*state, {-2, 2, -2, 2}, 320, 240);
    Require(scene.strokes.front().width == 5 && scene.strokes.front().style == Graphing::Renderer::LineStyle::DashDot,
            "Selected width and line style must survive scene generation");
    state->options->SetShowGrid(true);
    state->options->SetShowAxis(true);
    state->options->SetShowBox(true);
    state->options->SetAliasX(L"x<&");
    scene = BuildScene(*state, {-2, 2, -2, 2}, 320, 240);
    Require(!scene.labels.empty() &&
                std::any_of(scene.strokes.begin(), scene.strokes.end(), [](const auto &stroke) { return stroke.equation_id < 0; }),
            "Grid, axis, and box decorations missing");
    const auto svg = graphing::SerializeSvg(scene);
    Require(svg.find(L"<text") != std::wstring::npos && svg.find(L"x&lt;&amp;") != std::wstring::npos,
            "SVG labels must remain useful and XML-safe");
    state->options->SetForceProportional(true);
    auto renderer = CreateRenderer(state);
    renderer->SetGraphSize(400, 200);
    renderer->SetDisplayRanges(-1, 1, -1, 1);
    double xmin, xmax, ymin, ymax;
    renderer->GetDisplayRanges(xmin, xmax, ymin, ymax);
    Require(std::abs((xmax - xmin) / (ymax - ymin) - 2) < 1e-12, "Proportional range must use equal coordinate scale");
    graphing::Scene alpha;
    alpha.width = 2;
    alpha.height = 2;
    alpha.background = {0, 0, 255, 255};
    alpha.fills.push_back({{{0, 0}, {2, 0}, {2, 2}, {0, 2}}, {255, 0, 0, 128}});
    const auto pixels = graphing::Rasterize(alpha);
    Require(pixels[0] == 128 && pixels[2] == 127 && pixels[3] == 255, "Fill alpha must use source-over compositing");
    const auto alphaPng = DecodeStoredPng(graphing::EncodePng(alpha), 2, 2);
    Require(alphaPng == pixels, "PNG must preserve RGBA pixels through filter-zero stored DEFLATE decoding");
    const auto alphaBitmap = graphing::EncodeBitmap(alpha);
    Require(alphaBitmap[122] == 127 && alphaBitmap[124] == 128 && alphaBitmap[125] == 255,
            "BMP pixels must use the declared BGRA channel masks");
    graphing::Scene line;
    line.width = 100;
    line.height = 10;
    line.strokes.push_back({{{0, 5}, {99, 5}}, {0, 0, 0}, 2, Graphing::Renderer::LineStyle::Solid, 0});
    const auto solid = graphing::Rasterize(line);
    for (auto style : {Graphing::Renderer::LineStyle::Dot, Graphing::Renderer::LineStyle::Dash, Graphing::Renderer::LineStyle::DashDot,
                       Graphing::Renderer::LineStyle::DashDotDot})
    {
        line.strokes[0].style = style;
        const auto dashed = graphing::Rasterize(line);
        Require(dashed != solid && std::count(dashed.begin(), dashed.end(), BYTE(0)) > 0,
                "Every dash style must rasterize visible strokes and gaps");
    }
    graphing::Scene text;
    text.width = 40;
    text.height = 20;
    text.labels.push_back({{2, 12}, L"x=1", {0, 0, 0}});
    const auto textPixels = graphing::Rasterize(text);
    Require(std::count(textPixels.begin(), textPixels.end(), BYTE(0)) > 0, "Bitmap axis labels must render visible geometry");
    graphing::Scene oversized;
    oversized.width = 100000;
    oversized.height = 100000;
    Require(graphing::EncodeBitmap(oversized).empty() && graphing::EncodePng(oversized).empty(),
            "Bitmap allocation must reject oversized scenes");
}
} // namespace
int main()
{
    try
    {
        TestRangesAndHits();
        TestNormalizedTransformsAndTracingPrecision();
        TestGeometry();
        TestOptionsAndRaster();
        std::cout << "Renderer ranges, geometry, hit testing, styles, and bitmap encoding passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
