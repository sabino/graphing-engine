#pragma once

#include "GraphingInterfaces/GraphingEnums.h"
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace graphing::detail
{
// Codes follow Calculator GraphControl/Models/Equation.h where applicable.
// error_type uses its Evaluation=0, Syntax=1, Abort=2 convention.
enum class ParseErrorCode : int
{
    Success = 0,
    ParenthesisMismatch = 1,
    InvalidNumber = 3,
    ComplexityLimit = 4,
    UnexpectedEnd = 5,
    UnexpectedToken = 6,
    InvalidToken = 7,
    TooManyComparisons = 8,
    EmptyExpression = 12,
    ExpectedFunctionParenthesis = 25,
    IncorrectArguments = 26,
    InvalidMathML = 40,
    UnknownMathMLEntity = 41,
    UnknownMathMLElement = 42,
    OutOfDomain = -101,
    Unsupported = -503
};

// Inputs are limited to 65,536 characters, 8,192 nodes, and depth 128.
// Parsed nodes are immutable. Undefined real evaluations return quiet NaN.
// Identifiers start with ASCII or Greek letters; following digits/underscores are allowed.
// Lexing and numeric parsing are independent of the process locale.
enum class NodeKind
{
    Number,
    Variable,
    Unary,
    Binary,
    Call,
    List
};
struct Node;
using NodePtr = std::shared_ptr<const Node>;
struct Node
{
    NodeKind kind = NodeKind::Number;
    double number = 0;
    std::wstring name;
    std::vector<NodePtr> children;
};

struct ParseResult
{
    NodePtr root;
    int error_code = 0;
    int error_type = 0;
    std::wstring message;
};

struct EvalContext
{
    std::map<std::wstring, double> variables;
    Graphing::EvalTrigUnitMode trig_unit = Graphing::EvalTrigUnitMode::Radians;
};

ParseResult Parse(const std::wstring &input, Graphing::FormatType format, Graphing::LocalizationType localization);
double Evaluate(const NodePtr &node, const EvalContext &context);
std::set<std::wstring> FindVariables(const NodePtr &node);
// Unsupported output formats or invalid prefixes throw std::invalid_argument.
// InvariantFormula IDs follow sorted variable names. It is serialization-only.
std::wstring Serialize(const NodePtr &node, Graphing::FormatType format, Graphing::LocalizationType localization,
                       const std::wstring &mathml_prefix = L"");
} // namespace graphing::detail
