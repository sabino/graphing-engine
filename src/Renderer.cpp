#include "Core.h"
#include "GraphingInterfaces/IBitmap.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <new>
#include <sstream>
#ifdef _WIN32
#include <d2d1.h>
#include <d2d1helper.h>
#include <dxgiformat.h>
#endif

namespace graphing::detail
{
namespace
{
constexpr double PI = 3.14159265358979323846;
constexpr std::size_t MAX_PIXELS = 4 * 1024 * 1024;
bool ValidSize(unsigned width, unsigned height)
{
    return width && height && width <= 4096 && height <= 4096 && static_cast<std::size_t>(width) * height <= MAX_PIXELS;
}
Ranges Proportional(Ranges ranges, unsigned width, unsigned height, const GraphState &state)
{
    if (!state.options || !state.options->GetForceProportional())
        return ranges;
    const double pixelScale = std::max((ranges.x_max - ranges.x_min) / width, (ranges.y_max - ranges.y_min) / height);
    const double centerX = ranges.x_min / 2 + ranges.x_max / 2, centerY = ranges.y_min / 2 + ranges.y_max / 2;
    const Ranges adjusted{centerX - pixelScale * width / 2, centerX + pixelScale * width / 2, centerY - pixelScale * height / 2,
                          centerY + pixelScale * height / 2};
    return IsValidRange(adjusted.x_min, adjusted.x_max) && IsValidRange(adjusted.y_min, adjusted.y_max) ? adjusted : ranges;
}
Point Screen(Point value, const Ranges &ranges, unsigned width, unsigned height)
{
    return {(value.x - ranges.x_min) / (ranges.x_max - ranges.x_min) * (width - 1),
            (ranges.y_max - value.y) / (ranges.y_max - ranges.y_min) * (height - 1)};
}
Point World(Point screen, const Ranges &ranges, unsigned width, unsigned height)
{
    return {ranges.x_min + screen.x / std::max(1u, width - 1) * (ranges.x_max - ranges.x_min),
            ranges.y_max - screen.y / std::max(1u, height - 1) * (ranges.y_max - ranges.y_min)};
}
bool Clip(Point &a, Point &b, unsigned width, unsigned height)
{
    if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) || !std::isfinite(b.y))
        return false;
    double low = 0, high = 1;
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double p[] = {-dx, dx, -dy, dy}, q[] = {a.x, double(width - 1) - a.x, a.y, double(height - 1) - a.y};
    for (unsigned index = 0; index < 4; ++index)
    {
        if (p[index] == 0)
        {
            if (q[index] < 0)
                return false;
            continue;
        }
        const double ratio = q[index] / p[index];
        if (p[index] < 0)
            low = std::max(low, ratio);
        else
            high = std::min(high, ratio);
        if (low > high)
            return false;
    }
    const auto original = a;
    a = {original.x + low * dx, original.y + low * dy};
    b = {original.x + high * dx, original.y + high * dy};
    return true;
}
struct Budget
{
    std::chrono::steady_clock::time_point deadline;
    std::size_t evaluations = 0;
    bool exhausted = false;
    explicit Budget(const GraphState &state)
    {
        const auto requested = state.options ? state.options->GetMaxExecutionTime() : 0;
        // The public interface gives no unit. This implementation uses milliseconds; zero uses the safety cap.
        deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(requested ? std::min(1000ull, requested) : 1000ull);
    }
    bool Stop()
    {
        if (exhausted)
            return true;
        ++evaluations;
        exhausted = evaluations > 250000 || (evaluations % 64 == 0 && std::chrono::steady_clock::now() >= deadline);
        return exhausted;
    }
};
void AddSegment(Scene &scene, Point a, Point b, Graphing::Color color, float width, Graphing::Renderer::LineStyle style, int id)
{
    if (scene.strokes.size() >= 100000 || !Clip(a, b, scene.width, scene.height))
        return;
    if (!scene.strokes.empty())
    {
        auto &last = scene.strokes.back();
        if (last.equation_id == id && id >= 0 && last.points.size() < 4096 && last.points.back().x == a.x && last.points.back().y == a.y)
        {
            last.points.push_back(b);
            return;
        }
    }
    scene.strokes.push_back({{a, b}, color, width, style, id});
}
bool HasUnboundVariables(const Plot &plot, const EvalContext &context)
{
    auto variables = FindVariables(plot.first);
    if (plot.second)
    {
        const auto second = FindVariables(plot.second);
        variables.insert(second.begin(), second.end());
    }
    for (const auto &variable : variables)
    {
        if (context.variables.count(variable))
            continue;
        if ((plot.kind == PlotKind::ExplicitY && variable == L"x") || (plot.kind == PlotKind::ExplicitX && variable == L"y"))
            continue;
        if ((plot.kind == PlotKind::Implicit || plot.kind == PlotKind::Inequality) && (variable == L"x" || variable == L"y"))
            continue;
        if (plot.kind == PlotKind::Polar && (variable == L"theta" || variable == L"θ"))
            continue;
        if (plot.kind == PlotKind::Parametric && variable == L"t")
            continue;
        return true;
    }
    return false;
}
double Residual(const Plot &plot, EvalContext context, Point point)
{
    context.variables[L"x"] = point.x;
    context.variables[L"y"] = point.y;
    const auto first = Evaluate(plot.first, context);
    return plot.second ? first - Evaluate(plot.second, context) : first;
}
void SampleCurve(Scene &scene, const Plot &plot, const GraphState &state, const Ranges &ranges, Budget &budget, Graphing::Color color,
                 float lineWidth, Graphing::Renderer::LineStyle style)
{
    double minimum = state.parameter_min, maximum = state.parameter_max;
    if (plot.kind == PlotKind::ExplicitY)
    {
        minimum = ranges.x_min;
        maximum = ranges.x_max;
    }
    if (plot.kind == PlotKind::ExplicitX)
    {
        minimum = ranges.y_min;
        maximum = ranges.y_max;
    }
    if (!IsValidRange(minimum, maximum))
    {
        scene.has_missing_data = true;
        return;
    }
    const unsigned count = std::min(4096u, std::max(128u, std::max(scene.width, scene.height) * 2));
    std::function<void(double, std::optional<Point>, double, std::optional<Point>, unsigned)> refine;
    refine = [&](double left, std::optional<Point> a, double right, std::optional<Point> b, unsigned depth) {
        if (budget.Stop())
            return;
        const double middle = left / 2 + right / 2;
        const auto midpoint = EvaluatePlot(plot, state.context, middle);
        if (!a || !b || !midpoint)
        {
            if (depth < 6 && (a || b || midpoint))
            {
                refine(left, a, middle, midpoint, depth + 1);
                refine(middle, midpoint, right, b, depth + 1);
            }
            return;
        }
        auto screenA = Screen(*a, ranges, scene.width, scene.height), screenB = Screen(*b, ranges, scene.width, scene.height);
        const auto screenMiddle = Screen(*midpoint, ranges, scene.width, scene.height);
        const double deviation =
            std::hypot(screenMiddle.x - (screenA.x / 2 + screenB.x / 2), screenMiddle.y - (screenA.y / 2 + screenB.y / 2));
        if (!std::isfinite(deviation) || deviation > 1.0)
        {
            if (depth < 8)
            {
                refine(left, a, middle, midpoint, depth + 1);
                refine(middle, midpoint, right, b, depth + 1);
            }
            return;
        }
        AddSegment(scene, screenA, screenB, color, lineWidth, style, static_cast<int>(plot.id));
    };
    double previousParameter = minimum;
    auto previous = EvaluatePlot(plot, state.context, minimum);
    for (unsigned index = 1; index <= count && !budget.Stop(); ++index)
    {
        const double parameter = minimum + (maximum - minimum) * index / count;
        auto current = EvaluatePlot(plot, state.context, parameter);
        refine(previousParameter, previous, parameter, current, 0);
        previousParameter = parameter;
        previous = current;
    }
}
void SampleContour(Scene &scene, const Plot &plot, const GraphState &state, const Ranges &ranges, Budget &budget, Graphing::Color color,
                   float lineWidth, Graphing::Renderer::LineStyle style)
{
    const unsigned columns = std::clamp(scene.width / 6, 32u, 192u), rows = std::clamp(scene.height / 6, 32u, 192u);
    std::vector<double> values(static_cast<std::size_t>(columns + 1) * (rows + 1));
    const auto pointAt = [&](unsigned x, unsigned y) {
        return Point{ranges.x_min + (ranges.x_max - ranges.x_min) * x / columns, ranges.y_max - (ranges.y_max - ranges.y_min) * y / rows};
    };
    for (unsigned y = 0; y <= rows; ++y)
        for (unsigned x = 0; x <= columns; ++x)
        {
            if (budget.Stop())
                return;
            values[static_cast<std::size_t>(y) * (columns + 1) + x] = Residual(plot, state.context, pointAt(x, y));
        }
    const bool positive =
        plot.relation == L">" || plot.relation == L">=" || plot.relation == L"Greater" || plot.relation == L"GreaterEqual";
    for (unsigned y = 0; y < rows; ++y)
        for (unsigned x = 0; x < columns; ++x)
        {
            if (budget.Stop())
                return;
            const Point corners[] = {pointAt(x, y), pointAt(x + 1, y), pointAt(x + 1, y + 1), pointAt(x, y + 1)};
            const double cell[] = {values[y * (columns + 1) + x], values[y * (columns + 1) + x + 1],
                                   values[(y + 1) * (columns + 1) + x + 1], values[(y + 1) * (columns + 1) + x]};
            if (!std::all_of(std::begin(cell), std::end(cell), [](double value) { return std::isfinite(value); }))
                continue;
            std::vector<Point> crossings, fill;
            std::optional<Point> edgeCrossings[4];
            for (unsigned edge = 0; edge < 4; ++edge)
            {
                const unsigned next = (edge + 1) % 4;
                if (plot.kind == PlotKind::Inequality && (positive ? cell[edge] >= 0 : cell[edge] <= 0))
                    fill.push_back(Screen(corners[edge], ranges, scene.width, scene.height));
                if ((cell[edge] < 0) == (cell[next] < 0))
                    continue;
                const double fraction = cell[edge] / (cell[edge] - cell[next]);
                const Point crossing{corners[edge].x + fraction * (corners[next].x - corners[edge].x),
                                     corners[edge].y + fraction * (corners[next].y - corners[edge].y)};
                const double residual = Residual(plot, state.context, crossing);
                if (!std::isfinite(residual) || std::abs(residual) > std::max(std::abs(cell[edge]), std::abs(cell[next])) * 0.75)
                    continue;
                const auto screen = Screen(crossing, ranges, scene.width, scene.height);
                crossings.push_back(screen);
                edgeCrossings[edge] = screen;
                if (plot.kind == PlotKind::Inequality)
                    fill.push_back(screen);
            }
            const double centerValue =
                Residual(plot, state.context, {(corners[0].x + corners[2].x) / 2, (corners[0].y + corners[2].y) / 2});
            if (fill.size() >= 3)
            {
                auto shade = color;
                shade.A = static_cast<BYTE>(color.A / 5);
                const bool insideCenter = positive ? centerValue >= 0 : centerValue <= 0;
                if (crossings.size() == 4 && !insideCenter)
                {
                    for (unsigned corner = 0; corner < 4; ++corner)
                    {
                        const auto previous = (corner + 3) % 4;
                        if ((positive ? cell[corner] >= 0 : cell[corner] <= 0) && edgeCrossings[corner] && edgeCrossings[previous])
                            scene.fills.push_back({{Screen(corners[corner], ranges, scene.width, scene.height), *edgeCrossings[corner],
                                                    *edgeCrossings[previous]},
                                                   shade});
                    }
                }
                else
                    scene.fills.push_back({fill, shade});
            }
            if (crossings.size() == 4 && (centerValue < 0) != (cell[0] < 0))
            {
                AddSegment(scene, crossings[0], crossings[3], color, lineWidth, style, static_cast<int>(plot.id));
                AddSegment(scene, crossings[1], crossings[2], color, lineWidth, style, static_cast<int>(plot.id));
            }
            else
                for (std::size_t index = 0; index + 1 < crossings.size(); index += 2)
                    AddSegment(scene, crossings[index], crossings[index + 1], color, lineWidth, style, static_cast<int>(plot.id));
        }
}
double GridStep(double span)
{
    const double magnitude = std::pow(10.0, std::floor(std::log10(span / 8)));
    const double scaled = span / 8 / magnitude;
    return magnitude * (scaled <= 1 ? 1 : scaled <= 2 ? 2 : scaled <= 5 ? 5 : 10);
}
void AddFeatures(Scene &scene, const Plot &plot, const GraphState &state, const Ranges &ranges)
{
    const auto &options = *state.options;
    if (!(options.GetMarkZeros() || options.GetMarkYIntercept() || options.GetMarkMinima() || options.GetMarkMaxima() ||
          options.GetMarkInflectionPoints() || options.GetMarkVerticalAsymptotes() || options.GetMarkHorizontalAsymptotes() ||
          options.GetMarkObliqueAsymptotes()))
        return;
    const auto features = GetFeatureGeometry(state, plot.id);
    const double radius =
        plot.options ? (plot.selected ? plot.options->GetSelectedEquationPointRadius() : plot.options->GetPointRadius()) : 3;
    const auto mark = [&](Point value, Graphing::Color color) {
        const auto center = Screen(value, ranges, scene.width, scene.height);
        if (!std::isfinite(center.x) || !std::isfinite(center.y) || center.x < 0 || center.x >= scene.width || center.y < 0 ||
            center.y >= scene.height)
            return;
        Fill dot{{}, color};
        for (unsigned index = 0; index < 16; ++index)
            dot.polygon.push_back({center.x + std::clamp(radius, 0.5, 100.0) * std::cos(2 * PI * index / 16),
                                   center.y + std::clamp(radius, 0.5, 100.0) * std::sin(2 * PI * index / 16)});
        scene.fills.push_back(std::move(dot));
    };
    if (options.GetMarkZeros())
        for (auto point : features.zeros)
            mark(point, options.GetZerosColor());
    if (options.GetMarkYIntercept() && features.y_intercept)
        mark(*features.y_intercept, options.GetZerosColor());
    if (options.GetMarkMinima())
        for (auto point : features.minima)
            mark(point, options.GetExtremaColor());
    if (options.GetMarkMaxima())
        for (auto point : features.maxima)
            mark(point, options.GetExtremaColor());
    if (options.GetMarkInflectionPoints())
        for (auto point : features.inflections)
            mark(point, options.GetInflectionPointsColor());
    const auto asymptote = [&](Point a, Point b) {
        AddSegment(scene, Screen(a, ranges, scene.width, scene.height), Screen(b, ranges, scene.width, scene.height),
                   options.GetAsymptotesColor(), 1, Graphing::Renderer::LineStyle::Dash, -1);
    };
    if (options.GetMarkVerticalAsymptotes())
        for (auto value : features.vertical_asymptotes)
            asymptote({value, ranges.y_min}, {value, ranges.y_max});
    if (options.GetMarkHorizontalAsymptotes())
        for (auto value : features.horizontal_asymptotes)
            asymptote({ranges.x_min, value}, {ranges.x_max, value});
    if (options.GetMarkObliqueAsymptotes())
        for (auto [slope, intercept] : features.oblique_asymptotes)
            asymptote({ranges.x_min, slope * ranges.x_min + intercept}, {ranges.x_max, slope * ranges.x_max + intercept});
}
} // namespace

