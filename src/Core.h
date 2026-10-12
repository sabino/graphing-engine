#pragma once

#include "Expression.h"
#include "graphing/Scene.h"
#include <chrono>
#include <optional>

namespace graphing::detail
{
enum class PlotKind
{
    ExplicitY,
    ExplicitX,
    Implicit,
    Inequality,
    Polar,
    Parametric,
    Unsupported
};
struct Plot
{
    PlotKind kind = PlotKind::Unsupported;
    NodePtr first;
    NodePtr second;
    std::wstring relation;
    std::shared_ptr<Graphing::IEquationOptions> options;
    unsigned id = 0;
    bool selected = false;
};

struct GraphState
{
    std::vector<Plot> plots;
    EvalContext context;
    std::shared_ptr<Graphing::IGraphingOptions> options;
    HRESULT initialization_error = S_OK;
    std::uint64_t revision = 0;
    std::uint64_t initialization_generation = 0;
    double parameter_min = -10;
    double parameter_max = 10;
};

struct Ranges
{
    double x_min = -10;
    double x_max = 10;
    double y_min = -10;
    double y_max = 10;
};

struct FeatureGeometry
{
    std::vector<Point> zeros;
    std::optional<Point> y_intercept;
    std::vector<Point> minima;
    std::vector<Point> maxima;
    std::vector<Point> inflections;
    std::vector<double> vertical_asymptotes;
    std::vector<double> horizontal_asymptotes;
    std::vector<std::pair<double, double>> oblique_asymptotes;
};
FeatureGeometry GetFeatureGeometry(const GraphState &state, unsigned plot_id);

std::shared_ptr<Graphing::IEquationOptions> CreateEquationOptions(Graphing::Color color);
std::shared_ptr<Graphing::IGraphingOptions> CreateGraphOptions(std::weak_ptr<GraphState> state = {});
std::shared_ptr<Graphing::Renderer::IGraphRenderer> CreateRenderer(std::shared_ptr<GraphState> state);
std::shared_ptr<Graphing::Analyzer::IGraphAnalyzer> CreateAnalyzer(std::shared_ptr<GraphState> state);
Graphing::IGraphFunctionAnalysisData ReadAnalysis(const Graphing::Analyzer::IGraphAnalyzer *analyzer);
Scene BuildScene(const GraphState &state, const Ranges &ranges, unsigned width, unsigned height);
std::optional<Point> EvaluatePlot(const Plot &plot, const EvalContext &context, double parameter);
bool IsValidRange(double minimum, double maximum);
} // namespace graphing::detail
