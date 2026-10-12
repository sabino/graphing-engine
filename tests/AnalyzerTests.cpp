#include "Core.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace graphing::detail;
namespace
{
void Require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
NodePtr NumberNode(double value)
{
    auto node = std::make_shared<Node>();
    node->kind = NodeKind::Number;
    node->number = value;
    return node;
}
NodePtr VariableNode(const std::wstring &name = L"x")
{
    auto node = std::make_shared<Node>();
    node->kind = NodeKind::Variable;
    node->name = name;
    return node;
}
NodePtr Binary(const std::wstring &name, NodePtr a, NodePtr b)
{
    auto node = std::make_shared<Node>();
    node->kind = NodeKind::Binary;
    node->name = name;
    node->children = {a, b};
    return node;
}
NodePtr Call(const std::wstring &name, NodePtr argument)
{
    auto node = std::make_shared<Node>();
    node->kind = NodeKind::Call;
    node->name = name;
    node->children = {argument};
    return node;
}
std::shared_ptr<GraphState> State(NodePtr expression)
{
    auto state = std::make_shared<GraphState>();
    state->options = CreateGraphOptions();
    Plot plot;
    plot.kind = PlotKind::ExplicitY;
    plot.first = std::move(expression);
    plot.id = 7;
    state->plots.push_back(std::move(plot));
    return state;
}
void CheckMath(const std::wstring &value)
{
    Require(value.find(L"<math xmlns=\"http://www.w3.org/1998/Math/MathML\">") == 0, "analysis must return MathML");
    Require(value.rfind(L"</math>") == value.size() - 7, "MathML must be closed");
}
void TestRepeatedRoots()
{
    auto state = State(Binary(L"^", Binary(L"-", VariableNode(), NumberNode(1)), NumberNode(4)));
    auto analyzer = CreateAnalyzer(state);
    Require(analyzer->PerformFunctionAnalysis(255) == S_OK, "polynomial analysis failed");
    auto data = ReadAnalysis(analyzer.get());
    Require(data.TooComplexFeatures == 0, "simple repeated-root polynomial must be supported");
    Require(data.Minima.size() == 1 && data.Maxima.empty(), "quartic must have one minimum");
    Require(data.InflectionPoints.empty(), "stationary second derivative is not an inflection");
    Require(data.PeriodicityDirection == 2, "nonconstant polynomial is not periodic");
    CheckMath(data.Domain);
    CheckMath(data.Range);
    CheckMath(data.Zeros);
    CheckMath(data.Minima.front());
    const auto features = GetFeatureGeometry(*state, 7);
    Require(features.zeros.size() == 1 && std::abs(features.zeros[0].x - 1) < 1e-8, "repeated root must occur once");
    Require(features.minima.size() == 1 && std::abs(features.minima[0].y) < 1e-8, "quartic minimum geometry failed");
}
void TestHoleAndPole()
{
    const auto numerator = Binary(L"-", Binary(L"^", VariableNode(), NumberNode(2)), NumberNode(1));
    const auto denominator = Binary(L"-", VariableNode(), NumberNode(1));
    auto state = State(Binary(L"/", numerator, denominator));
    auto analyzer = CreateAnalyzer(state);
    Require(analyzer->PerformFunctionAnalysis(255) == S_OK, "hole analysis failed");
    const auto hole = ReadAnalysis(analyzer.get());
    Require(hole.Domain.find(L"≠") != std::wstring::npos, "cancelled denominator must remain excluded");
    Require(hole.VerticalAsymptotes.empty(), "removable hole is not a vertical asymptote");
    Require(hole.Range.empty() && (hole.TooComplexFeatures & 2), "uncertified rational range must be unknown");
    auto features = GetFeatureGeometry(*state, 7);
    Require(features.zeros.size() == 1 && std::abs(features.zeros[0].x + 1) < 1e-8, "hole must not become a zero");
    state->plots[0].first = Binary(L"/", numerator, Binary(L"^", denominator, NumberNode(2)));
    Require(ReadAnalysis(analyzer.get()).Domain.empty(), "changed AST must invalidate analysis");
    Require(analyzer->PerformFunctionAnalysis(255) == S_OK, "pole analysis failed");
    features = GetFeatureGeometry(*state, 7);
    Require(features.vertical_asymptotes.size() == 1 && std::abs(features.vertical_asymptotes[0] - 1) < 1e-8,
            "uncancelled pole must remain");
    Require(features.horizontal_asymptotes.size() == 1 && std::abs(features.horizontal_asymptotes[0] - 1) < 1e-8,
            "rational horizontal asymptote failed");
}
void TestCertifiedAbsenceAndRealDomainSymmetry()
{
    const auto positive = Binary(L"+", Binary(L"^", VariableNode(), NumberNode(2)), NumberNode(1));
    auto state = State(positive);
    auto analyzer = CreateAnalyzer(state);
    Require(analyzer->PerformFunctionAnalysis(255) == S_OK, "positive polynomial analysis failed");
    Require(ReadAnalysis(analyzer.get()).Zeros.find(L"<mo>{</mo><mo>}</mo>") != std::wstring::npos,
            "no-real-roots claim needs global isolation");
    const auto no_real_holes = Binary(L"+", Binary(L"+", Binary(L"^", VariableNode(), NumberNode(2)), VariableNode()), NumberNode(1));
    state->plots[0].first = Binary(L"/", no_real_holes, no_real_holes);
    Require(analyzer->PerformFunctionAnalysis(255) == S_OK, "complex-only exclusions analysis failed");
    const auto data = ReadAnalysis(analyzer.get());
    Require(data.Parity == 2 && data.PeriodicityDirection == 1, "non-real denominator roots cannot break real domain symmetry");
    Require(data.Domain.find(L"≠") == std::wstring::npos, "non-real roots must not exclude real inputs");
}
void TestRequestAndUnknown()
{
    auto state = State(Binary(L"^", VariableNode(), NumberNode(2)));
    auto analyzer = CreateAnalyzer(state);
    Require(analyzer->PerformFunctionAnalysis(1) == S_OK, "domain-only request failed");
    auto data = ReadAnalysis(analyzer.get());
    Require(!data.Domain.empty() && data.Range.empty() && data.Zeros.empty() && data.Parity == 0, "unrequested fields must stay empty");
    Require(analyzer->PerformFunctionAnalysis(256) == E_INVALIDARG, "unknown request flags must be rejected");
    state->plots[0].first = Call(L"sin", Binary(L"^", VariableNode(), NumberNode(2)));
    Require(analyzer->PerformFunctionAnalysis(255) == S_OK, "unsupported function should return feature status");
    data = ReadAnalysis(analyzer.get());
    Require(data.Domain.empty() && data.Range.empty() && data.Zeros.empty(), "unsupported analysis must not fabricate global features");
    Require(data.TooComplexFeatures == 8191, "unsupported features must all be marked unknown");
    state->plots[0].kind = PlotKind::Implicit;
    bool variable_is_not_x = true;
    Require(!analyzer->CanFunctionAnalysisBePerformed(variable_is_not_x), "implicit curves cannot be analyzed as functions");
    Require(analyzer->PerformFunctionAnalysis(255) == E_NOTIMPL, "unsupported plot kind must be rejected");
}
void TestSelectionAndParameters()
{
    auto state = State(Binary(L"*", VariableNode(L"a"), VariableNode()));
    state->context.variables[L"a"] = 2;
    state->options->SetAllowKeyGraphFeaturesForFunctionsWithParameters(false);
    auto analyzer = CreateAnalyzer(state);
    bool other = false;
    Require(!analyzer->CanFunctionAnalysisBePerformed(other), "parameter policy must be respected");
    state->options->SetAllowKeyGraphFeaturesForFunctionsWithParameters(true);
    Require(analyzer->CanFunctionAnalysisBePerformed(other), "assigned permitted parameter should be analyzable");
    Require(analyzer->PerformFunctionAnalysis(255) == S_OK, "parameter analysis failed");
    state->context.variables[L"a"] = -2;
    Require(ReadAnalysis(analyzer.get()).Domain.empty(), "parameter mutation must invalidate results");
    Require(analyzer->PerformFunctionAnalysis(255) == S_OK, "parameter reanalysis failed");
    Require(ReadAnalysis(analyzer.get()).MonotoneIntervals.begin()->second == 2, "parameter sign must change monotonicity");
    Plot second;
    second.kind = PlotKind::ExplicitY;
    second.first = Binary(L"^", VariableNode(), NumberNode(2));
    second.id = 9;
    second.selected = true;
    state->plots.push_back(second);
    Require(analyzer->PerformFunctionAnalysis(255) == S_OK, "selected plot analysis failed");
    Require(ReadAnalysis(analyzer.get()).Minima.size() == 1, "selected equation must control analysis");
    state->plots[0].selected = true;
    Require(ReadAnalysis(analyzer.get()).Domain.empty(), "selection mutation must invalidate results");
    Require(!analyzer->CanFunctionAnalysisBePerformed(other), "ambiguous multiple selection must be rejected");
    state->plots[0].selected = false;
    state->plots[1].kind = PlotKind::ExplicitX;
    state->plots[1].first = VariableNode(L"y");
    Require(analyzer->CanFunctionAnalysisBePerformed(other) && other, "explicit x must report non-x independent variable");
}
void TestElementary()
{
    auto state = State(Call(L"sqrt", Binary(L"-", VariableNode(), NumberNode(2))));
    auto analyzer = CreateAnalyzer(state);
    Require(analyzer->PerformFunctionAnalysis(255) == S_OK, "affine square root failed");
    auto data = ReadAnalysis(analyzer.get());
    Require(data.TooComplexFeatures == 0 && data.Minima.size() == 1, "square root boundary minimum should be known");
    Require(data.YIntercept.find(L"∅") != std::wstring::npos, "square root outside domain has no intercept");
    state->plots[0].first = Call(L"sin", Binary(L"*", NumberNode(2), VariableNode()));
    Require(analyzer->PerformFunctionAnalysis(255) == S_OK, "affine sine failed");
    data = ReadAnalysis(analyzer.get());
    Require(data.Parity == 1 && data.PeriodicityDirection == 1, "affine sine parity and period failed");
    Require(data.Zeros.find(L"ℤ") != std::wstring::npos, "infinite zeros need a global integer family");
    Require(data.TooComplexFeatures == (448 | 4096), "unsupported trig features must remain unknown");
    state->context.trig_unit = Graphing::EvalTrigUnitMode::Degrees;
    Require(ReadAnalysis(analyzer.get()).Domain.empty(), "angle mode changes must invalidate results");
    Require(analyzer->PerformFunctionAnalysis(128) == S_OK, "degree period failed");
    Require(ReadAnalysis(analyzer.get()).PeriodicityExpression.find(L"180") != std::wstring::npos, "degree period must use degree angles");
}
} // namespace
int main()
{
    try
    {
        TestRepeatedRoots();
        TestHoleAndPole();
        TestCertifiedAbsenceAndRealDomainSymmetry();
        TestRequestAndUnknown();
        TestSelectionAndParameters();
        TestElementary();
        std::cout << "Analyzer tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