bool IsValidRange(double minimum, double maximum)
{
    return std::isfinite(minimum) && std::isfinite(maximum) && maximum > minimum && std::isfinite(maximum - minimum) &&
           std::abs(minimum) <= 1e100 && std::abs(maximum) <= 1e100;
}
std::optional<Point> EvaluatePlot(const Plot &plot, const EvalContext &original, double parameter)
{
    if (!std::isfinite(parameter))
        return std::nullopt;
    auto context = original;
    Point point;
    switch (plot.kind)
    {
    case PlotKind::ExplicitY:
        context.variables[L"x"] = parameter;
        point = {parameter, Evaluate(plot.first, context)};
        break;
    case PlotKind::ExplicitX:
        context.variables[L"y"] = parameter;
        point = {Evaluate(plot.first, context), parameter};
        break;
    case PlotKind::Polar: {
        context.variables[L"theta"] = parameter;
        context.variables[L"θ"] = parameter;
        const auto radius = Evaluate(plot.first, context);
        double angle = parameter;
        if (context.trig_unit == Graphing::EvalTrigUnitMode::Degrees)
            angle *= PI / 180;
        if (context.trig_unit == Graphing::EvalTrigUnitMode::Grads)
            angle *= PI / 200;
        point = {radius * std::cos(angle), radius * std::sin(angle)};
        break;
    }
    case PlotKind::Parametric:
        context.variables[L"t"] = parameter;
        point = {Evaluate(plot.first, context), Evaluate(plot.second, context)};
        break;
    default:
        return std::nullopt;
    }
    return std::isfinite(point.x) && std::isfinite(point.y) ? std::optional<Point>(point) : std::nullopt;
}
Scene BuildScene(const GraphState &state, const Ranges &suppliedRanges, unsigned width, unsigned height)
{
    Scene scene;
    scene.width = width;
    scene.height = height;
    if (!ValidSize(width, height) || !IsValidRange(suppliedRanges.x_min, suppliedRanges.x_max) ||
        !IsValidRange(suppliedRanges.y_min, suppliedRanges.y_max))
    {
        scene.has_missing_data = true;
        return scene;
    }
    const auto ranges = Proportional(suppliedRanges, width, height, state);
    const auto options = state.options;
    if (options)
        scene.background = options->GetBackColor();
    const auto backgroundLine = [&](Point a, Point b, Graphing::Color color) {
        AddSegment(scene, Screen(a, ranges, width, height), Screen(b, ranges, width, height), color, 1,
                   options ? options->GetLineStyle() : Graphing::Renderer::LineStyle::Solid, -1);
    };
    if (options && options->GetShowGrid())
    {
        const auto xStep = GridStep(ranges.x_max - ranges.x_min), yStep = GridStep(ranges.y_max - ranges.y_min);
        for (unsigned index = 0; index < 64; ++index)
        {
            const double x = (std::ceil(ranges.x_min / xStep) + index) * xStep;
            if (x > ranges.x_max)
                break;
            backgroundLine({x, ranges.y_min}, {x, ranges.y_max}, options->GetGridColor());
            std::wostringstream label;
            label.precision(4);
            label << x;
            scene.labels.push_back({{Screen({x, 0}, ranges, width, height).x + 2,
                                     std::clamp(Screen({0, 0}, ranges, width, height).y + 14, 14.0, std::max(14.0, double(height) - 2))},
                                    label.str(),
                                    options->GetFontColor()});
        }
        for (unsigned index = 0; index < 64; ++index)
        {
            const double y = (std::ceil(ranges.y_min / yStep) + index) * yStep;
            if (y > ranges.y_max)
                break;
            backgroundLine({ranges.x_min, y}, {ranges.x_max, y}, options->GetGridColor());
            std::wostringstream label;
            label.precision(4);
            label << y;
            scene.labels.push_back({{std::clamp(Screen({0, 0}, ranges, width, height).x + 3, 3.0, std::max(3.0, double(width) - 32)),
                                     Screen({0, y}, ranges, width, height).y - 2},
                                    label.str(),
                                    options->GetFontColor()});
        }
    }
    if (options && options->GetShowAxis())
    {
        backgroundLine({ranges.x_min, 0}, {ranges.x_max, 0}, options->GetAxisColor());
        backgroundLine({0, ranges.y_min}, {0, ranges.y_max}, options->GetAxisColor());
        scene.labels.push_back(
            {{double(width) - 16, std::clamp(Screen({0, 0}, ranges, width, height).y - 5, 12.0, std::max(12.0, double(height) - 2))},
             options->GetAliasX(),
             options->GetFontColor()});
        scene.labels.push_back({{std::clamp(Screen({0, 0}, ranges, width, height).x + 5, 3.0, std::max(3.0, double(width) - 16)), 14},
                                options->GetAliasY(),
                                options->GetFontColor()});
    }
    if (options && options->GetShowBox())
        scene.strokes.push_back({{{0, 0}, {double(width - 1), 0}, {double(width - 1), double(height - 1)}, {0, double(height - 1)}, {0, 0}},
                                 options->GetBoxColor(),
                                 1,
                                 options->GetLineStyle(),
                                 -1});
    Budget budget(state);
    for (std::size_t index = 0; index < state.plots.size(); ++index)
    {
        if (index >= 128 || budget.Stop())
        {
            scene.has_missing_data = true;
            break;
        }
        const auto &plot = state.plots[index];
        if (plot.kind == PlotKind::Unsupported || HasUnboundVariables(plot, state.context))
        {
            scene.has_missing_data = true;
            continue;
        }
        const auto color = plot.options ? plot.options->GetGraphColor() : Graphing::Color(0, 80, 200);
        const float lineWidth =
            plot.options ? (plot.selected ? plot.options->GetSelectedEquationLineWidth() : plot.options->GetLineWidth()) : 1;
        const auto style = plot.options ? plot.options->GetLineStyle() : Graphing::Renderer::LineStyle::Solid;
        if (plot.kind == PlotKind::Implicit || plot.kind == PlotKind::Inequality)
            SampleContour(scene, plot, state, ranges, budget, color, lineWidth, style);
        else
            SampleCurve(scene, plot, state, ranges, budget, color, lineWidth, style);
        if (!budget.exhausted && options)
            AddFeatures(scene, plot, state, ranges);
        if (budget.Stop())
            scene.has_missing_data = true;
    }
    return scene;
}

