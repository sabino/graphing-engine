#include "GraphingInterfaces/IBitmap.h"
#include "GraphingInterfaces/IMathSolver.h"
#include "graphing/Scene.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <d2d1.h>
#include <d2d1helper.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif

namespace
{
void Check(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::unique_ptr<Graphing::IExpression> Parse(Graphing::IMathSolver &solver, const std::wstring &text)
{
    int code = 777, type = 777;
    auto expression = solver.ParseInput(text, code, type);
    if (!expression || code != 0 || type != 0)
        throw std::runtime_error("Valid input failed through public contract, code " + std::to_string(code) + ", type " +
                                 std::to_string(type));
    return expression;
}

void CheckPublicContract()
{
    auto solver = Graphing::IMathSolver::CreateMathSolver();
    auto expression = Parse(*solver, L"y=a*x^2-4");
    Check(expression->GetExpressionID() != 0 && !expression->IsEmptySet(), "Expression identity unavailable");
    auto graph = solver->CreateGrapher(expression.get());
    expression.reset();
    const auto equations = graph->TryInitialize();
    Check(equations && equations->size() == 1 && equations->front(), "Expression ownership or equation initialization failed");
    Check(equations->front()->GetGraphEquationID() == 0, "Equation identity must be the zero-based palette index");
    Check(graph->GetInitializationError() == S_OK, "Successful initialization left an error");
    graph->SetArgValue(L"a", 2);
    const auto variables = graph->GetVariables();
    bool found_a = false;
    for (const auto &variable : variables)
        found_a = found_a || variable->GetVariableName() == L"a";
    Check(found_a, "Parameter missing from public variable list");
    Check(equations->front()->TrySelectEquation() && equations->front()->IsEquationSelected(), "Equation selection failed");
    graph->TryResetSelection();
    Check(!equations->front()->IsEquationSelected(), "Selection reset failed");
    auto renderer = graph->GetRenderer();
    Check(renderer->SetGraphSize(320, 240) == S_OK, "Graph size rejected");
    Check(renderer->PrepareGraph() == S_OK, "Graph preparation failed");
    std::shared_ptr<Graphing::IBitmap> bitmap;
    bool missing = true;
    Check(renderer->GetBitmap(bitmap, missing) == S_OK && bitmap, "Public bitmap export failed");
    const auto bytes = bitmap->GetData();
    Check(bytes.size() > 32 && bytes[0] == 137 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G',
          "Bitmap is not an encoded PNG image");
    auto analyzer = graph->GetAnalyzer();
    graph->GetOptions().SetAllowKeyGraphFeaturesForFunctionsWithParameters(true);
    bool not_x = true;
    Check(analyzer->CanFunctionAnalysisBePerformed(not_x) && !not_x, "Parameterized analysis unavailable");
    Check(analyzer->PerformFunctionAnalysis(0xff) == S_OK, "Polynomial analysis failed");
    const auto features = solver->Analyze(analyzer.get());
    Check(features.TooComplexFeatures == 0 && !features.Domain.empty() && !features.Zeros.empty(),
          "Certified polynomial features incomplete");

    auto unsupported = Parse(*solver, L"x+y");
    Check(!graph->TryInitialize(unsupported.get()) && graph->GetInitializationError() < 0,
          "Unsupported bare implicit graph silently accepted");
    Check(renderer->PrepareGraph() == S_OK, "Rejected request destroyed previous graph");
    auto replacement = Parse(*solver, L"y=x");
    const auto replacement_equations = graph->TryInitialize(replacement.get());
    Check(replacement_equations && replacement_equations->size() == 1 && replacement_equations->front(), "Replacement graph failed");
    Check(replacement_equations->front()->GetGraphEquationID() == 0, "Equation palette indices must restart after initialization");
    Check(replacement_equations->front()->TrySelectEquation(), "Replacement equation should select");
    Check(!equations->front()->TrySelectEquation() && !equations->front()->IsEquationSelected(),
          "Stale equation must remain detached when a replacement reuses its palette index");
    Check(replacement_equations->front()->IsEquationSelected(), "Stale selection must not change a replacement equation");
    int code = 0, type = 0;
    Check(!solver->ParseInput(L"y=", code, type) && code != 0, "Invalid syntax accepted");
    solver->HRErrorToErrorInfo(E_ABORT, code, type);
    Check(code == 4 && type == 2, "Abort result mapping failed");
    solver->HRErrorToErrorInfo(S_OK, code, type);
    Check(code == 0 && type == 0, "Success left stale error outputs");
}

void CheckCalculatorMathML()
{
    auto solver = Graphing::IMathSolver::CreateMathSolver();
    solver->ParsingOptions().SetFormatType(Graphing::FormatType::MathML);
    solver->FormatOptions().SetFormatType(Graphing::FormatType::LinearInput);
    const auto expression =
        Parse(*solver, L"<math xmlns=\"http://www.w3.org/1998/Math/MathML\"><mrow><mi>show2d</mi><mfenced separators=\"\">"
                       L"<mrow><mi>plotEq2d</mi><mfenced separators=\"\"><mrow><mi>y</mi><mo>=</mo>"
                       L"<msup><mi>x</mi><mn>2</mn></msup></mrow></mfenced></mrow><mo>,</mo>"
                       L"<mrow><mi>plotIneq2D</mi><mfenced separators=\"\"><mrow><mi>y</mi><mo>&lt;</mo>"
                       L"<mn>1</mn></mrow></mfenced></mrow></mfenced></mrow></math>");
    auto graph = solver->CreateGrapher();
    const auto equations = graph->TryInitialize(expression.get());
    Check(equations && equations->size() == 2, "Calculator multi-equation MathML request failed");
    Check((*equations)[0]->GetGraphEquationID() == 0 && (*equations)[1]->GetGraphEquationID() == 1,
          "Multiple equation identities must match palette order");
    graphing::Scene scene;
    Check(graphing::GetScene(*graph->GetRenderer(), scene) == S_OK && !scene.fills.empty(), "Calculator inequality MathML did not render");
    const auto colors = std::vector<Graphing::Color>{{120, 30, 50}, {20, 90, 40}};
    Check(graph->GetOptions().SetGraphColors(colors), "Graph palette rejected");
    Check(static_cast<std::uint32_t>(equations->front()->GetGraphEquationOptions()->GetGraphColor()) ==
              static_cast<std::uint32_t>(colors[0]),
          "Palette failed to update equation color");
    const auto serialized = solver->Serialize(expression.get());
    Check(!serialized.empty(), "Linear serialization of Calculator request failed");
    solver->FormatOptions().SetFormatType(Graphing::FormatType::MathML);
    solver->FormatOptions().SetMathMLPrefix(L"mml:");
    const auto prefixed = solver->Serialize(expression.get());
    Check(prefixed.find(L"mml:") != std::wstring::npos, "MathML namespace prefix lost");
}

#ifdef _WIN32
void CheckWindowsDrawing()
{
    using Microsoft::WRL::ComPtr;
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM initialization failed");
    ComPtr<IWICImagingFactory> imaging;
    Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&imaging))),
          "WIC factory creation failed");
    auto solver = Graphing::IMathSolver::CreateMathSolver();
    auto expression = Parse(*solver, L"y=x^2-4");
    auto graph = solver->CreateGrapher(expression.get());
    Check(graph->TryInitialize().has_value(), "Windows graph initialization failed");
    auto renderer = graph->GetRenderer();
    Check(renderer->SetGraphSize(320, 240) == S_OK, "Windows graph sizing failed");
    std::shared_ptr<Graphing::IBitmap> encoded;
    bool missing = false;
    Check(renderer->GetBitmap(encoded, missing) == S_OK, "Windows PNG encoding failed");
    auto bytes = encoded->GetData();
    ComPtr<IWICStream> stream;
    Check(SUCCEEDED(imaging->CreateStream(&stream)), "WIC stream creation failed");
    Check(SUCCEEDED(stream->InitializeFromMemory(bytes.data(), static_cast<DWORD>(bytes.size()))), "WIC stream initialization failed");
    ComPtr<IWICBitmapDecoder> decoder;
    Check(SUCCEEDED(imaging->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)),
          "WIC PNG decoding failed");
    ComPtr<IWICBitmapFrameDecode> frame;
    Check(SUCCEEDED(decoder->GetFrame(0, &frame)), "WIC image frame unavailable");
    UINT width = 0, height = 0;
    Check(SUCCEEDED(frame->GetSize(&width, &height)) && width == 320 && height == 240, "WIC PNG dimensions changed");

    ComPtr<ID2D1Factory> factory;
    Check(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf())),
          "Direct2D factory creation failed");
    ComPtr<IWICBitmap> bitmap;
    Check(SUCCEEDED(imaging->CreateBitmap(320, 240, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap)),
          "WIC drawing bitmap creation failed");
    ComPtr<ID2D1RenderTarget> target;
    const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE);
    Check(SUCCEEDED(factory->CreateWicBitmapRenderTarget(bitmap.Get(), properties, &target)), "Direct2D software target creation failed");
    target->BeginDraw();
    Check(renderer->DrawD2D1(factory.Get(), target.Get(), missing) == S_OK, "Native Direct2D drawing failed");
    Check(SUCCEEDED(target->EndDraw()), "Direct2D drawing did not complete");
    // Release COM resources before balancing initialization.
    target.Reset();
    bitmap.Reset();
    factory.Reset();
    frame.Reset();
    decoder.Reset();
    stream.Reset();
    imaging.Reset();
    CoUninitialize();
}
#endif
} // namespace

int main()
{
    try
    {
        CheckPublicContract();
        CheckCalculatorMathML();
#ifdef _WIN32
        CheckWindowsDrawing();
#endif
        std::cout << "Public GraphingInterfaces integration passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
