#include "Core.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace graphing::detail
{
namespace
{
std::atomic<unsigned> next_id{1};

unsigned AllocateId()
{
    const unsigned value = next_id.fetch_add(1, std::memory_order_relaxed);
    if (value == 0 || value >= static_cast<unsigned>(std::numeric_limits<int>::max()))
        throw std::overflow_error("Graph identifier space exhausted");
    return value;
}

class Expression final : public Graphing::IExpression
{
  public:
    explicit Expression(NodePtr root) : root(std::move(root)), id_(AllocateId())
    {
    }
    unsigned GetExpressionID() const override
    {
        return id_;
    }
    bool IsEmptySet() const override
    {
        return root->kind == NodeKind::List && root->children.empty();
    }
    const NodePtr root;

  private:
    unsigned id_;
};

class Variable final : public Graphing::IVariable
{
  public:
    Variable(std::wstring name, int id) : name_(std::move(name)), id_(id)
    {
    }
    int GetVariableID() const override
    {
        return id_;
    }
    const std::wstring &GetVariableName() override
    {
        return name_;
    }

  private:
    std::wstring name_;
    int id_;
};

class Equation final : public Graphing::IEquation
{
  public:
    Equation(std::shared_ptr<GraphState> state, const Plot &plot)
        : state_(std::move(state)), options_(plot.options), id_(plot.id), generation_(state_->initialization_generation)
    {
    }
    std::shared_ptr<Graphing::IEquationOptions> GetGraphEquationOptions() const override
    {
        return options_;
    }
    unsigned GetGraphEquationID() const override
    {
        return id_;
    }
    bool TrySelectEquation() override
    {
        if (generation_ != state_->initialization_generation)
            return false;
        const auto found = std::find_if(state_->plots.begin(), state_->plots.end(), [this](const Plot &plot) { return plot.id == id_; });
        if (found == state_->plots.end())
            return false;
        for (auto &plot : state_->plots)
            plot.selected = plot.id == id_;
        ++state_->revision;
        return true;
    }
    bool IsEquationSelected() const override
    {
        if (generation_ != state_->initialization_generation)
            return false;
        return std::any_of(state_->plots.begin(), state_->plots.end(),
                           [this](const Plot &plot) { return plot.id == id_ && plot.selected; });
    }

  private:
    std::shared_ptr<GraphState> state_;
    std::shared_ptr<Graphing::IEquationOptions> options_;
    unsigned id_;
    std::uint64_t generation_;
};

bool IsVariable(const NodePtr &node, const std::wstring &name)
{
    return node && node->kind == NodeKind::Variable && node->name == name;
}

bool ContainsVariable(const NodePtr &node, const std::wstring &name)
{
    return FindVariables(node).count(name) != 0;
}

bool IsRelation(const NodePtr &node)
{
    if (!node || node->kind != NodeKind::Binary || node->children.size() != 2)
        return false;
    const auto &name = node->name;
    return name == L"=" || name == L"<" || name == L"<=" || name == L">" || name == L">=" || name == L"!=";
}

bool IsParametricPair(const NodePtr &node)
{
    if (!node || node->kind != NodeKind::List || node->children.size() != 2)
        return false;
    const auto &x = node->children[0];
    const auto &y = node->children[1];
    return IsRelation(x) && IsRelation(y) && x->name == L"=" && y->name == L"=" && IsVariable(x->children[0], L"x") &&
           IsVariable(y->children[0], L"y") && !ContainsVariable(x->children[1], L"x") && !ContainsVariable(x->children[1], L"y") &&
           !ContainsVariable(y->children[1], L"x") && !ContainsVariable(y->children[1], L"y");
}

void CollectPlots(const NodePtr &node, std::vector<Plot> &plots)
{
    if (!node || plots.size() >= 64)
        throw std::invalid_argument("Too many equations");
    if (IsParametricPair(node))
    {
        Plot parametric;
        parametric.kind = PlotKind::Parametric;
        parametric.first = node->children[0]->children[1];
        parametric.second = node->children[1]->children[1];
        parametric.relation = L"=";
        plots.push_back(std::move(parametric));
        return;
    }
    if (node->kind == NodeKind::List ||
        (node->kind == NodeKind::Call && (node->name == L"show2d" || node->name == L"msgraph" || node->name == L"plot2d" ||
                                          node->name == L"ploteq2d" || node->name == L"plotineq2d")))
    {
        if (node->children.empty())
            throw std::invalid_argument("Empty graph request");
        for (const auto &child : node->children)
            CollectPlots(child, plots);
        return;
    }
    Plot plot;
    if (IsRelation(node))
    {
        const auto &lhs = node->children[0];
        const auto &rhs = node->children[1];
        if (IsRelation(lhs) || IsRelation(rhs) || node->name == L"!=")
            throw std::invalid_argument("Unsupported graph relation");
        plot.first = lhs;
        plot.second = rhs;
        plot.relation = node->name;
        if (node->name != L"=")
            plot.kind = PlotKind::Inequality;
        else if (IsVariable(lhs, L"y") && !ContainsVariable(rhs, L"y"))
        {
            plot.kind = PlotKind::ExplicitY;
            plot.first = rhs;
            plot.second.reset();
        }
        else if (IsVariable(rhs, L"y") && !ContainsVariable(lhs, L"y"))
        {
            plot.kind = PlotKind::ExplicitY;
            plot.second.reset();
        }
        else if (IsVariable(lhs, L"x") && !ContainsVariable(rhs, L"x"))
        {
            plot.kind = PlotKind::ExplicitX;
            plot.first = rhs;
            plot.second.reset();
        }
        else if (IsVariable(rhs, L"x") && !ContainsVariable(lhs, L"x"))
        {
            plot.kind = PlotKind::ExplicitX;
            plot.second.reset();
        }
        else if ((IsVariable(lhs, L"r") || IsVariable(lhs, L"rho") || IsVariable(lhs, L"ρ")) && !ContainsVariable(rhs, lhs->name))
        {
            plot.kind = PlotKind::Polar;
            plot.first = rhs;
            plot.second.reset();
        }
        else
            plot.kind = PlotKind::Implicit;
    }
    else
    {
        const auto variables = FindVariables(node);
        if (variables.count(L"x") && variables.count(L"y"))
            throw std::invalid_argument("Graph needs a relation");
        if (variables.count(L"z"))
            throw std::invalid_argument("3D graphs are unsupported");
        plot.kind = variables.count(L"y") ? PlotKind::ExplicitX : PlotKind::ExplicitY;
        plot.first = node;
    }
    if (ContainsVariable(plot.first, L"z") || ContainsVariable(plot.second, L"z"))
        throw std::invalid_argument("3D graphs are unsupported");
    plots.push_back(std::move(plot));
}

class Graph final : public Graphing::IGraph
{
  public:
    Graph(NodePtr expression, Graphing::EvalTrigUnitMode unit) : state_(std::make_shared<GraphState>()), expression_(std::move(expression))
    {
        state_->context.trig_unit = unit;
        state_->options = CreateGraphOptions(state_);
        renderer_ = CreateRenderer(state_);
        analyzer_ = CreateAnalyzer(state_);
    }
    std::optional<std::vector<std::shared_ptr<Graphing::IEquation>>> TryInitialize(const Graphing::IExpression *expression) override
    {
        NodePtr root = expression_;
        if (expression)
        {
            const auto *native = dynamic_cast<const Expression *>(expression);
            if (!native)
            {
                state_->initialization_error = E_INVALIDARG;
                return std::nullopt;
            }
            root = native->root;
        }
        if (!root)
        {
            state_->initialization_error = E_INVALIDARG;
            return std::nullopt;
        }
        try
        {
            std::vector<Plot> plots;
            CollectPlots(root, plots);
            if (plots.empty())
            {
                state_->initialization_error = E_INVALIDARG;
                return std::nullopt;
            }
            const auto colors = state_->options->GetGraphColors();
            for (std::size_t index = 0; index < plots.size(); ++index)
            {
                plots[index].id = static_cast<unsigned>(index);
                plots[index].options = CreateEquationOptions(colors[index % colors.size()]);
            }
            std::vector<std::shared_ptr<Graphing::IVariable>> variables;
            std::map<std::wstring, double> values;
            for (const auto &name : FindVariables(root))
            {
                const auto found = state_->context.variables.find(name);
                values.emplace(name, found == state_->context.variables.end() ? 1 : found->second);
                variables.push_back(std::make_shared<Variable>(name, static_cast<int>(AllocateId())));
            }
            if (state_->initialization_generation == std::numeric_limits<std::uint64_t>::max())
                throw std::overflow_error("Graph generation space exhausted");
            ++state_->initialization_generation;
            state_->plots = std::move(plots);
            state_->context.variables = std::move(values);
            state_->initialization_error = S_OK;
            variables_ = std::move(variables);
            expression_ = std::move(root);
            ++state_->revision;
            std::vector<std::shared_ptr<Graphing::IEquation>> result;
            for (const auto &plot : state_->plots)
                result.push_back(std::make_shared<Equation>(state_, plot));
            renderer_->ResetRange();
            return result;
        }
        catch (const std::invalid_argument &)
        {
            state_->initialization_error = E_NOTIMPL;
            return std::nullopt;
        }
        catch (const std::overflow_error &)
        {
            state_->initialization_error = E_FAIL;
            return std::nullopt;
        }
    }
    HRESULT GetInitializationError() const override
    {
        return state_->initialization_error;
    }
    Graphing::IGraphingOptions &GetOptions() override
    {
        return *state_->options;
    }
    std::vector<std::shared_ptr<Graphing::IVariable>> GetVariables() override
    {
        return variables_;
    }
    void SetArgValue(std::wstring name, double value) override
    {
        if (!std::isfinite(value))
            return;
        const auto found = state_->context.variables.find(name);
        if (found == state_->context.variables.end() || found->second == value)
            return;
        found->second = value;
        ++state_->revision;
    }
    std::shared_ptr<Graphing::Renderer::IGraphRenderer> GetRenderer() const override
    {
        return renderer_;
    }
    bool TryResetSelection() override
    {
        for (auto &plot : state_->plots)
            plot.selected = false;
        ++state_->revision;
        return true;
    }
    std::shared_ptr<Graphing::Analyzer::IGraphAnalyzer> GetAnalyzer() const override
    {
        return analyzer_;
    }
    std::shared_ptr<GraphState> GetState() const
    {
        return state_;
    }

  private:
    std::shared_ptr<GraphState> state_;
    NodePtr expression_;
    std::vector<std::shared_ptr<Graphing::IVariable>> variables_;
    std::shared_ptr<Graphing::Renderer::IGraphRenderer> renderer_;
    std::shared_ptr<Graphing::Analyzer::IGraphAnalyzer> analyzer_;
};

class ParsingOptions final : public Graphing::IParsingOptions
{
  public:
    void SetFormatType(Graphing::FormatType value) override
    {
        format = value;
    }
    void SetLocalizationType(Graphing::LocalizationType value) override
    {
        localization = value;
    }
    Graphing::FormatType format = Graphing::FormatType::Linear;
    Graphing::LocalizationType localization = Graphing::LocalizationType::DecimalPointAndListComma;
};

class EvalOptions final : public Graphing::IEvalOptions
{
  public:
    Graphing::EvalTrigUnitMode GetTrigUnitMode() const override
    {
        return unit_;
    }
    void SetTrigUnitMode(Graphing::EvalTrigUnitMode value) override
    {
        if (value != Graphing::EvalTrigUnitMode::Radians && value != Graphing::EvalTrigUnitMode::Degrees &&
            value != Graphing::EvalTrigUnitMode::Grads)
            return;
        unit_ = value;
        states.erase(std::remove_if(states.begin(), states.end(),
                                    [value](const std::weak_ptr<GraphState> &weak) {
                                        auto state = weak.lock();
                                        if (!state)
                                            return true;
                                        state->context.trig_unit = value;
                                        ++state->revision;
                                        return false;
                                    }),
                     states.end());
    }
    std::vector<std::weak_ptr<GraphState>> states;

  private:
    Graphing::EvalTrigUnitMode unit_ = Graphing::EvalTrigUnitMode::Radians;
};

class FormatOptions final : public Graphing::IFormatOptions
{
  public:
    void SetFormatType(Graphing::FormatType value) override
    {
        format = value;
    }
    void SetMathMLPrefix(const std::wstring &value) override
    {
        const auto is_letter = [](wchar_t character) {
            return (character >= L'a' && character <= L'z') || (character >= L'A' && character <= L'Z') || character == L'_';
        };
        if (value.size() > 64)
            return;
        std::wstring candidate = value;
        if (!candidate.empty() && candidate.back() == L':')
            candidate.pop_back();
        if (!candidate.empty() && (!is_letter(candidate.front()) || !std::all_of(candidate.begin(), candidate.end(), [&](wchar_t c) {
                return is_letter(c) || (c >= L'0' && c <= L'9') || c == L'-';
            })))
            return;
        prefix = std::move(candidate);
    }
    void SetLocalizationType(Graphing::LocalizationType value) override
    {
        localization = value;
    }
    Graphing::FormatType format = Graphing::FormatType::Linear;
    Graphing::LocalizationType localization = Graphing::LocalizationType::DecimalPointAndListComma;
    std::wstring prefix;
};

class MathSolver final : public Graphing::IMathSolver
{
  public:
    Graphing::IParsingOptions &ParsingOptions() override
    {
        return parsing_;
    }
    Graphing::IEvalOptions &EvalOptions() override
    {
        return eval_;
    }
    Graphing::IFormatOptions &FormatOptions() override
    {
        return format_;
    }
    std::unique_ptr<Graphing::IExpression> ParseInput(const std::wstring &input, int &code, int &type) override
    {
        code = 0;
        type = 0;
        const auto result = Parse(input, parsing_.format, parsing_.localization);
        code = result.error_code;
        type = result.error_type;
        if (!result.root)
            return nullptr;
        return std::make_unique<Expression>(result.root);
    }
    void HRErrorToErrorInfo(HRESULT hr, int &code, int &type) override
    {
        code = 0;
        type = 0;
        if (hr >= 0)
            return;
        if (hr == E_INVALIDARG || hr == E_POINTER)
        {
            code = 6;
            type = 1;
        }
        else if (hr == E_ABORT)
        {
            code = 4;
            type = 2;
        }
        else if (hr == E_NOTIMPL)
            code = -503;
        else
            code = -501;
    }
    std::shared_ptr<Graphing::IGraph> CreateGrapher(const Graphing::IExpression *expression) override
    {
        const auto *native = dynamic_cast<const Expression *>(expression);
        if (expression && !native)
            return nullptr;
        auto graph = std::make_shared<Graph>(native ? native->root : NodePtr{}, eval_.GetTrigUnitMode());
        eval_.states.push_back(graph->GetState());
        return graph;
    }
    std::shared_ptr<Graphing::IGraph> CreateGrapher() override
    {
        return CreateGrapher(nullptr);
    }
    std::wstring Serialize(const Graphing::IExpression *expression) override
    {
        const auto *native = dynamic_cast<const Expression *>(expression);
        if (!native)
            return {};
        try
        {
            return graphing::detail::Serialize(native->root, format_.format, format_.localization, format_.prefix);
        }
        catch (const std::invalid_argument &)
        {
            return {};
        }
    }
    Graphing::IGraphFunctionAnalysisData Analyze(const Graphing::Analyzer::IGraphAnalyzer *analyzer) override
    {
        return ReadAnalysis(analyzer);
    }

  private:
    graphing::detail::ParsingOptions parsing_;
    graphing::detail::EvalOptions eval_;
    graphing::detail::FormatOptions format_;
};
} // namespace
std::unique_ptr<Graphing::IMathSolver> CreateSolver()
{
    return std::make_unique<MathSolver>();
}
} // namespace graphing::detail

std::unique_ptr<Graphing::IMathSolver> Graphing::IMathSolver::CreateMathSolver()
{
    return graphing::detail::CreateSolver();
}
