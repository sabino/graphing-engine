#include "Text.h"
#include "graphing/Scene.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
double ParseNumber(const std::string &text)
{
    std::size_t consumed = 0;
    const double value = std::stod(text, &consumed);
    if (consumed != text.size() || !std::isfinite(value))
        throw std::invalid_argument("Expected a finite number");
    return value;
}

void RequireSuccess(HRESULT result, const char *operation)
{
    if (result < 0)
        throw std::runtime_error(operation);
}
} // namespace

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        std::cerr << "Usage: graphing-cli 'y=sin(x)' graph.svg|graph.png|graph.bmp [--mathml] [--degrees] [--set a=2] [--range xmin xmax "
                     "ymin ymax]\n";
        return 2;
    }
    try
    {
        auto solver = Graphing::IMathSolver::CreateMathSolver();
        std::map<std::wstring, double> parameters;
        double xmin = -10, xmax = 10, ymin = -10, ymax = 10;
        for (int index = 3; index < argc; ++index)
        {
            const std::string flag = argv[index];
            if (flag == "--mathml")
                solver->ParsingOptions().SetFormatType(Graphing::FormatType::MathML);
            else if (flag == "--degrees")
                solver->EvalOptions().SetTrigUnitMode(Graphing::EvalTrigUnitMode::Degrees);
            else if (flag == "--set" && index + 1 < argc)
            {
                const std::string assignment = argv[++index];
                const auto equals = assignment.find('=');
                if (equals == std::string::npos || equals == 0)
                    throw std::invalid_argument("Use --set name=value");
                parameters.emplace(graphing::detail::DecodeUtf8(assignment.substr(0, equals)), ParseNumber(assignment.substr(equals + 1)));
            }
            else if (flag == "--range" && index + 4 < argc)
            {
                xmin = ParseNumber(argv[++index]);
                xmax = ParseNumber(argv[++index]);
                ymin = ParseNumber(argv[++index]);
                ymax = ParseNumber(argv[++index]);
            }
            else
                throw std::invalid_argument("Unknown or incomplete option: " + flag);
        }
        int code = 0, type = 0;
        const auto expression = solver->ParseInput(graphing::detail::DecodeUtf8(argv[1]), code, type);
        if (!expression)
            throw std::invalid_argument("Parse failed, code " + std::to_string(code) + ", type " + std::to_string(type));
        const auto graph = solver->CreateGrapher(expression.get());
        if (!graph || !graph->TryInitialize())
            throw std::invalid_argument("Unsupported graph request");
        for (const auto &parameter : parameters)
            graph->SetArgValue(parameter.first, parameter.second);
        auto renderer = graph->GetRenderer();
        RequireSuccess(renderer->SetGraphSize(960, 640), "Graph size failed");
        RequireSuccess(renderer->SetDisplayRanges(xmin, xmax, ymin, ymax), "Invalid ranges");
        RequireSuccess(renderer->PrepareGraph(), "Graph preparation failed");
        graphing::Scene scene;
        RequireSuccess(graphing::GetScene(*renderer, scene), "Scene unavailable");
        const std::string path = argv[2];
        if (path.size() < 4 ||
            (path.substr(path.size() - 4) != ".svg" && path.substr(path.size() - 4) != ".bmp" && path.substr(path.size() - 4) != ".png"))
            throw std::invalid_argument("Output must end in .svg, .png, or .bmp");
        std::ofstream output(path, std::ios::binary);
        if (!output)
            throw std::runtime_error("Cannot open output file");
        if (path.size() >= 4 && path.substr(path.size() - 4) == ".svg")
        {
            const auto text = graphing::detail::EncodeUtf8(graphing::SerializeSvg(scene));
            output.write(text.data(), static_cast<std::streamsize>(text.size()));
        }
        else
        {
            const auto data = path.substr(path.size() - 4) == ".png" ? graphing::EncodePng(scene) : graphing::EncodeBitmap(scene);
            if (data.empty())
                throw std::runtime_error("Bitmap encoding failed");
            output.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
        }
        if (!output)
            throw std::runtime_error("Output write failed");
        std::cout << "Rendered " << scene.width << 'x' << scene.height << " graph to " << path << '\n';
        if (scene.has_missing_data)
            std::cout << "Graph contains missing or unfinished data\n";
        const auto analyzer = graph->GetAnalyzer();
        bool not_x = false;
        if (analyzer->CanFunctionAnalysisBePerformed(not_x) && !not_x)
        {
            const auto flags = static_cast<unsigned>(Graphing::Analyzer::PerformAnalysisType::PerformAnalysisType_All);
            if (analyzer->PerformFunctionAnalysis(flags) == S_OK)
            {
                const auto data = solver->Analyze(analyzer.get());
                std::cout << "Unresolved analysis features: " << data.TooComplexFeatures << '\n';
            }
        }
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
