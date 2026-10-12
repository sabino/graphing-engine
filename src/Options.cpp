#include "Core.h"
#include <cmath>

namespace graphing::detail
{
namespace
{
class EquationOptions final : public Graphing::IEquationOptions
{
  public:
    Graphing::Color GetGraphColor() const override
    {
        return graphcolor_;
    }
    void SetGraphColor(const Graphing::Color &value) override
    {
        graphcolor_ = value;
    }
    void ResetGraphColor() override
    {
        graphcolor_ = Graphing::Color{0, 120, 212};
    }

    Graphing::Renderer::LineStyle GetLineStyle() const override
    {
        return linestyle_;
    }
    void SetLineStyle(Graphing::Renderer::LineStyle value) override
    {
        if (!(value >= Graphing::Renderer::LineStyle::Solid && value <= Graphing::Renderer::LineStyle::DashDotDot))
            return;
        linestyle_ = value;
    }
    void ResetLineStyle() override
    {
        linestyle_ = Graphing::Renderer::LineStyle::Solid;
    }

    float GetLineWidth() const override
    {
        return linewidth_;
    }
    void SetLineWidth(float value) override
    {
        if (!(std::isfinite(value) && value > 0 && value <= 100))
            return;
        linewidth_ = value;
    }
    void ResetLineWidth() override
    {
        linewidth_ = 2;
    }

    float GetSelectedEquationLineWidth() const override
    {
        return selectedequationlinewidth_;
    }
    void SetSelectedEquationLineWidth(float value) override
    {
        if (!(std::isfinite(value) && value > 0 && value <= 100))
            return;
        selectedequationlinewidth_ = value;
    }
    void ResetSelectedEquationLineWidth() override
    {
        selectedequationlinewidth_ = 3;
    }

    float GetPointRadius() const override
    {
        return pointradius_;
    }
    void SetPointRadius(float value) override
    {
        if (!(std::isfinite(value) && value > 0 && value <= 100))
            return;
        pointradius_ = value;
    }
    void ResetPointRadius() override
    {
        pointradius_ = 3;
    }

    float GetSelectedEquationPointRadius() const override
    {
        return selectedequationpointradius_;
    }
    void SetSelectedEquationPointRadius(float value) override
    {
        if (!(std::isfinite(value) && value > 0 && value <= 100))
            return;
        selectedequationpointradius_ = value;
    }
    void ResetSelectedEquationPointRadius() override
    {
        selectedequationpointradius_ = 4;
    }

  private:
    Graphing::Color graphcolor_ = Graphing::Color{0, 120, 212};
    Graphing::Renderer::LineStyle linestyle_ = Graphing::Renderer::LineStyle::Solid;
    float linewidth_ = 2;
    float selectedequationlinewidth_ = 3;
    float pointradius_ = 3;
    float selectedequationpointradius_ = 4;
};

class GraphOptions final : public Graphing::IGraphingOptions
{
  public:
    explicit GraphOptions(std::weak_ptr<GraphState> state) : state_(std::move(state))
    {
    }
    void ResetMarkKeyGraphFeaturesData() override
    {
        markzeros_ = false;
        markyintercept_ = false;
        markminima_ = false;
        markmaxima_ = false;
        markinflectionpoints_ = false;
        markverticalasymptotes_ = false;
        markhorizontalasymptotes_ = false;
        markobliqueasymptotes_ = false;
    }

    bool GetMarkZeros() const override
    {
        return markzeros_;
    }
    void SetMarkZeros(bool value) override
    {
        markzeros_ = value;
    }

    bool GetMarkYIntercept() const override
    {
        return markyintercept_;
    }
    void SetMarkYIntercept(bool value) override
    {
        markyintercept_ = value;
    }

    bool GetMarkMinima() const override
    {
        return markminima_;
    }
    void SetMarkMinima(bool value) override
    {
        markminima_ = value;
    }

    bool GetMarkMaxima() const override
    {
        return markmaxima_;
    }
    void SetMarkMaxima(bool value) override
    {
        markmaxima_ = value;
    }

    bool GetMarkInflectionPoints() const override
    {
        return markinflectionpoints_;
    }
    void SetMarkInflectionPoints(bool value) override
    {
        markinflectionpoints_ = value;
    }

    bool GetMarkVerticalAsymptotes() const override
    {
        return markverticalasymptotes_;
    }
    void SetMarkVerticalAsymptotes(bool value) override
    {
        markverticalasymptotes_ = value;
    }

    bool GetMarkHorizontalAsymptotes() const override
    {
        return markhorizontalasymptotes_;
    }
    void SetMarkHorizontalAsymptotes(bool value) override
    {
        markhorizontalasymptotes_ = value;
    }

    bool GetMarkObliqueAsymptotes() const override
    {
        return markobliqueasymptotes_;
    }
    void SetMarkObliqueAsymptotes(bool value) override
    {
        markobliqueasymptotes_ = value;
    }

