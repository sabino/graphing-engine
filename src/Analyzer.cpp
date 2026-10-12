#include "Core.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace graphing::detail
{
namespace
{
// Coefficients are exact rationals. Overflow ends analysis rather than changing its claims.
struct Unsupported : std::exception
{
};
struct TimedOut : std::exception
{
};
struct Budget
{
    std::chrono::steady_clock::time_point end;
    unsigned work = 0;
    void Check()
    {
        if (++work > 200000 || std::chrono::steady_clock::now() >= end)
            throw TimedOut{};
    }
};
using Integer = std::int64_t;
Integer AddInteger(Integer a, Integer b)
{
    if ((b > 0 && a > std::numeric_limits<Integer>::max() - b) || (b < 0 && a < -std::numeric_limits<Integer>::max() - b))
        throw Unsupported{};
    return a + b;
}
Integer MultiplyInteger(Integer a, Integer b)
{
    if (a == 0 || b == 0)
        return 0;
    if (std::abs(a) > std::numeric_limits<Integer>::max() / std::abs(b))
        throw Unsupported{};
    return a * b;
}
struct Rational
{
    Integer n = 0;
    Integer d = 1;
    Rational() = default;
    Rational(Integer numerator, Integer denominator = 1)
    {
        if (denominator == 0 || numerator == std::numeric_limits<Integer>::min() || denominator == std::numeric_limits<Integer>::min())
            throw Unsupported{};
        if (denominator < 0)
        {
            numerator = -numerator;
            denominator = -denominator;
        }
        const Integer divisor = std::gcd(numerator, denominator);
        n = numerator / divisor;
        d = denominator / divisor;
    }
};
Rational operator+(Rational a, Rational b)
{
    const Integer divisor = std::gcd(a.d, b.d);
    return {AddInteger(MultiplyInteger(a.n, b.d / divisor), MultiplyInteger(b.n, a.d / divisor)), MultiplyInteger(a.d, b.d / divisor)};
}
Rational operator-(Rational a, Rational b)
{
    return a + Rational{-b.n, b.d};
}
Rational operator*(Rational a, Rational b)
{
    const Integer first = std::gcd(a.n, b.d), second = std::gcd(b.n, a.d);
    return {MultiplyInteger(a.n / first, b.n / second), MultiplyInteger(a.d / second, b.d / first)};
}
Rational operator/(Rational a, Rational b)
{
    return a * Rational{b.d, b.n};
}
Rational FromDouble(double value)
{
    if (!std::isfinite(value))
        throw Unsupported{};
    if (value == 0)
        return {};
    int exponent;
    const double fraction = std::frexp(value, &exponent);
    Integer numerator = static_cast<Integer>(std::ldexp(fraction, 53));
    exponent -= 53;
    while (exponent < 0 && numerator % 2 == 0)
    {
        numerator /= 2;
        ++exponent;
    }
    Integer denominator = 1;
    while (exponent < 0)
    {
        denominator = MultiplyInteger(denominator, 2);
        ++exponent;
    }
    while (exponent > 0)
    {
        numerator = MultiplyInteger(numerator, 2);
        --exponent;
    }
    return {numerator, denominator};
}
long double Approximate(Rational value)
{
    return static_cast<long double>(value.n) / value.d;
}
using Polynomial = std::vector<Rational>;
void Trim(Polynomial &polynomial)
{
    while (polynomial.size() > 1 && polynomial.back().n == 0)
        polynomial.pop_back();
    if (polynomial.size() > 13)
        throw Unsupported{};
}
bool IsZero(const Polynomial &polynomial)
{
    return polynomial.size() == 1 && polynomial.front().n == 0;
}
Polynomial Add(Polynomial a, const Polynomial &b, int sign = 1)
{
    a.resize(std::max(a.size(), b.size()));
    for (std::size_t i = 0; i < b.size(); ++i)
        a[i] = a[i] + b[i] * Rational{sign};
    Trim(a);
    return a;
}
Polynomial Multiply(const Polynomial &a, const Polynomial &b)
{
    if (a.size() + b.size() > 14)
        throw Unsupported{};
    Polynomial result(a.size() + b.size() - 1);
    for (std::size_t i = 0; i < a.size(); ++i)
        for (std::size_t j = 0; j < b.size(); ++j)
            result[i + j] = result[i + j] + a[i] * b[j];
    Trim(result);
    return result;
}
Polynomial Scale(Polynomial a, Rational scale)
{
    for (auto &coefficient : a)
        coefficient = coefficient * scale;
    Trim(a);
    return a;
}
Polynomial Differentiate(const Polynomial &a)
{
    if (a.size() == 1)
        return {{0}};
    Polynomial result(a.size() - 1);
    for (std::size_t i = 1; i < a.size(); ++i)
        result[i - 1] = a[i] * Rational{static_cast<Integer>(i)};
    Trim(result);
    return result;
}
std::pair<Polynomial, Polynomial> Divide(Polynomial numerator, const Polynomial &denominator)
{
    if (IsZero(denominator))
        throw Unsupported{};
    Polynomial quotient(numerator.size() >= denominator.size() ? numerator.size() - denominator.size() + 1 : 1);
    while (!IsZero(numerator) && numerator.size() >= denominator.size())
    {
        const std::size_t offset = numerator.size() - denominator.size();
        const Rational factor = numerator.back() / denominator.back();
        quotient[offset] = factor;
        for (std::size_t i = 0; i < denominator.size(); ++i)
            numerator[i + offset] = numerator[i + offset] - factor * denominator[i];
        Trim(numerator);
    }
    Trim(quotient);
    return {quotient, numerator};
}
Polynomial Gcd(Polynomial a, Polynomial b)
{
    while (!IsZero(b))
    {
        auto remainder = Divide(a, b).second;
        a = b;
        b = remainder;
    }
    return Scale(a, Rational{1} / a.back());
}
struct Fraction
{
    Polynomial numerator{{0}};
    Polynomial denominator{{1}};
    std::vector<Polynomial> exclusions;
};
Fraction Reduce(Fraction result)
{
    if (IsZero(result.denominator))
        throw Unsupported{};
    const auto divisor = Gcd(result.numerator, result.denominator);
    result.numerator = Divide(result.numerator, divisor).first;
    result.denominator = Divide(result.denominator, divisor).first;
    const auto leading = result.denominator.back();
    result.numerator = Scale(result.numerator, Rational{1} / leading);
    result.denominator = Scale(result.denominator, Rational{1} / leading);
    return result;
}
Fraction Convert(const NodePtr &node, const std::wstring &variable, const EvalContext &context, Budget &budget, unsigned depth = 0)
{
    budget.Check();
    if (!node || depth > 64)
        throw Unsupported{};
    if (node->kind == NodeKind::Number)
        return {{{FromDouble(node->number)}}, {{1}}, {}};
    if (node->kind == NodeKind::Variable)
    {
        if (node->name == variable)
            return {{{0}, {1}}, {{1}}, {}};
        const auto found = context.variables.find(node->name);
        if (found == context.variables.end())
            throw Unsupported{};
        return {{{FromDouble(found->second)}}, {{1}}, {}};
    }
    if (node->kind == NodeKind::Unary && node->children.size() == 1)
    {
        auto result = Convert(node->children[0], variable, context, budget, depth + 1);
        if (node->name == L"-")
            result.numerator = Scale(result.numerator, {-1});
        else if (node->name != L"+")
            throw Unsupported{};
        return result;
    }
    if (node->kind != NodeKind::Binary || node->children.size() != 2)
        throw Unsupported{};
    auto a = Convert(node->children[0], variable, context, budget, depth + 1);
    auto b = Convert(node->children[1], variable, context, budget, depth + 1);
    a.exclusions.insert(a.exclusions.end(), b.exclusions.begin(), b.exclusions.end());
    if (node->name == L"+" || node->name == L"-")
        return Reduce({Add(Multiply(a.numerator, b.denominator), Multiply(b.numerator, a.denominator), node->name == L"+" ? 1 : -1),
                       Multiply(a.denominator, b.denominator), a.exclusions});
    if (node->name == L"*")
        return Reduce({Multiply(a.numerator, b.numerator), Multiply(a.denominator, b.denominator), a.exclusions});
    if (node->name == L"/")
    {
        a.exclusions.push_back(b.numerator);
        return Reduce({Multiply(a.numerator, b.denominator), Multiply(a.denominator, b.numerator), a.exclusions});
    }
    if (node->name == L"^")
    {
        if (b.numerator.size() != 1 || b.denominator.size() != 1 || !b.exclusions.empty())
            throw Unsupported{};
        const Rational power = b.numerator[0] / b.denominator[0];
        if (power.d != 1 || std::abs(power.n) > 12)
            throw Unsupported{};
        Fraction result{{{1}}, {{1}}, a.exclusions};
        if (power.n < 0)
        {
            result.exclusions.push_back(a.numerator);
            std::swap(a.numerator, a.denominator);
        }
        for (Integer i = 0; i < std::abs(power.n); ++i)
        {
            result.numerator = Multiply(result.numerator, a.numerator);
            result.denominator = Multiply(result.denominator, a.denominator);
        }
        return Reduce(result);
    }
    throw Unsupported{};
}
long double EvaluatePolynomial(const Polynomial &polynomial, long double x)
{
    long double value = 0;
    for (auto it = polynomial.rbegin(); it != polynomial.rend(); ++it)
        value = value * x + Approximate(*it);
    if (!std::isfinite(value))
        throw Unsupported{};
    return value;
}
struct Interval
{
    long double lo;
    long double hi;
};
long double Down(long double value)
{
    return std::nextafter(value, -std::numeric_limits<long double>::infinity());
}
long double Up(long double value)
{
    return std::nextafter(value, std::numeric_limits<long double>::infinity());
}
int SignAt(const Polynomial &polynomial, long double x)
{
    Interval value{0, 0};
    for (auto it = polynomial.rbegin(); it != polynomial.rend(); ++it)
    {
        const long double first = value.lo * x, second = value.hi * x;
        const long double numerator = static_cast<long double>(it->n), denominator = static_cast<long double>(it->d);
        const long double ratios[] = {Down(numerator) / Down(denominator), Down(numerator) / Up(denominator),
                                      Up(numerator) / Down(denominator), Up(numerator) / Up(denominator)};
        const long double coefficient_lo = Down(*std::min_element(std::begin(ratios), std::end(ratios)));
        const long double coefficient_hi = Up(*std::max_element(std::begin(ratios), std::end(ratios)));
        value = {Down(Down(std::min(first, second)) + coefficient_lo), Up(Up(std::max(first, second)) + coefficient_hi)};
    }
    if (!std::isfinite(value.lo) || !std::isfinite(value.hi))
        throw Unsupported{};
    if (value.lo > 0)
        return 1;
    if (value.hi < 0)
        return -1;
    // Exact zero is required before omitting a Sturm term.
    try
    {
        const Rational argument = FromDouble(static_cast<double>(x));
        if (Approximate(argument) != x)
            throw Unsupported{};
        Rational exact;
        for (auto it = polynomial.rbegin(); it != polynomial.rend(); ++it)
            exact = exact * argument + *it;
        return exact.n > 0 ? 1 : exact.n < 0 ? -1 : 0;
    }
    catch (const Unsupported &)
    {
        throw Unsupported{};
    }
}
std::vector<Polynomial> Sturm(const Polynomial &polynomial)
{
    auto square_free = Divide(polynomial, Gcd(polynomial, Differentiate(polynomial))).first;
    std::vector<Polynomial> sequence{square_free, Differentiate(square_free)};
    while (!IsZero(sequence.back()))
    {
        auto remainder = Divide(sequence[sequence.size() - 2], sequence.back()).second;
        if (IsZero(remainder))
            break;
        // Positive normalization preserves Sturm signs and reduces integer growth.
        remainder = Scale(remainder, Rational{-1} / Rational{std::abs(remainder.back().n), remainder.back().d});
        sequence.push_back(std::move(remainder));
    }
    return sequence;
}
int Variations(const std::vector<Polynomial> &sequence, long double x)
{
    int previous = 0, count = 0;
    for (const auto &polynomial : sequence)
    {
        const int sign = SignAt(polynomial, x);
        if (sign != 0)
        {
            if (previous != 0 && previous != sign)
                ++count;
            previous = sign;
        }
    }
    return count;
}
std::vector<long double> FindRoots(const Polynomial &polynomial, Budget &budget)
{
    if (polynomial.size() == 1)
        return {};
    const auto sequence = Sturm(polynomial);
    long double bound = 2;
    for (std::size_t i = 0; i + 1 < polynomial.size(); ++i)
        bound = std::max(bound, 2 + std::abs(Approximate(polynomial[i] / polynomial.back())));
    bound = std::ceil(bound);
    if (!std::isfinite(bound) || bound > 1e12L)
        throw Unsupported{};
    struct Bracket
    {
        long double lo;
        long double hi;
        int left;
        int right;
        unsigned depth;
        bool exact_hi = false;
    };
    std::vector<Bracket> pending{{-bound, bound, Variations(sequence, -bound), Variations(sequence, bound), 0}};
    std::vector<long double> roots;
    while (!pending.empty())
    {
        budget.Check();
        const Bracket bracket = pending.back();
        pending.pop_back();
        const int count = bracket.left - bracket.right;
        if (count == 0)
            continue;
        if (count < 0 || bracket.depth > 80)
            throw Unsupported{};
        if (count == 1 && bracket.exact_hi)
        {
            roots.push_back(bracket.hi);
            continue;
        }
        if (count == 1 && bracket.hi - bracket.lo < 1e-11L * std::max(1.0L, std::abs(bracket.lo)))
        {
            roots.push_back((bracket.lo + bracket.hi) / 2);
            continue;
        }
        const long double midpoint = (bracket.lo + bracket.hi) / 2;
        if (midpoint == bracket.lo || midpoint == bracket.hi)
            throw Unsupported{};
        const int middle = Variations(sequence, midpoint);
        const bool exact_midpoint = SignAt(sequence.front(), midpoint) == 0;
        pending.push_back({midpoint, bracket.hi, middle, bracket.right, bracket.depth + 1, bracket.exact_hi});
        pending.push_back({bracket.lo, midpoint, bracket.left, middle, bracket.depth + 1, exact_midpoint});
    }
    std::sort(roots.begin(), roots.end());
    return roots;
}
std::wstring Number(long double value)
{
    std::wostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(12) << value;
    return L"<mn>" + output.str() + L"</mn>";
}
std::wstring Math(const std::wstring &content)
{
    return L"<math xmlns=\"http://www.w3.org/1998/Math/MathML\"><mrow>" + content + L"</mrow></math>";
}
std::wstring Coordinate(long double x, long double y)
{
    return Math(L"<mo>(</mo>" + Number(x) + L"<mo>,</mo>" + Number(y) + L"<mo>)</mo>");
}
std::wstring IntervalMath(long double lo, long double hi, bool closed_lo = false, bool closed_hi = false)
{
    return Math(std::wstring(closed_lo ? L"<mo>[</mo>" : L"<mo>(</mo>") + (std::isinf(lo) ? L"<mo>−∞</mo>" : Number(lo)) + L"<mo>,</mo>" +
                (std::isinf(hi) ? L"<mo>∞</mo>" : Number(hi)) + (closed_hi ? L"<mo>]</mo>" : L"<mo>)</mo>"));
}
std::wstring RootSet(const std::vector<long double> &roots)
{
    std::wstring content = L"<mo>{</mo>";
    for (std::size_t i = 0; i < roots.size(); ++i)
    {
        if (i)
            content += L"<mo>,</mo>";
        content += Number(roots[i]);
    }
    return Math(content + L"<mo>}</mo>");
}
constexpr long double Infinity = std::numeric_limits<long double>::infinity();
constexpr unsigned Domain = 1, Range = 2, Parity = 4, Intercepts = 8, Critical = 16, Asymptotes = 32, Monotonicity = 64, Period = 128;
int FeatureBits(unsigned requested)
{
    return ((requested & Domain) ? 1 : 0) | ((requested & Range) ? 2 : 0) | ((requested & Parity) ? 4 : 0) |
           ((requested & Period) ? 8 : 0) | ((requested & Intercepts) ? 48 : 0) | ((requested & Critical) ? 448 : 0) |
           ((requested & Asymptotes) ? 3584 : 0) | ((requested & Monotonicity) ? 4096 : 0);
}
Graphing::IGraphFunctionAnalysisData EmptyData()
{
    return {};
}
std::vector<long double> UnionRoots(const std::vector<Polynomial> &polynomials, Budget &budget)
{
    // The square-free product makes coincident exclusions exact before root isolation.
    Polynomial product{{1}};
    for (const auto &polynomial : polynomials)
    {
        if (IsZero(polynomial))
            throw Unsupported{};
        product = Divide(Multiply(product, polynomial), Gcd(product, polynomial)).first;
    }
    return FindRoots(product, budget);
}
Polynomial RemoveExcluded(Polynomial polynomial, const std::vector<Polynomial> &exclusions)
{
    if (IsZero(polynomial))
        return polynomial;
    for (const auto &excluded : exclusions)
        for (;;)
        {
            const auto divisor = Gcd(polynomial, excluded);
            if (divisor.size() == 1)
                break;
            polynomial = Divide(polynomial, divisor).first;
        }
    return polynomial;
}
void RequireSeparated(const std::vector<long double> &roots)
{
    // Feature coordinates are approximations. Reject clusters whose classification could depend on rounding.
    for (std::size_t i = 1; i < roots.size(); ++i)
        if (roots[i] - roots[i - 1] < 1e-7L * std::max({1.0L, std::abs(roots[i]), std::abs(roots[i - 1])}))
            throw Unsupported{};
}
bool ContainsNear(const std::vector<long double> &values, long double value)
{
    return std::any_of(values.begin(), values.end(),
                       [value](long double candidate) { return std::abs(candidate - value) < 1e-8L * std::max(1.0L, std::abs(value)); });
}
long double FractionValue(const Fraction &fraction, long double x)
{
    const long double value = EvaluatePolynomial(fraction.numerator, x) / EvaluatePolynomial(fraction.denominator, x);
    if (!std::isfinite(value))
        throw Unsupported{};
    return value;
}
std::wstring DomainMath(const std::vector<long double> &exclusions, const std::wstring &variable)
{
    if (exclusions.empty())
        return Math(L"<mi>ℝ</mi>");
    std::wstring content = L"<mo>{</mo><mi>" + variable + L"</mi><mo>∈</mo><mi>ℝ</mi><mo>|</mo>";
    for (std::size_t i = 0; i < exclusions.size(); ++i)
    {
        if (i)
            content += L"<mo>,</mo>";
        content += L"<mi>" + variable + L"</mi><mo>≠</mo>" + Number(exclusions[i]);
    }
    return Math(content + L"<mo>}</mo>");
}
Polynomial Reflect(Polynomial polynomial)
{
    for (std::size_t i = 1; i < polynomial.size(); i += 2)
        polynomial[i].n = -polynomial[i].n;
    return polynomial;
}
void FillAlgebraic(const Fraction &fraction, unsigned requested, const std::wstring &variable, Budget &budget,
                   Graphing::IGraphFunctionAnalysisData &data, FeatureGeometry *geometry = nullptr)
{
    const auto point = [&variable](long double x, long double y) {
        return variable == L"x" ? Point{static_cast<double>(x), static_cast<double>(y)}
                                : Point{static_cast<double>(y), static_cast<double>(x)};
    };
    auto excluded_polynomials = fraction.exclusions;
    excluded_polynomials.push_back(fraction.denominator);
    const auto exclusions = UnionRoots(excluded_polynomials, budget);
    const bool polynomial = fraction.denominator.size() == 1 && exclusions.empty();
    if (requested & Domain)
        data.Domain = DomainMath(exclusions, variable);
    if (requested & Parity)
    {
        auto excluded = Polynomial{{1}};
        for (const auto &p : excluded_polynomials)
            excluded = Divide(Multiply(excluded, p), Gcd(excluded, p)).first;
        const auto symmetric_factors = Gcd(excluded, Reflect(excluded));
        const bool symmetric = FindRoots(Divide(excluded, symmetric_factors).first, budget).empty();
        if (!symmetric)
            data.Parity = 3;
        else
        {
            const auto first = Multiply(Reflect(fraction.numerator), fraction.denominator);
            const auto second = Multiply(fraction.numerator, Reflect(fraction.denominator));
            data.Parity = IsZero(Add(first, second, -1)) ? 2 : IsZero(Add(first, second)) ? 1 : 3;
        }
    }
    if (requested & Intercepts)
    {
        if (IsZero(fraction.numerator))
            data.Zeros = DomainMath(exclusions, variable);
        else
        {
            const auto allowed = RemoveExcluded(fraction.numerator, excluded_polynomials);
            const auto zeros = FindRoots(allowed, budget);
            data.Zeros = RootSet(zeros);
            if (geometry)
                for (const auto root : zeros)
                    geometry->zeros.push_back(point(root, 0));
        }
        const bool zero_excluded =
            std::any_of(excluded_polynomials.begin(), excluded_polynomials.end(), [](const Polynomial &p) { return p[0].n == 0; });
        data.YIntercept = zero_excluded ? Math(L"<mo>∅</mo>") : Coordinate(0, FractionValue(fraction, 0));
        if (geometry && !zero_excluded)
            geometry->y_intercept = point(0, FractionValue(fraction, 0));
    }
    if (requested & Period)
    {
        if (fraction.numerator.size() == 1 && fraction.denominator.size() == 1 && exclusions.empty())
        {
            data.PeriodicityDirection = 1;
            data.PeriodicityExpression = Math(L"<mi>T</mi><mo>&gt;</mo><mn>0</mn>");
        }
        else
            data.PeriodicityDirection = 2;
    }
    if (requested & Asymptotes)
    {
        for (const long double root : FindRoots(fraction.denominator, budget))
        {
            data.VerticalAsymptotes.push_back(Math(L"<mi>" + variable + L"</mi><mo>=</mo>" + Number(root)));
            if (geometry)
                geometry->vertical_asymptotes.push_back(static_cast<double>(root));
        }
        const auto quotient = Divide(fraction.numerator, fraction.denominator).first;
        const std::wstring dependent = variable == L"x" ? L"y" : L"x";
        if (!polynomial && quotient.size() == 1)
        {
            data.HorizontalAsymptotes.push_back(Math(L"<mi>" + dependent + L"</mi><mo>=</mo>" + Number(Approximate(quotient[0]))));
            if (geometry)
                geometry->horizontal_asymptotes.push_back(static_cast<double>(Approximate(quotient[0])));
        }
        if (!polynomial && quotient.size() == 2 && fraction.denominator.size() > 1)
        {
            if (geometry)
                geometry->oblique_asymptotes.emplace_back(static_cast<double>(Approximate(quotient[1])),
                                                          static_cast<double>(Approximate(quotient[0])));
            data.ObliqueAsymptotes.push_back(Math(L"<mi>" + dependent + L"</mi><mo>=</mo>" + Number(Approximate(quotient[1])) + L"<mi>" +
                                                  variable + L"</mi><mo>+</mo>" + Number(Approximate(quotient[0]))));
        }
    }
    if (!(requested & (Critical | Monotonicity | Range)))
        return;
    if ((requested & Range) && !polynomial)
        data.TooComplexFeatures |= 2;
    if (!(requested & (Critical | Monotonicity)) && !polynomial)
        return;
    const auto derivative = Add(Multiply(Differentiate(fraction.numerator), fraction.denominator),
                                Multiply(fraction.numerator, Differentiate(fraction.denominator)), -1);
    const auto critical_roots = (requested & (Critical | Monotonicity | Range))
                                    ? FindRoots(RemoveExcluded(derivative, excluded_polynomials), budget)
                                    : std::vector<long double>{};
    auto boundaries = exclusions;
    boundaries.insert(boundaries.end(), critical_roots.begin(), critical_roots.end());
    std::sort(boundaries.begin(), boundaries.end());
    RequireSeparated(boundaries);
    boundaries.insert(boundaries.begin(), -Infinity);
    boundaries.push_back(Infinity);
    std::vector<int> directions;
    for (std::size_t i = 1; i < boundaries.size(); ++i)
    {
        const auto lo = boundaries[i - 1], hi = boundaries[i];
        const auto probe = std::isinf(lo)   ? (std::isinf(hi) ? 0 : hi - std::max(1.0L, std::abs(hi)))
                           : std::isinf(hi) ? lo + std::max(1.0L, std::abs(lo))
                                            : (lo + hi) / 2;
        const int sign = SignAt(derivative, probe);
        directions.push_back(sign);
        if (requested & Monotonicity)
            data.MonotoneIntervals[IntervalMath(lo, hi)] = sign > 0 ? 1 : sign < 0 ? 2 : 3;
    }
    if (requested & Critical)
    {
        for (std::size_t i = 1; i + 1 < boundaries.size(); ++i)
        {
            const auto root = boundaries[i];
            if (ContainsNear(exclusions, root))
                continue;
            if (directions[i - 1] < 0 && directions[i] > 0)
            {
                data.Minima.push_back(Coordinate(root, FractionValue(fraction, root)));
                if (geometry)
                    geometry->minima.push_back(point(root, FractionValue(fraction, root)));
            }
            if (directions[i - 1] > 0 && directions[i] < 0)
            {
                data.Maxima.push_back(Coordinate(root, FractionValue(fraction, root)));
                if (geometry)
                    geometry->maxima.push_back(point(root, FractionValue(fraction, root)));
            }
        }
        const auto second = Add(Multiply(Differentiate(derivative), fraction.denominator),
                                Scale(Multiply(derivative, Differentiate(fraction.denominator)), {2}), -1);
        auto inflection_boundaries = exclusions;
        const auto second_roots = FindRoots(RemoveExcluded(second, excluded_polynomials), budget);
        inflection_boundaries.insert(inflection_boundaries.end(), second_roots.begin(), second_roots.end());
        std::sort(inflection_boundaries.begin(), inflection_boundaries.end());
        RequireSeparated(inflection_boundaries);
        inflection_boundaries.insert(inflection_boundaries.begin(), -Infinity);
        inflection_boundaries.push_back(Infinity);
        for (std::size_t i = 1; i + 1 < inflection_boundaries.size(); ++i)
        {
            const auto root = inflection_boundaries[i];
            if (ContainsNear(exclusions, root))
                continue;
            const auto before = std::isinf(inflection_boundaries[i - 1]) ? root - std::max(1.0L, std::abs(root))
                                                                         : (inflection_boundaries[i - 1] + root) / 2;
            const auto after = std::isinf(inflection_boundaries[i + 1]) ? root + std::max(1.0L, std::abs(root))
                                                                        : (inflection_boundaries[i + 1] + root) / 2;
            const int left = SignAt(second, before) * SignAt(fraction.denominator, before);
            const int right = SignAt(second, after) * SignAt(fraction.denominator, after);
            if (left * right < 0)
            {
                data.InflectionPoints.push_back(Coordinate(root, FractionValue(fraction, root)));
                if (geometry)
                    geometry->inflections.push_back(point(root, FractionValue(fraction, root)));
            }
        }
    }
    if (requested & Range)
    {
        if (!polynomial)
            data.TooComplexFeatures |= 2;
        else if (fraction.numerator.size() == 1)
            data.Range = RootSet({Approximate(fraction.numerator[0])});
        else if (fraction.numerator.size() % 2 == 0)
            data.Range = Math(L"<mi>ℝ</mi>");
        else
        {
            if (critical_roots.empty())
                throw Unsupported{};
            long double extreme = FractionValue(fraction, critical_roots.front());
            const bool minimum = fraction.numerator.back().n > 0;
            for (const auto root : critical_roots)
                extreme = minimum ? std::min(extreme, FractionValue(fraction, root)) : std::max(extreme, FractionValue(fraction, root));
            data.Range = minimum ? IntervalMath(extreme, Infinity, true) : IntervalMath(-Infinity, extreme, false, true);
        }
    }
}
// Elementary support uses exact affine arguments, never viewport samples.
void FillElementary(const NodePtr &node, unsigned requested, const std::wstring &variable, const EvalContext &context, Budget &budget,
                    Graphing::IGraphFunctionAnalysisData &data, FeatureGeometry *geometry = nullptr)
{
    if (!node || node->kind != NodeKind::Call || node->children.size() != 1)
        throw Unsupported{};
    const auto &name = node->name;
    if (name != L"sin" && name != L"cos" && name != L"tan" && name != L"ln" && name != L"sqrt" && name != L"abs" && name != L"exp")
        throw Unsupported{};
    const auto argument = Convert(node->children[0], variable, context, budget);
    if (argument.denominator.size() != 1 || !argument.exclusions.empty() || argument.numerator.size() != 2 || argument.numerator[1].n == 0)
        throw Unsupported{};
    const long double a = Approximate(argument.numerator[1]), b = Approximate(argument.numerator[0]);
    const long double boundary = -b / a;
    const bool trigonometric = name == L"sin" || name == L"cos" || name == L"tan";
    const long double pi = std::acos(-1.0L);
    const long double angle_scale = context.trig_unit == Graphing::EvalTrigUnitMode::Degrees ? pi / 180
                                    : context.trig_unit == Graphing::EvalTrigUnitMode::Grads ? pi / 200
                                                                                             : 1;
    if (trigonometric && context.trig_unit == Graphing::EvalTrigUnitMode::Invalid)
        throw Unsupported{};
    const auto point = [&variable](long double x, long double y) {
        return variable == L"x" ? Point{static_cast<double>(x), static_cast<double>(y)}
                                : Point{static_cast<double>(y), static_cast<double>(x)};
    };
    const std::wstring half_turn = context.trig_unit == Graphing::EvalTrigUnitMode::Degrees ? L"<mn>180</mn>"
                                   : context.trig_unit == Graphing::EvalTrigUnitMode::Grads ? L"<mn>200</mn>"
                                                                                            : L"<mi>π</mi>";
    const auto family = [&](bool half_offset) {
        return L"<mfrac><mrow>" +
               std::wstring(half_offset ? L"<mo>(</mo><mi>k</mi><mo>+</mo><mfrac><mn>1</mn><mn>2</mn></mfrac><mo>)</mo>" : L"<mi>k</mi>") +
               half_turn + L"<mo>−</mo>" + Number(b) + L"</mrow>" + Number(a) + L"</mfrac>";
    };
    if (requested & Domain)
    {
        if (name == L"ln" || name == L"sqrt")
            data.Domain =
                a > 0 ? IntervalMath(boundary, Infinity, name == L"sqrt") : IntervalMath(-Infinity, boundary, false, name == L"sqrt");
        else if (name == L"tan")
            data.Domain = Math(L"<mo>{</mo><mi>" + variable + L"</mi><mo>∈</mo><mi>ℝ</mi><mo>|</mo><mi>" + variable + L"</mi><mo>≠</mo>" +
                               family(true) + L"<mo>,</mo><mi>k</mi><mo>∈</mo><mi>ℤ</mi><mo>}</mo>");
        else
            data.Domain = Math(L"<mi>ℝ</mi>");
    }
    if (requested & Range)
        data.Range = name == L"sin" || name == L"cos"    ? IntervalMath(-1, 1, true, true)
                     : name == L"sqrt" || name == L"abs" ? IntervalMath(0, Infinity, true)
                     : name == L"exp"                    ? IntervalMath(0, Infinity)
                                                         : Math(L"<mi>ℝ</mi>");
    if (requested & Parity)
    {
        if (argument.numerator[0].n == 0 && (name == L"sin" || name == L"tan"))
            data.Parity = 1;
        else if (argument.numerator[0].n == 0 && (name == L"cos" || name == L"abs"))
            data.Parity = 2;
        else if (!trigonometric)
            data.Parity = 3;
        else
            data.TooComplexFeatures |= 4;
    }
    if (requested & Intercepts)
    {
        if (trigonometric)
            data.Zeros = Math(L"<mo>{</mo>" + family(name == L"cos") + L"<mo>|</mo><mi>k</mi><mo>∈</mo><mi>ℤ</mi><mo>}</mo>");
        else if (name == L"exp")
            data.Zeros = Math(L"<mo>∅</mo>");
        else
        {
            const long double root = name == L"ln" ? (1 - b) / a : boundary;
            data.Zeros = RootSet({root});
            if (geometry)
                geometry->zeros.push_back(point(root, 0));
        }
        bool has_intercept = true;
        long double value = 0;
        if (name == L"ln")
        {
            has_intercept = b > 0;
            if (has_intercept)
                value = std::log(b);
        }
        if (name == L"sqrt")
        {
            has_intercept = b >= 0;
            if (has_intercept)
                value = std::sqrt(b);
        }
        if (name == L"abs")
            value = std::abs(b);
        if (name == L"exp")
            value = std::exp(b);
        if (name == L"sin")
            value = std::sin(b * angle_scale);
        if (name == L"cos")
            value = std::cos(b * angle_scale);
        if (name == L"tan")
        {
            // Identifying an exact transcendental pole from a rounded numeric angle is unsupported.
            if (std::abs(std::cos(b * angle_scale)) < 1e-12L)
            {
                data.TooComplexFeatures |= 32;
                has_intercept = false;
            }
            else
                value = std::tan(b * angle_scale);
        }
        if (has_intercept && !std::isfinite(value))
            data.TooComplexFeatures |= 32;
        if (has_intercept && std::isfinite(value))
        {
            data.YIntercept = Coordinate(0, value);
            if (geometry)
                geometry->y_intercept = point(0, value);
        }
        else if (!(data.TooComplexFeatures & 32))
            data.YIntercept = Math(L"<mo>∅</mo>");
    }
    if (requested & Critical)
    {
        if (trigonometric)
            data.TooComplexFeatures |= 448;
        else if (name == L"abs" || name == L"sqrt")
        {
            data.Minima.push_back(Coordinate(boundary, 0));
            if (geometry)
                geometry->minima.push_back(point(boundary, 0));
        }
    }
    if (requested & Monotonicity)
    {
        if (trigonometric)
            data.TooComplexFeatures |= 4096;
        else if (name == L"abs")
        {
            data.MonotoneIntervals[IntervalMath(-Infinity, boundary)] = 2;
            data.MonotoneIntervals[IntervalMath(boundary, Infinity)] = 1;
        }
        else if (name == L"ln" || name == L"sqrt")
            data.MonotoneIntervals[a > 0 ? IntervalMath(boundary, Infinity) : IntervalMath(-Infinity, boundary)] = a > 0 ? 1 : 2;
        else
            data.MonotoneIntervals[IntervalMath(-Infinity, Infinity)] = a > 0 ? 1 : 2;
    }
    if (requested & Asymptotes)
    {
        if (name == L"tan")
            data.VerticalAsymptotes.push_back(
                Math(L"<mi>" + variable + L"</mi><mo>=</mo>" + family(true) + L"<mo>,</mo><mi>k</mi><mo>∈</mo><mi>ℤ</mi>"));
        if (name == L"ln")
        {
            data.VerticalAsymptotes.push_back(Math(L"<mi>" + variable + L"</mi><mo>=</mo>" + Number(boundary)));
            if (geometry)
                geometry->vertical_asymptotes.push_back(static_cast<double>(boundary));
        }
        if (name == L"abs")
        {
            const std::wstring dependent = variable == L"x" ? L"y" : L"x";
            for (const int sign : {-1, 1})
            {
                data.ObliqueAsymptotes.push_back(Math(L"<mi>" + dependent + L"</mi><mo>=</mo>" + Number(sign * a) + L"<mi>" + variable +
                                                      L"</mi><mo>+</mo>" + Number(sign * b)));
                if (geometry)
                    geometry->oblique_asymptotes.emplace_back(static_cast<double>(sign * a), static_cast<double>(sign * b));
            }
        }
        if (name == L"exp")
        {
            data.HorizontalAsymptotes.push_back(
                Math(L"<mi>" + std::wstring(variable == L"x" ? L"y" : L"x") + L"</mi><mo>=</mo><mn>0</mn>"));
            if (geometry)
                geometry->horizontal_asymptotes.push_back(0);
        }
    }
    if (requested & Period)
    {
        data.PeriodicityDirection = trigonometric ? 1 : 2;
        if (trigonometric)
            data.PeriodicityExpression = Math(L"<mfrac><mrow>" + std::wstring(name == L"tan" ? L"" : L"<mn>2</mn>") + half_turn +
                                              L"</mrow>" + Number(std::abs(a)) + L"</mfrac>");
    }
}
const Plot *ChoosePlot(const GraphState &state)
{
    const Plot *selected = nullptr;
    for (const auto &plot : state.plots)
        if (plot.selected)
        {
            if (selected)
                return nullptr;
            selected = &plot;
        }
    return selected ? selected : state.plots.size() == 1 ? &state.plots.front() : nullptr;
}
std::wstring Fingerprint(const GraphState &state)
{
    std::wostringstream value;
    value << state.revision << L':' << static_cast<int>(state.context.trig_unit);
    value << L':' << (state.options && state.options->GetAllowKeyGraphFeaturesForFunctionsWithParameters());
    for (const auto &entry : state.context.variables)
        value << L':' << entry.first << L'=' << std::hexfloat << entry.second;
    for (const auto &plot : state.plots)
        value << L':' << plot.id << L':' << plot.selected << L':' << static_cast<int>(plot.kind) << L':' << plot.first.get();
    return value.str();
}
class Analyzer final : public Graphing::Analyzer::IGraphAnalyzer
{
  public:
    explicit Analyzer(std::shared_ptr<GraphState> state) : state_(std::move(state))
    {
    }
    bool CanFunctionAnalysisBePerformed(bool &variable_is_not_x) override
    {
        variable_is_not_x = false;
        const Plot *plot = ChoosePlot(*state_);
        if (!plot)
            return false;
        variable_is_not_x = plot->kind == PlotKind::ExplicitX;
        if (plot->kind != PlotKind::ExplicitY && plot->kind != PlotKind::ExplicitX)
            return false;
        const std::wstring variable = variable_is_not_x ? L"y" : L"x";
        for (const auto &name : FindVariables(plot->first))
        {
            if (name == variable || name == L"pi" || name == L"e")
                continue;
            if (!state_->options || !state_->options->GetAllowKeyGraphFeaturesForFunctionsWithParameters() ||
                state_->context.variables.find(name) == state_->context.variables.end())
                return false;
        }
        return true;
    }
    HRESULT PerformFunctionAnalysis(unsigned requested) override
    {
        data_ = EmptyData();
        fingerprint_ = Fingerprint(*state_);
        if (requested & ~255u)
            return E_INVALIDARG;
        bool variable_is_not_x;
        if (!CanFunctionAnalysisBePerformed(variable_is_not_x))
            return E_NOTIMPL;
        if (requested == 0)
            return S_OK;
        const auto limit = state_->options ? state_->options->GetMaxExecutionTime() : 0;
        const auto milliseconds = limit == 0 ? 2000ULL : std::min(2000ULL, static_cast<unsigned long long>(limit));
        Budget budget{std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds)};
        try
        {
            const auto variable = variable_is_not_x ? L"y" : L"x";
            const auto expression = ChoosePlot(*state_)->first;
            auto result = EmptyData();
            if (expression && expression->kind == NodeKind::Call)
                FillElementary(expression, requested, variable, state_->context, budget, result);
            else
                FillAlgebraic(Convert(expression, variable, state_->context, budget), requested, variable, budget, result);
            data_ = std::move(result);
        }
        catch (const TimedOut &)
        {
            data_.TooComplexFeatures = FeatureBits(requested);
            return E_ABORT;
        }
        catch (const Unsupported &)
        {
            data_.TooComplexFeatures = FeatureBits(requested);
        }
        return S_OK;
    }
    HRESULT GetAnalysisTypeCaption(Graphing::Analyzer::AnalysisType type, std::wstring &output) const override
    {
        static const wchar_t *captions[] = {L"Domain",
                                            L"Range",
                                            L"Parity",
                                            L"Zeros",
                                            L"Y-intercept",
                                            L"Minima",
                                            L"Maxima",
                                            L"Inflection points",
                                            L"Vertical asymptotes",
                                            L"Horizontal asymptotes",
                                            L"Oblique asymptotes",
                                            L"Monotonicity",
                                            L"Period"};
        if (type < 0 || type > Graphing::Analyzer::AnalysisType_Period)
        {
            output.clear();
            return E_INVALIDARG;
        }
        output = captions[type];
        return S_OK;
    }
    HRESULT GetMessage(Graphing::Analyzer::GraphAnalyzerMessage message, std::wstring &output) const override
    {
        static const wchar_t *messages[] = {L"No data",
                                            L"No zeros",
                                            L"No y-intercept",
                                            L"No minima",
                                            L"No maxima",
                                            L"No inflection points",
                                            L"No vertical asymptotes",
                                            L"No horizontal asymptotes",
                                            L"No oblique asymptotes",
                                            L"Not able to calculate",
                                            L"Not able to mark all graph features",
                                            L"These features are too complex to calculate",
                                            L"This feature is too complex to calculate"};
        if (message < 0 || message > Graphing::Analyzer::GraphAnalyzerMessage_ThisFeatureIsTooComplexToCalculate)
        {
            output.clear();
            return E_INVALIDARG;
        }
        output = messages[message];
        return S_OK;
    }
    Graphing::IGraphFunctionAnalysisData Read() const
    {
        return fingerprint_ == Fingerprint(*state_) ? data_ : EmptyData();
    }

  private:
    std::shared_ptr<GraphState> state_;
    Graphing::IGraphFunctionAnalysisData data_{};
    std::wstring fingerprint_;
};
} // namespace
FeatureGeometry GetFeatureGeometry(const GraphState &state, unsigned plot_id)
{
    const auto found = std::find_if(state.plots.begin(), state.plots.end(), [plot_id](const Plot &plot) { return plot.id == plot_id; });
    if (found == state.plots.end() || (found->kind != PlotKind::ExplicitY && found->kind != PlotKind::ExplicitX))
        return {};
    const std::wstring variable = found->kind == PlotKind::ExplicitX ? L"y" : L"x";
    for (const auto &name : FindVariables(found->first))
        if (name != variable && (!state.options || !state.options->GetAllowKeyGraphFeaturesForFunctionsWithParameters() ||
                                 state.context.variables.find(name) == state.context.variables.end()))
            return {};
    const auto limit = state.options ? state.options->GetMaxExecutionTime() : 0;
    Budget budget{std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(limit == 0 ? 250 : std::min(250ULL, static_cast<unsigned long long>(limit)))};
    try
    {
        FeatureGeometry geometry;
        auto data = EmptyData();
        constexpr unsigned requested = Intercepts | Critical | Asymptotes;
        if (found->first && found->first->kind == NodeKind::Call)
            FillElementary(found->first, requested, variable, state.context, budget, data, &geometry);
        else
            FillAlgebraic(Convert(found->first, variable, state.context, budget), requested, variable, budget, data, &geometry);
        return geometry;
    }
    catch (const Unsupported &)
    {
        return {};
    }
    catch (const TimedOut &)
    {
        return {};
    }
}
std::shared_ptr<Graphing::Analyzer::IGraphAnalyzer> CreateAnalyzer(std::shared_ptr<GraphState> state)
{
    return std::make_shared<Analyzer>(std::move(state));
}
Graphing::IGraphFunctionAnalysisData ReadAnalysis(const Graphing::Analyzer::IGraphAnalyzer *analyzer)
{
    const auto *implementation = dynamic_cast<const Analyzer *>(analyzer);
    return implementation ? implementation->Read() : EmptyData();
}
} // namespace graphing::detail
