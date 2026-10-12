#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <objbase.h>
#include <windows.h>
#endif
#include "GraphingInterfaces/IMathSolver.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using Factory = decltype(&Graphing::IMathSolver::CreateMathSolver);
const std::array<const char *, 10> RequiredCases = {"polynomial", "repeated_root", "rational_hole", "parameter",      "degrees",
                                                    "explicit_x", "implicit",      "inequality",    "multi_equation", "parse_error"};
struct Case
{
    std::vector<std::string> columns;
};
struct Record
{
    std::string type;
    std::string value;
};
using Records = std::map<std::pair<std::string, std::string>, Record>;
const std::array<const char *, 14> FormatNames = {"Formula",
                                                  "InvariantFormula",
                                                  "FormulaWithoutAggregate",
                                                  "Linear",
                                                  "LinearInput",
                                                  "MathML",
                                                  "MathMLNoWrapper",
                                                  "MathRichEdit",
                                                  "InlineMathRichEdit",
                                                  "Binary",
                                                  "InvariantBinary",
                                                  "Base64",
                                                  "InvariantBase64",
                                                  "Latex"};
std::vector<std::string> Split(const std::string &text, char separator)
{
    std::vector<std::string> parts;
    std::size_t begin = 0;
    for (;;)
    {
        const auto end = text.find(separator, begin);
        parts.push_back(text.substr(begin, end - begin));
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
    return parts;
}
std::wstring Decode(const std::string &input)
{
    std::wstring output;
    for (std::size_t i = 0; i < input.size();)
    {
        const auto first = static_cast<unsigned char>(input[i++]);
        std::uint32_t point = first;
        unsigned trailing = 0;
        if (first >= 0xc2 && first <= 0xdf)
        {
            point = first & 31;
            trailing = 1;
        }
        else if (first >= 0xe0 && first <= 0xef)
        {
            point = first & 15;
            trailing = 2;
        }
        else if (first >= 0xf0 && first <= 0xf4)
        {
            point = first & 7;
            trailing = 3;
        }
        else if (first >= 128)
            throw std::runtime_error("Invalid fixture UTF-8");
        for (unsigned j = 0; j < trailing; ++j)
        {
            if (i == input.size())
                throw std::runtime_error("Truncated fixture UTF-8");
            const auto byte = static_cast<unsigned char>(input[i++]);
            if ((byte & 0xc0) != 0x80)
                throw std::runtime_error("Invalid fixture UTF-8 continuation");
            point = point * 64 + (byte & 63);
        }
        if ((trailing == 1 && point < 0x80) || (trailing == 2 && point < 0x800) || (trailing == 3 && point < 0x10000) || point > 0x10ffff ||
            (point >= 0xd800 && point <= 0xdfff))
            throw std::runtime_error("Invalid fixture code point");
        if (sizeof(wchar_t) == 2 && point > 0xffff)
        {
            output.push_back(static_cast<wchar_t>(0xd800 + ((point - 0x10000) >> 10)));
            output.push_back(static_cast<wchar_t>(0xdc00 + ((point - 0x10000) & 1023)));
        }
        else
            output.push_back(static_cast<wchar_t>(point));
    }
    return output;
}
std::string Escape(const std::wstring &input)
{
    std::ostringstream output;
    output.imbue(std::locale::classic());
    const auto unicode = [&output](std::uint32_t point) {
        output << "\\u" << std::hex << std::setw(4) << std::setfill('0') << point << std::dec;
    };
    for (const wchar_t character : input)
    {
        const auto point = static_cast<std::uint32_t>(character);
        if (point == '\\')
            output << "\\\\";
        else if (point == '\t')
            output << "\\t";
        else if (point == '\n')
            output << "\\n";
        else if (point == '\r')
            output << "\\r";
        else if (point >= 32 && point < 127)
            output << static_cast<char>(point);
        else if (point > 0xffff)
        {
            unicode(0xd800 + ((point - 0x10000) >> 10));
            unicode(0xdc00 + ((point - 0x10000) & 1023));
        }
        else
            unicode(point);
    }
    return output.str();
}
std::string Numeric(double value)
{
    if (std::isnan(value))
        return "nan";
    if (std::isinf(value))
        return value < 0 ? "-inf" : "inf";
    if (value == 0)
        return "0";
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return output.str();
}
void Put(Records &records, const std::string &id, const std::string &key, const std::string &type, std::string value)
{
    if (!records.emplace(std::make_pair(id, key), Record{type, std::move(value)}).second)
        throw std::runtime_error("Duplicate snapshot field");
}
void Exact(Records &records, const std::string &id, const std::string &key, const std::wstring &value)
{
    Put(records, id, key, "exact", Escape(value));
}
void Exact(Records &records, const std::string &id, const std::string &key, const std::string &value)
{
    Put(records, id, key, "exact", Escape(Decode(value)));
}
void Integer(Records &records, const std::string &id, const std::string &key, std::int64_t value)
{
    Put(records, id, key, "exact", std::to_string(value));
}
void Boolean(Records &records, const std::string &id, const std::string &key, bool value)
{
    Put(records, id, key, "exact", value ? "true" : "false");
}
void Float(Records &records, const std::string &id, const std::string &key, double value)
{
    Put(records, id, key, "numeric", Numeric(value));
}
std::vector<Case> ReadCases(const std::filesystem::path &path, std::string &hash)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot read case file");
    std::vector<Case> cases;
    std::set<std::string> ids;
    std::uint64_t fingerprint = 14695981039346656037ULL;
    std::string line;
    while (std::getline(input, line))
    {
        if (line.size() > 65536)
            throw std::runtime_error("Case line exceeds safety bound");
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        auto columns = Split(line, '\t');
        if (columns.size() != 11 || columns[0].empty() || columns[0] == "meta" || !ids.insert(columns[0]).second)
            throw std::runtime_error("Case requires eleven columns and a unique ID");
        for (const char character : columns[0])
            if (!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_'))
                throw std::runtime_error("Case ID requires lowercase ASCII, digits, or underscores");
        for (const char character : line + '\n')
        {
            fingerprint ^= static_cast<unsigned char>(character);
            fingerprint *= 1099511628211ULL;
        }
        cases.push_back({std::move(columns)});
        if (cases.size() > 64)
            throw std::runtime_error("Case count exceeds safety bound");
    }
    for (const auto *required : RequiredCases)
        if (!ids.count(required))
            throw std::runtime_error(std::string("Missing required case: ") + required);
    std::ostringstream encoded;
    encoded << std::hex << std::setw(16) << std::setfill('0') << fingerprint;
    hash = encoded.str();
    return cases;
}
int ParseInteger(const std::string &text)
{
    std::size_t consumed;
    const int value = std::stoi(text, &consumed);
    if (consumed != text.size())
        throw std::runtime_error("Invalid integer option");
    return value;
}
bool ParseBoolean(const std::string &text)
{
    if (text != "0" && text != "1")
        throw std::runtime_error("Expected boolean 0 or 1");
    return text == "1";
}
Graphing::FormatType ParseFormat(const std::string &name)
{
    for (unsigned i = 0; i < FormatNames.size(); ++i)
        if (name == FormatNames[i])
            return static_cast<Graphing::FormatType>(i);
    throw std::runtime_error("Unknown parse format");
}
Graphing::LocalizationType ParseLocalization(const std::string &name)
{
    if (name == "PointComma")
        return Graphing::LocalizationType::DecimalPointAndListComma;
    if (name == "PointSemicolon")
        return Graphing::LocalizationType::DecimalPointAndListSemicolon;
    if (name == "CommaSemicolon")
        return Graphing::LocalizationType::DecimalCommaAndListSemicolon;
    throw std::runtime_error("Unknown localization");
}
Graphing::EvalTrigUnitMode ParseAngle(const std::string &name)
{
    if (name == "Radians")
        return Graphing::EvalTrigUnitMode::Radians;
    if (name == "Degrees")
        return Graphing::EvalTrigUnitMode::Degrees;
    if (name == "Grads")
        return Graphing::EvalTrigUnitMode::Grads;
    throw std::runtime_error("Unknown angle unit");
}
void AnalysisSnapshot(Records &records, const std::string &id, const Graphing::IGraphFunctionAnalysisData &data)
{
    Integer(records, id, "analysis.too_complex", data.TooComplexFeatures);
    Integer(records, id, "analysis.parity", data.Parity);
    Integer(records, id, "analysis.period_direction", data.PeriodicityDirection);
    const auto text = [&](const char *key, const std::wstring &value, unsigned bit) {
        Exact(records, id, std::string("analysis.") + key, value);
        Exact(records, id, std::string("analysis.") + key + ".status",
              (data.TooComplexFeatures & bit) ? "unknown"
              : value.empty()                 ? "empty_reported"
                                              : "reported");
    };
    text("domain", data.Domain, 1);
    text("range", data.Range, 2);
    text("period", data.PeriodicityExpression, 8);
    text("zeros", data.Zeros, 16);
    text("y_intercept", data.YIntercept, 32);
    const auto list = [&](const char *key, std::vector<std::wstring> values, unsigned bit) {
        std::sort(values.begin(), values.end());
        Integer(records, id, std::string("analysis.") + key + ".count", static_cast<std::int64_t>(values.size()));
        Exact(records, id, std::string("analysis.") + key + ".status", (data.TooComplexFeatures & bit) ? "unknown" : "reported");
        for (std::size_t i = 0; i < values.size(); ++i)
            Exact(records, id, std::string("analysis.") + key + "." + std::to_string(i), values[i]);
    };
    list("minima", data.Minima, 64);
    list("maxima", data.Maxima, 128);
    list("inflections", data.InflectionPoints, 256);
    list("vertical_asymptotes", data.VerticalAsymptotes, 512);
    list("horizontal_asymptotes", data.HorizontalAsymptotes, 1024);
    list("oblique_asymptotes", data.ObliqueAsymptotes, 2048);
    Integer(records, id, "analysis.monotonicity.count", static_cast<std::int64_t>(data.MonotoneIntervals.size()));
    Exact(records, id, "analysis.monotonicity.status", (data.TooComplexFeatures & 4096) ? "unknown" : "reported");
    std::size_t index = 0;
    for (const auto &interval : data.MonotoneIntervals)
    {
        Exact(records, id, "analysis.monotonicity." + std::to_string(index) + ".interval", interval.first);
        Integer(records, id, "analysis.monotonicity." + std::to_string(index++) + ".direction", interval.second);
    }
    Exact(records, id, "analysis.parity.status", (data.TooComplexFeatures & 4) ? "unknown" : "reported");
}
void RangesSnapshot(Records &records, const std::string &id, const std::string &phase, Graphing::Renderer::IGraphRenderer &renderer)
{
    double xmin = std::numeric_limits<double>::quiet_NaN(), xmax = xmin, ymin = xmin, ymax = xmin;
    const auto result = renderer.GetDisplayRanges(xmin, xmax, ymin, ymax);
    Integer(records, id, phase + ".hr", result);
    Float(records, id, phase + ".x_min", xmin);
    Float(records, id, phase + ".x_max", xmax);
    Float(records, id, phase + ".y_min", ymin);
    Float(records, id, phase + ".y_max", ymax);
}
void CaptureCase(Records &records, const Case &item, Factory factory)
{
    const auto &c = item.columns;
    const auto &id = c[0];
    bool complete = true;
    auto solver = factory();
    if (!solver)
        throw std::runtime_error("Factory returned null");
    for (unsigned i = 1; i < c.size(); ++i)
        Exact(records, id, "input." + std::to_string(i), c[i]);
    solver->ParsingOptions().SetFormatType(ParseFormat(c[1]));
    solver->ParsingOptions().SetLocalizationType(ParseLocalization(c[2]));
    solver->EvalOptions().SetTrigUnitMode(ParseAngle(c[3]));
    Integer(records, id, "eval.angle", static_cast<int>(solver->EvalOptions().GetTrigUnitMode()));
    int code = -987654, type = -987654;
    auto expression = solver->ParseInput(Decode(c[10]), code, type);
    Boolean(records, id, "parse.present", static_cast<bool>(expression));
    Integer(records, id, "parse.error_code", code);
    Integer(records, id, "parse.error_type", type);
    complete = static_cast<bool>(expression) == ParseBoolean(c[7]);
    int mapped_code = -987654, mapped_type = -987654;
    solver->HRErrorToErrorInfo(E_ABORT, mapped_code, mapped_type);
    Integer(records, id, "error.abort.code", mapped_code);
    Integer(records, id, "error.abort.type", mapped_type);
    if (!expression)
    {
        Exact(records, id, "graph.status", "not_applicable");
        complete = complete && !ParseBoolean(c[8]) && !ParseBoolean(c[9]);
        Exact(records, id, "case.status", complete ? "complete" : "unexpected_parse");
        return;
    }
    Boolean(records, id, "expression.empty_set", expression->IsEmptySet());
    // Invariant serializers expose allocation-dependent IDs. Their availability is captured without treating IDs as semantics.
    for (unsigned i = 0; i < FormatNames.size(); ++i)
    {
        solver->FormatOptions().SetFormatType(static_cast<Graphing::FormatType>(i));
        solver->FormatOptions().SetLocalizationType(ParseLocalization(c[2]));
        solver->FormatOptions().SetMathMLPrefix(L"");
        const auto serialized = solver->Serialize(expression.get());
        const std::string field = "serialize." + std::string(FormatNames[i]);
        Boolean(records, id, field + ".available", !serialized.empty());
        if (i != 1 && i != 10 && i != 12)
            Exact(records, id, field, serialized);
        else
            Exact(records, id, field + ".status", "identity_payload_excluded");
        if ((i == 0 || i == 3 || i == 5) && serialized.empty())
            complete = false;
    }
    auto graph = solver->CreateGrapher(expression.get());
    Boolean(records, id, "graph.present", static_cast<bool>(graph));
    if (!graph)
    {
        Exact(records, id, "case.status", "missing_graph");
        return;
    }
    auto equations = graph->TryInitialize();
    Boolean(records, id, "init.present", equations.has_value());
    Integer(records, id, "init.hr", graph->GetInitializationError());
    complete = complete && equations.has_value() == ParseBoolean(c[8]);
    if (!equations)
    {
        Exact(records, id, "case.status", complete ? "complete" : "unexpected_init");
        return;
    }
    Integer(records, id, "equations.count", static_cast<std::int64_t>(equations->size()));
    for (std::size_t i = 0; i < equations->size(); ++i)
    {
        if (!(*equations)[i])
        {
            complete = false;
            continue;
        }
        Boolean(records, id, "selection.initial." + std::to_string(i), (*equations)[i]->IsEquationSelected());
    }
    std::vector<std::wstring> variables;
    for (const auto &variable : graph->GetVariables())
        if (variable)
            variables.push_back(variable->GetVariableName());
        else
            complete = false;
    std::sort(variables.begin(), variables.end());
    Integer(records, id, "variables.count", static_cast<std::int64_t>(variables.size()));
    for (std::size_t i = 0; i < variables.size(); ++i)
        Exact(records, id, "variables." + std::to_string(i), variables[i]);
    graph->GetOptions().SetAllowKeyGraphFeaturesForFunctionsWithParameters(ParseBoolean(c[4]));
    graph->GetOptions().SetMaxExecutionTime(1000);
    Boolean(records, id, "options.allow_parameters", graph->GetOptions().GetAllowKeyGraphFeaturesForFunctionsWithParameters());
    Integer(records, id, "options.max_execution_time", static_cast<std::int64_t>(graph->GetOptions().GetMaxExecutionTime()));
    if (c[5] != "-")
        for (const auto &parameter : Split(c[5], ';'))
        {
            const auto parts = Split(parameter, '=');
            if (parts.size() != 2)
                throw std::runtime_error("Invalid parameter assignment");
            std::size_t consumed;
            const double value = std::stod(parts[1], &consumed);
            if (consumed != parts[1].size() || !std::isfinite(value))
                throw std::runtime_error("Invalid parameter value");
            graph->SetArgValue(Decode(parts[0]), value);
        }
    const int selected = ParseInteger(c[6]);
    if (selected >= 0)
    {
        if (static_cast<std::size_t>(selected) >= equations->size() || !(*equations)[static_cast<std::size_t>(selected)])
            complete = false;
        else
        {
            const bool selected_ok = (*equations)[static_cast<std::size_t>(selected)]->TrySelectEquation();
            Boolean(records, id, "selection.select_result", selected_ok);
            complete = complete && selected_ok;
        }
    }
    for (std::size_t i = 0; i < equations->size(); ++i)
        if ((*equations)[i])
            Boolean(records, id, "selection.after." + std::to_string(i), (*equations)[i]->IsEquationSelected());
    auto analyzer = graph->GetAnalyzer();
    Boolean(records, id, "analyzer.present", static_cast<bool>(analyzer));
    if (analyzer)
    {
        bool not_x = false;
        const bool can = analyzer->CanFunctionAnalysisBePerformed(not_x);
        Boolean(records, id, "analysis.can_perform", can);
        Boolean(records, id, "analysis.variable_not_x", not_x);
        complete = complete && can == ParseBoolean(c[9]);
        if (can)
        {
            const auto result = analyzer->PerformFunctionAnalysis(255);
            Integer(records, id, "analysis.hr", result);
            if (result >= 0)
                AnalysisSnapshot(records, id, solver->Analyze(analyzer.get()));
            else
            {
                Exact(records, id, "analysis.status", "failed");
                complete = false;
            }
        }
        else
            Exact(records, id, "analysis.status", "not_applicable");
    }
    else
        complete = false;
    auto renderer = graph->GetRenderer();
    Boolean(records, id, "renderer.present", static_cast<bool>(renderer));
    if (renderer)
    {
        {
            const auto result = renderer->SetGraphSize(400, 300);
            Integer(records, id, "viewport.size.hr", result);
            complete = complete && result >= 0;
        }
        {
            const auto result = renderer->SetDpi(96, 96);
            Integer(records, id, "viewport.dpi.hr", result);
            complete = complete && result >= 0;
        }
        RangesSnapshot(records, id, "viewport.initial", *renderer);
        {
            const auto result = renderer->SetDisplayRanges(-4, 4, -3, 3);
            Integer(records, id, "viewport.set_ranges.hr", result);
            complete = complete && result >= 0;
        }
        {
            const auto result = renderer->PrepareGraph();
            Integer(records, id, "viewport.prepare.hr", result);
            complete = complete && result >= 0;
        }
        RangesSnapshot(records, id, "viewport.set", *renderer);
        std::map<unsigned, std::string> equation_names;
        for (std::size_t i = 0; i < equations->size(); ++i)
            if ((*equations)[i])
                equation_names.emplace((*equations)[i]->GetGraphEquationID(), "equation_" + std::to_string(i));
        for (unsigned i = 0; i < 3; ++i)
        {
            const double query_x = i == 0 ? 200 : i == 1 ? 100 : 300;
            const double query_y = i == 0 ? 150 : i == 1 ? 250 : 50;
            int formula = -1;
            float sx = std::numeric_limits<float>::quiet_NaN(), sy = sx;
            double x = std::numeric_limits<double>::quiet_NaN(), y = x, rho = x, theta = x, t = x;
            const auto result = renderer->GetClosePointData(query_x, query_y, 0.001, formula, sx, sy, x, y, rho, theta, t);
            const auto field = "hit." + std::to_string(i);
            Integer(records, id, field + ".hr", result);
            const auto name = equation_names.find(static_cast<unsigned>(formula));
            Exact(records, id, field + ".equation", formula == -1 ? "none" : name == equation_names.end() ? "unmapped" : name->second);
            Float(records, id, field + ".screen_x", sx);
            Float(records, id, field + ".screen_y", sy);
            Float(records, id, field + ".x", x);
            Float(records, id, field + ".y", y);
            Float(records, id, field + ".rho", rho);
            Float(records, id, field + ".theta", theta);
            Float(records, id, field + ".t", t);
        }
        {
            const auto result = renderer->MoveRangeByRatio(0.25, -0.25);
            Integer(records, id, "viewport.move.hr", result);
            complete = complete && result >= 0;
        }
        RangesSnapshot(records, id, "viewport.moved", *renderer);
        {
            const auto result = renderer->ScaleRange(0.25, -0.5, 0.5);
            Integer(records, id, "viewport.scale.hr", result);
            complete = complete && result >= 0;
        }
        RangesSnapshot(records, id, "viewport.scaled", *renderer);
        {
            const auto result = renderer->ResetRange();
            Integer(records, id, "viewport.reset_operation.hr", result);
            complete = complete && result >= 0;
        }
        RangesSnapshot(records, id, "viewport.reset", *renderer);
    }
    else
        complete = false;
    Boolean(records, id, "selection.reset_result", graph->TryResetSelection());
    for (std::size_t i = 0; i < equations->size(); ++i)
        if ((*equations)[i])
            Boolean(records, id, "selection.reset." + std::to_string(i), (*equations)[i]->IsEquationSelected());
    Exact(records, id, "case.status", complete ? "complete" : "unexpected_or_missing");
}
void WriteSnapshot(const std::filesystem::path &path, const Records &records)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("Cannot write snapshot");
    for (const auto &row : records)
        output << row.first.first << '\t' << row.first.second << '\t' << row.second.type << '\t' << row.second.value << '\n';
    if (!output)
        throw std::runtime_error("Snapshot write failed");
}
Records ReadSnapshot(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot read snapshot");
    Records records;
    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.size() > 1048576 || records.size() > 30000)
            throw std::runtime_error("Snapshot exceeds safety bound");
        const auto columns = Split(line, '\t');
        if (columns.size() != 4)
            throw std::runtime_error("Malformed snapshot row");
        Put(records, columns[0], columns[1], columns[2], columns[3]);
    }
    const auto value = [&](const std::string &id, const std::string &key) -> const std::string & {
        const auto found = records.find({id, key});
        if (found == records.end())
            throw std::runtime_error("Missing required snapshot field: " + id + "." + key);
        return found->second.value;
    };
    if (value("meta", "schema") != "1" || value("meta", "complete") != "true")
        throw std::runtime_error("Incomplete or unsupported snapshot");
    const auto manifest = Split(value("meta", "cases"), ',');
    std::set<std::string> ids(manifest.begin(), manifest.end());
    if (ids.size() != manifest.size() || ids.size() != static_cast<std::size_t>(ParseInteger(value("meta", "case_count"))))
        throw std::runtime_error("Invalid case manifest");
    for (const auto *required : RequiredCases)
        if (!ids.count(required))
            throw std::runtime_error("Required compatibility case missing");
    for (const auto &id : ids)
    {
        if (value(id, "case.status") != "complete")
            throw std::runtime_error("Incomplete case: " + id);
        const auto first = records.lower_bound({id, ""});
        std::size_t count = 0;
        for (auto it = first; it != records.end() && it->first.first == id; ++it)
            ++count;
        if (count != static_cast<std::size_t>(ParseInteger(value(id, "case.record_count"))) + 1)
            throw std::runtime_error("Truncated case: " + id);
        value(id, "parse.present");
        value(id, "parse.error_code");
        value(id, "parse.error_type");
        for (unsigned i = 1; i <= 10; ++i)
            value(id, "input." + std::to_string(i));
    }
    for (const auto &row : records)
    {
        if (row.first.first != "meta" && !ids.count(row.first.first))
            throw std::runtime_error("Unlisted case in snapshot");
        if (row.second.type != "exact" && row.second.type != "numeric" && row.second.type != "identity")
            throw std::runtime_error("Unknown field type");
    }
    value("meta", "fixture_hash");
    value("meta", "architecture_bits");
    return records;
}
int Compare(const std::filesystem::path &left_path, const std::filesystem::path &right_path)
{
    const auto left = ReadSnapshot(left_path), right = ReadSnapshot(right_path);
    std::size_t differences = 0, compared = 0, unknown = 0;
    std::set<std::pair<std::string, std::string>> keys;
    for (const auto &row : left)
        keys.insert(row.first);
    for (const auto &row : right)
        keys.insert(row.first);
    for (const auto &key : keys)
    {
        const auto a = left.find(key), b = right.find(key);
        if ((a != left.end() && a->second.type == "identity") || (b != right.end() && b->second.type == "identity"))
        {
            if (key.first != "meta" || (key.second != "engine" && key.second != "runtime" && key.second != "wchar_bits"))
                throw std::runtime_error("Identity exemption used on a behavior field");
            continue;
        }
        ++compared;
        if (a != left.end() && (a->second.value == "unknown" || a->second.value == "empty_reported" ||
                                (a->second.type == "numeric" && a->second.value == "nan")))
            ++unknown;
        if (a == left.end() || b == right.end() || a->second.type != b->second.type || a->second.value != b->second.value)
        {
            ++differences;
            std::cout << "DIFFERENCE\t" << key.first << '\t' << key.second << '\n';
        }
    }
    std::cout << "SUMMARY\tfields=" << compared << "\tdifferences=" << differences << "\tunknown_or_unset=" << unknown << '\n';
    if (!differences)
        std::cout << "MATCH: recorded fields agree. Unknown fields and untested behavior remain outside this result.\n";
    return differences ? 1 : 0;
}
std::string Runtime()
{
#ifdef _MSC_VER
    return "MSVC_" + std::to_string(_MSC_VER) + "_iterator_" + std::to_string(_ITERATOR_DEBUG_LEVEL);
#elif defined(__clang__)
    return "Clang_" + std::to_string(__clang_major__);
#elif defined(__GNUC__)
    return "GCC_" + std::to_string(__GNUC__);
#else
    return "unknown";
#endif
}
int Run(const std::vector<std::string> &arguments)
{
    if (arguments.size() == 3 && arguments[0] == "--compare")
        return Compare(std::filesystem::u8path(arguments[1]), std::filesystem::u8path(arguments[2]));
    std::filesystem::path cases_path, output_path, engine_path;
    std::string export_name;
    for (std::size_t i = 0; i < arguments.size(); ++i)
    {
        if (i + 1 == arguments.size())
            throw std::runtime_error("Option requires a value");
        const auto option = arguments[i++];
        const auto &argument = arguments[i];
        if (option == "--cases")
            cases_path = std::filesystem::u8path(argument);
        else if (option == "--output")
            output_path = std::filesystem::u8path(argument);
        else if (option == "--engine")
            engine_path = std::filesystem::u8path(argument);
        else if (option == "--factory-export")
            export_name = argument;
        else
            throw std::runtime_error("Unknown option: " + option);
    }
    if (cases_path.empty() || output_path.empty())
        throw std::runtime_error("Use --cases CASES.tsv --output SNAPSHOT.tsv [--engine ABSOLUTE.dll --factory-export EXACT_EXPORT]");
    if (engine_path.empty() != export_name.empty())
        throw std::runtime_error("Reference loading requires both --engine and --factory-export");
    std::string fingerprint;
    const auto cases = ReadCases(cases_path, fingerprint);
    Factory factory = &Graphing::IMathSolver::CreateMathSolver;
#ifdef _WIN32
    struct Module
    {
        HMODULE handle = nullptr;
        ~Module()
        {
            if (handle)
                FreeLibrary(handle);
        }
    } module;
    struct Com
    {
        HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ~Com()
        {
            if (result >= 0)
                CoUninitialize();
        }
    } com;
    if (com.result < 0)
        throw std::runtime_error("COM apartment initialization failed");
    if (!engine_path.empty())
    {
        if (!engine_path.is_absolute() || !std::filesystem::is_regular_file(engine_path))
            throw std::runtime_error("--engine requires an existing absolute DLL path");
        module.handle = LoadLibraryExW(engine_path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module.handle)
            throw std::runtime_error("Constrained DLL load failed, Win32 error " + std::to_string(GetLastError()));
        std::array<wchar_t, 32768> actual{};
        const auto length = GetModuleFileNameW(module.handle, actual.data(), static_cast<DWORD>(actual.size()));
        if (length == 0 || length >= actual.size() || !std::filesystem::equivalent(engine_path, std::filesystem::path(actual.data())))
            throw std::runtime_error("Loaded module does not match requested DLL");
        const auto address = GetProcAddress(module.handle, export_name.c_str());
        if (!address)
            throw std::runtime_error("Exact factory export unavailable");
        // MSVC's C++ ABI is a prerequisite, not something this cast verifies.
        factory = reinterpret_cast<Factory>(address);
    }
#else
    if (!engine_path.empty())
        throw std::runtime_error("Reference DLL loading requires native Windows");
#endif
    Records records;
    Exact(records, "meta", "schema", "1");
    Exact(records, "meta", "fixture_hash", fingerprint);
    Integer(records, "meta", "case_count", static_cast<std::int64_t>(cases.size()));
    Integer(records, "meta", "architecture_bits", static_cast<std::int64_t>(sizeof(void *) * 8));
    Put(records, "meta", "runtime", "identity", Runtime());
    Put(records, "meta", "wchar_bits", "identity", std::to_string(sizeof(wchar_t) * 8));
    Put(records, "meta", "engine", "identity", engine_path.empty() ? "linked" : Escape(engine_path.wstring()));
    std::string manifest;
    bool complete = true;
    for (const auto &item : cases)
    {
        const auto &id = item.columns[0];
        if (!manifest.empty())
            manifest += ',';
        manifest += id;
        try
        {
            CaptureCase(records, item, factory);
        }
        catch (const std::exception &error)
        {
            complete = false;
            if (!records.count({id, "case.status"}))
                Exact(records, id, "case.status", "exception");
            Exact(records, id, "case.exception", error.what());
        }
        complete = complete && records.at({id, "case.status"}).value == "complete";
        std::size_t count = 0;
        for (auto it = records.lower_bound({id, ""}); it != records.end() && it->first.first == id; ++it)
            ++count;
        Integer(records, id, "case.record_count", static_cast<std::int64_t>(count));
    }
    Exact(records, "meta", "cases", manifest);
    Boolean(records, "meta", "complete", complete);
    WriteSnapshot(output_path, records);
    std::cout << "CAPTURE\tcases=" << cases.size() << "\tcomplete=" << (complete ? "true" : "false") << '\n';
    return complete ? 0 : 1;
}
} // namespace
int main(int argc, char *argv[])
{
    try
    {
        std::locale::global(std::locale::classic());
        return Run(std::vector<std::string>(argv + 1, argv + argc));
    }
    catch (const std::exception &error)
    {
        std::cerr << "Probe rejected: " << error.what() << '\n';
        return 2;
    }
}
