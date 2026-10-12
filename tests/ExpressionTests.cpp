// Licensed under the MIT License.
#include "Expression.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using Graphing::FormatType;
using Graphing::LocalizationType;
using graphing::detail::EvalContext;
using graphing::detail::Evaluate;
using graphing::detail::FindVariables;
using graphing::detail::NodeKind;
using graphing::detail::NodePtr;
using graphing::detail::Parse;
using graphing::detail::ParseErrorCode;
using graphing::detail::Serialize;

namespace
{
void Require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

NodePtr Read(const std::wstring &input, FormatType format = FormatType::Linear,
             LocalizationType localization = LocalizationType::DecimalPointAndListComma)
{
    const auto parsed = Parse(input, format, localization);
    Require(parsed.root != nullptr && parsed.error_code == 0, "Valid expression failed to parse.");
    return parsed.root;
}

void CheckValue(const std::wstring &input, double expected, const EvalContext &context = {})
{
    const auto actual = Evaluate(Read(input), context);
    Require(std::isfinite(actual) && std::abs(actual - expected) <= 1e-11 * std::max(1.0, std::abs(expected)),
            "Expression returned an incorrect finite value.");
}

void CheckGrammar()
{
    CheckValue(L"2+3*4", 14);
    CheckValue(L"-2^2", -4);
    CheckValue(L"(-2)^2", 4);
    CheckValue(L"2^3^2", 512);
    CheckValue(L"2^-3", 0.125);
    CheckValue(L"2(3+1)", 8);
    CheckValue(L"2x+3(x+1)", 13, {{{L"x", 2}}, Graphing::EvalTrigUnitMode::Radians});
    CheckValue(L"x(x+1)", 6, {{{L"x", 2}}, Graphing::EvalTrigUnitMode::Radians});
    CheckValue(L"(2+3)4", 20);
    CheckValue(L"x 2", 4, {{{L"x", 2}}, Graphing::EvalTrigUnitMode::Radians});
    CheckValue(L"α+1", 3, {{{L"α", 2}}, Graphing::EvalTrigUnitMode::Radians});
    CheckValue(L"1.5e2+2e-2", 150.02);
    CheckValue(L"π", 3.14159265358979323846);
    CheckValue(L"5%2", 1);
    CheckValue(L"2<=3", 1);
    CheckValue(L"2≠2", 0);
    const auto variables = FindVariables(Read(L"a*x+x^2+y"));
    Require(variables == std::set<std::wstring>{L"a", L"x", L"y"}, "Variables must be distinct and retain names.");
    const auto list = Read(L"{x,x^2,sin(x)}");
    Require(list->kind == NodeKind::List && list->children.size() == 3, "List expressions must retain their elements.");
    Require(std::isnan(Evaluate(list, {})), "A list must not silently become a scalar.");
    Require(Read(L"x=y")->name == L"=", "Equation AST must retain its relation.");
}

void CheckFunctionsAndDomains()
{
    const EvalContext degrees{{}, Graphing::EvalTrigUnitMode::Degrees};
    const EvalContext grads{{}, Graphing::EvalTrigUnitMode::Grads};
    CheckValue(L"sin(30)", 0.5, degrees);
    CheckValue(L"asin(0.5)", 30, degrees);
    CheckValue(L"sin(100)", 1, grads);
    CheckValue(L"asin(1)", 100, grads);
    CheckValue(L"sin(pi/2)", 1);
    CheckValue(L"arccos(0)", 90, degrees);
    CheckValue(L"sec(0)", 1);
    CheckValue(L"csc(pi/2)", 1);
    CheckValue(L"cot(pi/4)", 1);
    CheckValue(L"asec(1)", 0);
    CheckValue(L"acsc(1)", 90, degrees);
    CheckValue(L"acot(0)", 90, degrees);
    CheckValue(L"sinh(1)", std::sinh(1), degrees);
    CheckValue(L"asinh(sinh(1))", 1, degrees);
    CheckValue(L"acosh(cosh(1))", 1);
    CheckValue(L"atanh(tanh(0.5))", 0.5);
    CheckValue(L"asech(sech(1))", 1);
    CheckValue(L"acsch(csch(1))", 1);
    CheckValue(L"acoth(coth(1))", 1);
    CheckValue(L"root(-8,3)", -2);
    CheckValue(L"cuberoot(-27)", -3);
    CheckValue(L"sqrt(9)+abs(-2)", 5);
    CheckValue(L"ln(e)", 1);
    CheckValue(L"log(100)", 2);
    CheckValue(L"log(8,2)", 3);
    CheckValue(L"log10(1000)", 3);
    CheckValue(L"exp(0)", 1);
    CheckValue(L"floor(1.8)+ceiling(1.2)+sgn(-3)", 2);
    CheckValue(L"min({3,1,2})+max(4,-1,2)", 5);
    for (const auto *expression : {L"1/0", L"sqrt(-1)", L"ln(0)", L"asin(2)", L"atanh(1)", L"root(-8,2)", L"root(2,0)", L"log(2,1)",
                                   L"exp(1000)", L"0^0", L"0^-1", L"5.5%2", L"missing+1", L"csch(0)"})
        Require(std::isnan(Evaluate(Read(expression), {})), "Undefined real values must return NaN.");
    Require(std::isnan(Evaluate(Read(L"tan(90)"), degrees)), "A trig pole must be undefined.");
    Require(std::isnan(Evaluate(Read(L"x"), {{{L"x", std::numeric_limits<double>::infinity()}}, Graphing::EvalTrigUnitMode::Radians})),
            "Nonfinite external variable values must be rejected.");
}

void CheckFormats()
{
    const auto formula = Read(L"Sum[1,Divide[2,x]]", FormatType::Formula);
    Require(Evaluate(formula, {{{L"x", 2}}, Graphing::EvalTrigUnitMode::Radians}) == 2,
            "Formula example from GraphingEnums must evaluate.");
    const auto aggregate = Read(L"Aggregate[Sum[1,2]]", FormatType::FormulaWithoutAggregate);
    Require(Evaluate(aggregate, {}) == 3, "Formula aggregate must be removed without changing its value.");
    const auto linear = Read(L"1.25+2/x+sin(x)");
    const EvalContext context{{{L"x", 2}}, Graphing::EvalTrigUnitMode::Radians};
    for (const auto format : {FormatType::Linear, FormatType::LinearInput, FormatType::Formula, FormatType::MathML})
    {
        for (const auto localization : {LocalizationType::DecimalPointAndListComma, LocalizationType::DecimalPointAndListSemicolon,
                                        LocalizationType::DecimalCommaAndListSemicolon})
        {
            const auto text = Serialize(linear, format, localization, L"mml");
            const auto roundTrip = Read(text, format, localization);
            Require(std::abs(Evaluate(roundTrip, context) - Evaluate(linear, context)) < 1e-12,
                    "Supported formats and localization must round trip without changing values.");
        }
    }
    const auto comma = Read(L"max(1,5;2,5)", FormatType::Linear, LocalizationType::DecimalCommaAndListSemicolon);
    Require(Evaluate(comma, {}) == 2.5, "Decimal-comma input must distinguish its semicolon list separator.");
    const auto ids = Serialize(Read(L"z+x+z"), FormatType::InvariantFormula, LocalizationType::Unknown);
    Require(ids == L"Sum[Sum[Var(1),Var(0)],Var(1)]", "Invariant IDs must use a stable sorted variable order.");
    const auto mathml = Read(L"<math><mfrac><mn>2</mn><mi>x</mi></mfrac></math>", FormatType::MathML);
    Require(Evaluate(mathml, context) == 1, "MathML fraction must preserve its operands.");
    Require(Evaluate(Read(L"<math><mroot><mn>27</mn><mn>3</mn></mroot></math>", FormatType::MathML), {}) == 3,
            "MathML indexed root must preserve radicand and degree.");
    const auto noWrapper = Serialize(mathml, FormatType::MathMLNoWrapper, LocalizationType::Unknown, L"mml");
    Require(noWrapper.find(L"<mml:mfrac>") == 0 && noWrapper.find(L"<mml:math") == std::wstring::npos,
            "MathMLNoWrapper must omit only the root wrapper.");
    Require(Serialize(mathml, FormatType::Latex, LocalizationType::Unknown) == L"\\frac{2}{x}",
            "LaTeX fraction output must retain numerator and denominator.");
    const auto request =
        Read(L"<math xmlns=\"http://www.w3.org/1998/Math/MathML\"><mrow><mi>show2d</mi><mfenced separators=\"\">"
             L"<mrow><mi>plotEq2d</mi><mfenced separators=\"\"><math><mrow><mi>y</mi><mo>=</mo><msup><mi>x</mi><mn>2</mn></msup>"
             L"</mrow></math></mfenced></mrow><mrow><mi>plotIneq2D</mi><mfenced separators=\"\"><math><mrow><mi>y</mi><mo>&#x2264;</mo>"
             L"<mi>x</mi></mrow></math></mfenced></mrow></mfenced></mrow></math>",
             FormatType::MathML);
    Require(request->kind == NodeKind::Call && request->name == L"show2d" && request->children.size() == 2,
            "Source Grapher show2d request must retain each plot wrapper.");
    Require(request->children[0]->name == L"ploteq2d" && request->children[0]->children.front()->name == L"=" &&
                request->children[1]->name == L"plotineq2d" && request->children[1]->children.front()->name == L"<=",
            "Source Equation wrappers must retain equations and inequality relations.");
    const auto graphRoundTrip = Read(Serialize(request, FormatType::MathML, LocalizationType::Unknown, L"mml"), FormatType::MathML);
    Require(graphRoundTrip->name == L"show2d" && graphRoundTrip->children.size() == 2, "Prefixed MathML graph wrappers must round trip.");
    const auto mixedRequest = Read(L"<math xmlns=\"http://www.w3.org/1998/Math/MathML\"><mrow><mi>show2d</mi><mfenced separators=\"\">"
                                   L"<mrow><mi>plotEq2d</mi><mfenced separators=\"\"><mrow><mi>y</mi><mo>=</mo>"
                                   L"<msup><mi>x</mi><mn>2</mn></msup></mrow></mfenced></mrow><mo>,</mo>"
                                   L"<mrow><mi>plotIneq2D</mi><mfenced separators=\"\"><mrow><mi>y</mi><mo>&lt;</mo>"
                                   L"<mn>1</mn></mrow></mfenced></mrow></mfenced></mrow></math>",
                                   FormatType::MathML);
    Require(mixedRequest->name == L"show2d" && mixedRequest->children.size() == 2 &&
                mixedRequest->children[1]->children.front()->name == L"<",
            "Source explicit mo separators must separate plot arguments without becoming expressions.");
    Require(Serialize(mixedRequest, FormatType::MathML, LocalizationType::Unknown, L"mml:") ==
                Serialize(mixedRequest, FormatType::MathML, LocalizationType::Unknown, L"mml"),
            "MathML namespace prefixes must accept an optional trailing colon.");
}

void CheckRejections()
{
    for (const auto *input : {L"", L"2+", L"2**3", L"1.2.3", L"1e+", L"(x", L"x)", L"2 3", L"sin()", L"root(2)", L"1<2<3"})
    {
        const auto parsed = Parse(input, FormatType::Linear, LocalizationType::Unknown);
        Require(!parsed.root && parsed.error_code > 0 && parsed.error_type == 1, "Malformed syntax must return a syntax diagnostic.");
    }
    const auto unsupported = Parse(L"AA==", FormatType::Base64, LocalizationType::Unknown);
    Require(!unsupported.root && unsupported.error_code == static_cast<int>(ParseErrorCode::Unsupported),
            "Unsupported binary encoding must be reported explicitly.");
    const auto domain = Parse(L"1e999", FormatType::Linear, LocalizationType::Unknown);
    Require(!domain.root && domain.error_code == static_cast<int>(ParseErrorCode::OutOfDomain) && domain.error_type == 0,
            "Unrepresentable numeric literals must return a domain diagnostic.");
    for (const auto *xml : {L"<!DOCTYPE math [<!ENTITY x SYSTEM 'file:///etc/passwd'>]><math><mi>&x;</mi></math>",
                            L"<math><mi>&unknown;</mi></math>", L"<math><mfrac><mn>1</mn></mfrac></math>", L"<math><mn>1</mi></math>",
                            L"<math><mn>2x</mn></math>", L"<math><annotation>text</annotation></math>"})
        Require(!Parse(xml, FormatType::MathML, LocalizationType::Unknown).root, "Invalid or unsupported MathML must be rejected locally.");
    const auto longInput = Parse(std::wstring(65537, L'x'), FormatType::Linear, LocalizationType::Unknown);
    Require(longInput.error_code == static_cast<int>(ParseErrorCode::ComplexityLimit), "Input length limit must be enforced.");
    const auto nested = Parse(std::wstring(129, L'(') + L"x" + std::wstring(129, L')'), FormatType::Linear, LocalizationType::Unknown);
    Require(nested.error_code == static_cast<int>(ParseErrorCode::ComplexityLimit), "Parser depth must be bounded.");
    bool serializationRejected = false;
    try
    {
        Serialize(Read(L"x"), FormatType::Binary, LocalizationType::Unknown);
    }
    catch (const std::invalid_argument &)
    {
        serializationRejected = true;
    }
    Require(serializationRejected, "Unsupported output encoding must not masquerade as text.");
}
} // namespace

int main()
{
    try
    {
        CheckGrammar();
        CheckFunctionsAndDomains();
        CheckFormats();
        CheckRejections();
        std::cout << "Expression tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