    unsigned long long GetMaxExecutionTime() const override
    {
        return maxexecutiontime_;
    }
    void SetMaxExecutionTime(unsigned long long value) override
    {
        maxexecutiontime_ = value;
    }
    void ResetMaxExecutionTime() override
    {
        maxexecutiontime_ = 0;
    }

    Graphing::Color GetBackColor() const override
    {
        return backcolor_;
    }
    void SetBackColor(const Graphing::Color &value) override
    {
        backcolor_ = value;
    }
    void ResetBackColor() override
    {
        backcolor_ = Graphing::Color{255, 255, 255};
    }

    Graphing::Color GetZerosColor() const override
    {
        return zeroscolor_;
    }
    void SetZerosColor(const Graphing::Color &value) override
    {
        zeroscolor_ = value;
    }
    void ResetZerosColor() override
    {
        zeroscolor_ = Graphing::Color{0, 120, 212};
    }

    Graphing::Color GetExtremaColor() const override
    {
        return extremacolor_;
    }
    void SetExtremaColor(const Graphing::Color &value) override
    {
        extremacolor_ = value;
    }
    void ResetExtremaColor() override
    {
        extremacolor_ = Graphing::Color{190, 45, 65};
    }

    Graphing::Color GetInflectionPointsColor() const override
    {
        return inflectionpointscolor_;
    }
    void SetInflectionPointsColor(const Graphing::Color &value) override
    {
        inflectionpointscolor_ = value;
    }
    void ResetInflectionPointsColor() override
    {
        inflectionpointscolor_ = Graphing::Color{130, 70, 170};
    }

    Graphing::Color GetAsymptotesColor() const override
    {
        return asymptotescolor_;
    }
    void SetAsymptotesColor(const Graphing::Color &value) override
    {
        asymptotescolor_ = value;
    }
    void ResetAsymptotesColor() override
    {
        asymptotescolor_ = Graphing::Color{130, 130, 130};
    }

    Graphing::Color GetAxisColor() const override
    {
        return axiscolor_;
    }
    void SetAxisColor(const Graphing::Color &value) override
    {
        axiscolor_ = value;
    }
    void ResetAxisColor() override
    {
        axiscolor_ = Graphing::Color{70, 70, 70};
    }

    Graphing::Color GetBoxColor() const override
    {
        return boxcolor_;
    }
    void SetBoxColor(const Graphing::Color &value) override
    {
        boxcolor_ = value;
    }
    void ResetBoxColor() override
    {
        boxcolor_ = Graphing::Color{130, 130, 130};
    }

    Graphing::Color GetGridColor() const override
    {
        return gridcolor_;
    }
    void SetGridColor(const Graphing::Color &value) override
    {
        gridcolor_ = value;
    }
    void ResetGridColor() override
    {
        gridcolor_ = Graphing::Color{228, 228, 228};
    }

    Graphing::Color GetFontColor() const override
    {
        return fontcolor_;
    }
    void SetFontColor(const Graphing::Color &value) override
    {
        fontcolor_ = value;
    }
    void ResetFontColor() override
    {
        fontcolor_ = Graphing::Color{40, 40, 40};
    }

    bool GetShowAxis() const override
    {
        return showaxis_;
    }
    void SetShowAxis(bool value) override
    {
        showaxis_ = value;
    }
    void ResetShowAxis() override
    {
        showaxis_ = true;
    }

    bool GetShowGrid() const override
    {
        return showgrid_;
    }
    void SetShowGrid(bool value) override
    {
        showgrid_ = value;
    }
    void ResetShowGrid() override
    {
        showgrid_ = true;
    }

    bool GetShowBox() const override
    {
        return showbox_;
    }
    void SetShowBox(bool value) override
    {
        showbox_ = value;
    }
    void ResetShowBox() override
    {
        showbox_ = true;
    }

    bool GetForceProportional() const override
    {
        return forceproportional_;
    }
    void SetForceProportional(bool value) override
    {
        forceproportional_ = value;
    }
    void ResetForceProportional() override
    {
        forceproportional_ = false;
    }

    bool GetAllowKeyGraphFeaturesForFunctionsWithParameters() const override
    {
        return allowkeygraphfeaturesforfunctionswithparameters_;
    }
    void SetAllowKeyGraphFeaturesForFunctionsWithParameters(bool value) override
    {
        allowkeygraphfeaturesforfunctionswithparameters_ = value;
    }
    void ResetAllowKeyGraphFeaturesForFunctionsWithParameters() override
    {
        allowkeygraphfeaturesforfunctionswithparameters_ = false;
    }

    std::wstring GetAliasX() const override
    {
        return aliasx_;
    }
    void SetAliasX(const std::wstring &value) override
    {
        if (!(value.size() <= 64))
            return;
        aliasx_ = value;
    }
    void ResetAliasX() override
    {
        aliasx_ = L"x";
    }