namespace
{
class Bitmap final : public Graphing::IBitmap
{
  public:
    explicit Bitmap(std::vector<BYTE> bytes) : m_bytes(std::move(bytes))
    {
    }
    std::vector<BYTE> GetData() const override
    {
        return m_bytes;
    }

  private:
    std::vector<BYTE> m_bytes;
};
class Renderer final : public Graphing::Renderer::IGraphRenderer
{
  public:
    explicit Renderer(std::shared_ptr<GraphState> state) : m_state(std::move(state))
    {
        ResetRange();
    }
    HRESULT SetGraphSize(unsigned width, unsigned height) override
    {
        if (!ValidSize(width, height))
            return E_INVALIDARG;
        m_width = width;
        m_height = height;
        return S_OK;
    }
    HRESULT SetDpi(float x, float y) override
    {
        if (!std::isfinite(x) || !std::isfinite(y) || x <= 0 || y <= 0 || x > 960 || y > 960)
            return E_INVALIDARG;
        m_dpiX = x;
        m_dpiY = y;
        return S_OK;
    }
    HRESULT ReadScene(Scene &out) const
    {
        out = {};
        if (!m_state)
            return E_FAIL;
        if (m_state->plots.empty())
            return m_state->initialization_error < 0 ? m_state->initialization_error : E_FAIL;
        try
        {
            out = BuildScene(*m_state, EffectiveRanges(), m_width, m_height);
            const float scale = (m_dpiX + m_dpiY) / 192;
            for (auto &stroke : out.strokes)
                stroke.width = std::clamp(stroke.width * scale, 0.5f, 1000.0f);
            return S_OK;
        }
        catch (const std::bad_alloc &)
        {
            out = {};
            return E_FAIL;
        }
    }
    HRESULT PrepareGraph() override
    {
        Scene scene;
        return ReadScene(scene);
    }
    HRESULT GetBitmap(std::shared_ptr<Graphing::IBitmap> &out, bool &missing) override
    {
        out.reset();
        missing = false;
        Scene scene;
        const auto result = ReadScene(scene);
        if (result < 0)
            return result;
        missing = scene.has_missing_data;
        try
        {
            auto bytes = EncodePng(scene);
            if (bytes.empty())
                return E_FAIL;
            out = std::make_shared<Bitmap>(std::move(bytes));
            return S_OK;
        }
        catch (const std::bad_alloc &)
        {
            return E_FAIL;
        }
    }
    HRESULT DrawD2D1(ID2D1Factory *factory, ID2D1RenderTarget *target, bool &missing) override
    {
        missing = false;
#ifdef _WIN32
        if (!factory || !target)
            return E_POINTER;
        Scene scene;
        const auto result = ReadScene(scene);
        if (result < 0)
            return result;
        missing = scene.has_missing_data;
        auto pixels = Rasterize(scene);
        if (pixels.empty())
            return E_FAIL;
        for (std::size_t index = 0; index < pixels.size(); index += 4)
        {
            const unsigned alpha = pixels[index + 3];
            const auto red = pixels[index];
            pixels[index] = static_cast<BYTE>(pixels[index + 2] * alpha / 255);
            pixels[index + 1] = static_cast<BYTE>(pixels[index + 1] * alpha / 255);
            pixels[index + 2] = static_cast<BYTE>(red * alpha / 255);
        }
        ID2D1Bitmap *bitmap = nullptr;
        const auto properties =
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), m_dpiX, m_dpiY);
        const auto hr = target->CreateBitmap(D2D1::SizeU(m_width, m_height), pixels.data(), m_width * 4, properties, &bitmap);
        if (hr < 0)
            return hr;
        const auto size = target->GetSize();
        target->DrawBitmap(bitmap, D2D1::RectF(0, 0, size.width, size.height));
        bitmap->Release();
        return S_OK;
#else
        (void)factory;
        (void)target;
        return E_NOTIMPL;
#endif
    }
    HRESULT GetDisplayRanges(double &xmin, double &xmax, double &ymin, double &ymax) override
    {
        const auto ranges = EffectiveRanges();
        xmin = ranges.x_min;
        xmax = ranges.x_max;
        ymin = ranges.y_min;
        ymax = ranges.y_max;
        return S_OK;
    }
    HRESULT SetDisplayRanges(double xmin, double xmax, double ymin, double ymax) override
    {
        if (!IsValidRange(xmin, xmax) || !IsValidRange(ymin, ymax))
            return E_INVALIDARG;
        m_ranges = {xmin, xmax, ymin, ymax};
        return S_OK;
    }
    HRESULT ResetRange() override
    {
        if (!m_state)
            return E_FAIL;
        const auto x = m_state->options ? m_state->options->GetDefaultXRange() : std::pair<double, double>{-10, 10};
        const auto y = m_state->options ? m_state->options->GetDefaultYRange() : std::pair<double, double>{-10, 10};
        return SetDisplayRanges(x.first, x.second, y.first, y.second);
    }
    HRESULT ScaleRange(double x, double y, double scale) override
    {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(scale) || scale <= 0)
            return E_INVALIDARG;
        const auto ranges = EffectiveRanges();
        // Calculator supplies normalized coordinates: zero is the viewport center, +1 is its positive edge.
        const double centerX = ranges.x_min / 2 + ranges.x_max / 2 + x * (ranges.x_max - ranges.x_min) / 2;
        const double centerY = ranges.y_min / 2 + ranges.y_max / 2 + y * (ranges.y_max - ranges.y_min) / 2;
        return SetDisplayRanges(centerX + (ranges.x_min - centerX) * scale, centerX + (ranges.x_max - centerX) * scale,
                                centerY + (ranges.y_min - centerY) * scale, centerY + (ranges.y_max - centerY) * scale);
    }
    HRESULT MoveRangeByRatio(double x, double y) override
    {
        if (!std::isfinite(x) || !std::isfinite(y))
            return E_INVALIDARG;
        const auto ranges = EffectiveRanges();
        const auto dx = (ranges.x_max - ranges.x_min) * x / 2, dy = (ranges.y_max - ranges.y_min) * y / 2;
        return SetDisplayRanges(ranges.x_min + dx, ranges.x_max + dx, ranges.y_min + dy, ranges.y_max + dy);
    }
    HRESULT ChangeRange(Graphing::Renderer::ChangeRangeAction action) override
    {
        using Graphing::Renderer::ChangeRangeAction;
        const auto r = EffectiveRanges();
        const double x = r.x_min / 2 + r.x_max / 2, y = r.y_min / 2 + r.y_max / 2;
        switch (action)
        {
        case ChangeRangeAction::ZoomIn:
        case ChangeRangeAction::PinchZoomIn:
            return ScaleRange(0, 0, 0.8);
        case ChangeRangeAction::ZoomOut:
        case ChangeRangeAction::PinchZoomOut:
            return ScaleRange(0, 0, 1.25);
        case ChangeRangeAction::SmoothZoomIn:
            return ScaleRange(0, 0, 0.95);
        case ChangeRangeAction::SmoothZoomOut:
            return ScaleRange(0, 0, 1 / 0.95);
        case ChangeRangeAction::WidenX:
            return SetDisplayRanges(x + (r.x_min - x) * 1.25, x + (r.x_max - x) * 1.25, r.y_min, r.y_max);
        case ChangeRangeAction::ShrinkX:
            return SetDisplayRanges(x + (r.x_min - x) * 0.8, x + (r.x_max - x) * 0.8, r.y_min, r.y_max);
        case ChangeRangeAction::WidenY:
            return SetDisplayRanges(r.x_min, r.x_max, y + (r.y_min - y) * 1.25, y + (r.y_max - y) * 1.25);
        case ChangeRangeAction::ShrinkY:
            return SetDisplayRanges(r.x_min, r.x_max, y + (r.y_min - y) * 0.8, y + (r.y_max - y) * 0.8);
        case ChangeRangeAction::MoveNegativeX:
            return MoveRangeByRatio(-0.1, 0);
        case ChangeRangeAction::MovePositiveX:
            return MoveRangeByRatio(0.1, 0);
        case ChangeRangeAction::MoveNegativeY:
            return MoveRangeByRatio(0, -0.1);
        case ChangeRangeAction::MovePositiveY:
            return MoveRangeByRatio(0, 0.1);
        case ChangeRangeAction::WidenZ:
        case ChangeRangeAction::ShrinkZ:
        case ChangeRangeAction::MoveNegativeZ:
        case ChangeRangeAction::MovePositiveZ:
            return E_NOTIMPL;
        default:
            return E_INVALIDARG;
        }
    }
    HRESULT GetClosePointData(double sx, double sy, double precision, int &id, float &screenX, float &screenY, double &x, double &y,
                              double &rho, double &theta, double &t) override
    {
        id = -1;
        screenX = screenY = 0;
        x = y = rho = theta = t = std::numeric_limits<double>::quiet_NaN();
        if (!std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(precision) || precision < 0)
            return E_INVALIDARG;
        Scene scene;
        const auto result = ReadScene(scene);
        if (result < 0)
            return result;
        // RenderMain derives precision from the world x-span. It is a refinement tolerance, not a screen radius.
        double best = std::numeric_limits<double>::infinity();
        Point nearest, segmentA, segmentB;
        for (const auto &stroke : scene.strokes)
        {
            if (stroke.equation_id < 0)
                continue;
            for (std::size_t index = 1; index < stroke.points.size(); ++index)
            {
                const auto a = stroke.points[index - 1], b = stroke.points[index];
                const auto dx = b.x - a.x, dy = b.y - a.y;
                const double lengthSquared = dx * dx + dy * dy;
                const double fraction = lengthSquared == 0 ? 0 : std::clamp(((sx - a.x) * dx + (sy - a.y) * dy) / lengthSquared, 0.0, 1.0);
                const Point point{a.x + fraction * dx, a.y + fraction * dy};
                const double distance = std::hypot(point.x - sx, point.y - sy);
                if (distance <= best)
                {
                    best = distance;
                    nearest = point;
                    segmentA = a;
                    segmentB = b;
                    id = stroke.equation_id;
                }
            }
        }
        if (id < 0)
            return S_FALSE;
        const auto ranges = EffectiveRanges();
        auto value = World(nearest, ranges, m_width, m_height);
        const auto plot = std::find_if(m_state->plots.begin(), m_state->plots.end(),
                                       [id](const Plot &entry) { return entry.id == static_cast<unsigned>(id); });
        if (plot != m_state->plots.end() && plot->kind != PlotKind::Implicit && plot->kind != PlotKind::Inequality)
        {
            double parameter = 0, left = 0, right = 0;
            const auto distance = [&](double argument) {
                const auto point = EvaluatePlot(*plot, m_state->context, argument);
                if (!point)
                    return std::numeric_limits<double>::infinity();
                const auto screen = Screen(*point, ranges, m_width, m_height);
                if (screen.x < 0 || screen.x > m_width - 1 || screen.y < 0 || screen.y > m_height - 1)
                    return std::numeric_limits<double>::infinity();
                return std::hypot(screen.x - sx, screen.y - sy);
            };
            if (plot->kind == PlotKind::ExplicitY || plot->kind == PlotKind::ExplicitX)
            {
                const auto a = World(segmentA, ranges, m_width, m_height), b = World(segmentB, ranges, m_width, m_height);
                const bool explicitY = plot->kind == PlotKind::ExplicitY;
                parameter = explicitY ? value.x : value.y;
                const double first = explicitY ? a.x : a.y, second = explicitY ? b.x : b.y;
                const double step = std::abs(second - first);
                left = std::max(explicitY ? ranges.x_min : ranges.y_min, std::min(first, second) - step);
                right = std::min(explicitY ? ranges.x_max : ranges.y_max, std::max(first, second) + step);
            }
            else
            {
                double sampledBest = std::numeric_limits<double>::infinity();
                for (unsigned index = 0; index <= 2048; ++index)
                {
                    const double candidate = m_state->parameter_min + (m_state->parameter_max - m_state->parameter_min) * index / 2048;
                    const double candidateDistance = distance(candidate);
                    if (candidateDistance < sampledBest)
                    {
                        sampledBest = candidateDistance;
                        parameter = candidate;
                    }
                }
                const double step = (m_state->parameter_max - m_state->parameter_min) / 2048;
                left = std::max(m_state->parameter_min, parameter - step);
                right = std::min(m_state->parameter_max, parameter + step);
            }
            const double tolerance =
                std::max(precision, std::numeric_limits<double>::epsilon() * std::max({1.0, std::abs(left), std::abs(right)}) * 8);
            double bestParameter = parameter, bestDistance = distance(parameter);
            for (unsigned iteration = 0; iteration < 80 && right - left > tolerance; ++iteration)
            {
                const double a = left + (right - left) / 3, b = right - (right - left) / 3;
                const double distanceA = distance(a), distanceB = distance(b);
                if (distanceA < bestDistance)
                {
                    bestDistance = distanceA;
                    bestParameter = a;
                }
                if (distanceB < bestDistance)
                {
                    bestDistance = distanceB;
                    bestParameter = b;
                }
                if (distanceA < distanceB)
                    right = b;
                else
                    left = a;
            }
            if (std::isfinite(bestDistance))
            {
                const auto refined = EvaluatePlot(*plot, m_state->context, bestParameter);
                if (refined)
                {
                    value = *refined;
                    nearest = Screen(value, ranges, m_width, m_height);
                    if (plot->kind == PlotKind::Parametric)
                        t = bestParameter;
                }
            }
        }
        screenX = static_cast<float>(nearest.x);
        screenY = static_cast<float>(nearest.y);
        x = value.x;
        y = value.y;
        rho = std::hypot(x, y);
        theta = std::atan2(y, x);
        if (m_state->context.trig_unit == Graphing::EvalTrigUnitMode::Degrees)
            theta *= 180 / PI;
        if (m_state->context.trig_unit == Graphing::EvalTrigUnitMode::Grads)
            theta *= 200 / PI;
        return S_OK;
    }

  private:
    Ranges EffectiveRanges() const
    {
        return m_state ? Proportional(m_ranges, m_width, m_height, *m_state) : m_ranges;
    }
    std::shared_ptr<GraphState> m_state;
    Ranges m_ranges;
    unsigned m_width = 640, m_height = 480;
    float m_dpiX = 96, m_dpiY = 96;
};
} // namespace
std::shared_ptr<Graphing::Renderer::IGraphRenderer> CreateRenderer(std::shared_ptr<GraphState> state)
{
    return std::make_shared<Renderer>(std::move(state));
}
} // namespace graphing::detail

namespace graphing
{
HRESULT GetScene(const Graphing::Renderer::IGraphRenderer &renderer, Scene &out)
{
    out = {};
    const auto *portable = dynamic_cast<const detail::Renderer *>(&renderer);
    return portable ? portable->ReadScene(out) : E_NOTIMPL;
}
} // namespace graphing