    std::wstring GetAliasY() const override
    {
        return aliasy_;
    }
    void SetAliasY(const std::wstring &value) override
    {
        if (!(value.size() <= 64))
            return;
        aliasy_ = value;
    }
    void ResetAliasY() override
    {
        aliasy_ = L"y";
    }

    Graphing::Renderer::LineStyle GetLineStyle() const override
    {
        return linestyle_;
    }
    void SetLineStyle(Graphing::Renderer::LineStyle value) override
    {
        if (!(value >= Graphing::Renderer::LineStyle::Solid && value <= Graphing::Renderer::LineStyle::DashDotDot))
            return;
        linestyle_ = value;
    }
    void ResetLineStyle() override
    {
        linestyle_ = Graphing::Renderer::LineStyle::Solid;
    }

    std::vector<Graphing::Color> GetGraphColors() const override
    {
        return palette_;
    }
    bool SetGraphColors(const std::vector<Graphing::Color> &colors) override
    {
        if (colors.empty() || colors.size() > 64)
            return false;
        palette_ = colors;
        ApplyPalette();
        return true;
    }
    void ResetGraphColors() override
    {
        palette_ = DefaultPalette();
        ApplyPalette();
    }
    std::pair<double, double> GetDefaultXRange() const override
    {
        return xrange_;
    }
    bool SetDefaultXRange(const std::pair<double, double> &range) override
    {
        if (!IsValidRange(range.first, range.second))
            return false;
        xrange_ = range;
        return true;
    }
    void ResetDefaultXRange() override
    {
        xrange_ = {-10, 10};
    }
    std::pair<double, double> GetDefaultYRange() const override
    {
        return yrange_;
    }
    bool SetDefaultYRange(const std::pair<double, double> &range) override
    {
        if (!IsValidRange(range.first, range.second))
            return false;
        yrange_ = range;
        return true;
    }
    void ResetDefaultYRange() override
    {
        yrange_ = {-10, 10};
    }

  private:
    void ApplyPalette()
    {
        if (auto state = state_.lock())
        {
            for (std::size_t index = 0; index < state->plots.size(); ++index)
                state->plots[index].options->SetGraphColor(palette_[index % palette_.size()]);
            ++state->revision;
        }
    }
    std::weak_ptr<GraphState> state_;
    static std::vector<Graphing::Color> DefaultPalette()
    {
        return {{0, 120, 212}, {210, 50, 65}, {35, 150, 75}, {140, 65, 180}, {230, 140, 30}};
    }
    std::vector<Graphing::Color> palette_ = DefaultPalette();
    std::pair<double, double> xrange_{-10, 10};
    std::pair<double, double> yrange_{-10, 10};
    bool markzeros_ = false;
    bool markyintercept_ = false;
    bool markminima_ = false;
    bool markmaxima_ = false;
    bool markinflectionpoints_ = false;
    bool markverticalasymptotes_ = false;
    bool markhorizontalasymptotes_ = false;
    bool markobliqueasymptotes_ = false;
    unsigned long long maxexecutiontime_ = 0;
    Graphing::Color backcolor_ = Graphing::Color{255, 255, 255};
    Graphing::Color zeroscolor_ = Graphing::Color{0, 120, 212};
    Graphing::Color extremacolor_ = Graphing::Color{190, 45, 65};
    Graphing::Color inflectionpointscolor_ = Graphing::Color{130, 70, 170};
    Graphing::Color asymptotescolor_ = Graphing::Color{130, 130, 130};
    Graphing::Color axiscolor_ = Graphing::Color{70, 70, 70};
    Graphing::Color boxcolor_ = Graphing::Color{130, 130, 130};
    Graphing::Color gridcolor_ = Graphing::Color{228, 228, 228};
    Graphing::Color fontcolor_ = Graphing::Color{40, 40, 40};
    bool showaxis_ = true;
    bool showgrid_ = true;
    bool showbox_ = true;
    bool forceproportional_ = false;
    bool allowkeygraphfeaturesforfunctionswithparameters_ = false;
    std::wstring aliasx_ = L"x";
    std::wstring aliasy_ = L"y";
    Graphing::Renderer::LineStyle linestyle_ = Graphing::Renderer::LineStyle::Solid;
};
} // namespace

std::shared_ptr<Graphing::IEquationOptions> CreateEquationOptions(Graphing::Color color)
{
    auto options = std::make_shared<EquationOptions>();
    options->SetGraphColor(color);
    return options;
}

std::shared_ptr<Graphing::IGraphingOptions> CreateGraphOptions(std::weak_ptr<GraphState> state)
{
    return std::make_shared<GraphOptions>(std::move(state));
}
} // namespace graphing::detail
