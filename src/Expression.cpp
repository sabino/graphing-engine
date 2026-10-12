// Licensed under the MIT License.
#include "Expression.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace graphing::detail
{
namespace
{
constexpr std::size_t MAX_EXPRESSION_INPUT = 65536;
constexpr std::size_t MAX_NODES = 8192;
constexpr unsigned MAX_DEPTH = 128;
constexpr double PI = 3.141592653589793238462643383279502884;
constexpr double UNDEFINED = std::numeric_limits<double>::quiet_NaN();

struct ParseFailure
{
    ParseErrorCode code;
    int type;
    std::wstring message;
};

[[noreturn]] void Fail(ParseErrorCode code, const wchar_t *message)
{
    const int type = code == ParseErrorCode::ComplexityLimit ? 2 : static_cast<int>(code) < 0 ? 0 : 1;
    throw ParseFailure{code, type, message};
}

std::wstring Lower(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return character >= L'A' && character <= L'Z' ? static_cast<wchar_t>(character + (L'a' - L'A')) : character;
    });
    return value;
}

bool IsDigit(wchar_t value)
{
    return value >= L'0' && value <= L'9';
}
bool IsSpace(wchar_t value)
{
    return value == L' ' || value == L'\t' || value == L'\r' || value == L'\n' || value == L'\f' || value == L'\v';
}
bool IsLetter(wchar_t value)
{
    return (value >= L'a' && value <= L'z') || (value >= L'A' && value <= L'Z') || (value >= L'Α' && value <= L'Ρ') ||
           (value >= L'Σ' && value <= L'Ω') || (value >= L'α' && value <= L'ω');
}

unsigned GetDepth(const NodePtr &node)
{
    unsigned depth = 1;
    for (const auto &child : node->children)
    {
        depth = std::max(depth, GetDepth(child) + 1);
        if (depth > MAX_DEPTH)
        {
            Fail(ParseErrorCode::ComplexityLimit, L"Expression depth exceeds 128.");
        }
    }
    return depth;
}

NodePtr MakeNode(NodeKind kind, std::wstring name = {}, std::vector<NodePtr> children = {}, double number = 0)
{
    auto node = std::make_shared<Node>();
    node->kind = kind;
    node->name = std::move(name);
    node->children = std::move(children);
    node->number = number;
    GetDepth(node);
    return node;
}

std::wstring CanonicalFunction(std::wstring name)
{
    name = Lower(std::move(name));
    if (name == L"arcsin")
        return L"asin";
    if (name == L"arccos")
        return L"acos";
    if (name == L"arctan")
        return L"atan";
    if (name == L"arccot")
        return L"acot";
    if (name == L"arcsec")
        return L"asec";
    if (name == L"arccsc")
        return L"acsc";
    if (name == L"arcsinh")
        return L"asinh";
    if (name == L"arccosh")
        return L"acosh";
    if (name == L"arctanh")
        return L"atanh";
    if (name == L"ceiling")
        return L"ceil";
    if (name == L"sgn")
        return L"sign";
    if (name == L"cuberoot")
        return L"cbrt";
    if (name == L"logbasey")
        return L"log";
    return name;
}

bool IsGraphFunction(const std::wstring &name)
{
    return name == L"show2d" || name == L"plot2d" || name == L"ploteq2d" || name == L"plotineq2d" || name == L"plotparam2d" ||
           name == L"plotpolar2d";
}

bool IsFunction(const std::wstring &name)
{
    static const std::set<std::wstring> functions = {
        L"sin",  L"cos",  L"tan",  L"cot",  L"sec",   L"csc",   L"asin",  L"acos",  L"atan",  L"acot",  L"asec",  L"acsc", L"sinh",
        L"cosh", L"tanh", L"coth", L"sech", L"csch",  L"asinh", L"acosh", L"atanh", L"acoth", L"asech", L"acsch", L"sqrt", L"cbrt",
        L"root", L"abs",  L"log",  L"ln",   L"log10", L"exp",   L"floor", L"ceil",  L"sign",  L"min",   L"max"};
    return functions.count(name) != 0 || IsGraphFunction(name);
}

NodePtr MakeCall(const std::wstring &original, std::vector<NodePtr> args, bool formula)
{
    const auto name = CanonicalFunction(original);
    const auto binary = [&](const wchar_t *operation) {
        if (args.size() != 2)
            Fail(ParseErrorCode::IncorrectArguments, L"Binary operation requires two arguments.");
        return MakeNode(NodeKind::Binary, operation, std::move(args));
    };
    if (formula)
    {
        if (name == L"list")
            return MakeNode(NodeKind::List, {}, std::move(args));
        if (name == L"aggregate")
        {
            if (args.size() != 1)
                Fail(ParseErrorCode::IncorrectArguments, L"Aggregate requires one argument.");
            return args.front();
        }
        if (name == L"sum" || name == L"product")
        {
            if (args.empty())
                Fail(ParseErrorCode::IncorrectArguments, L"Sum or Product requires an argument.");
            NodePtr result = args.front();
            for (std::size_t index = 1; index < args.size(); ++index)
            {
                result = MakeNode(NodeKind::Binary, name == L"sum" ? L"+" : L"*", {result, args[index]});
            }
            return result;
        }
        if (name == L"divide")
            return binary(L"/");
        if (name == L"modulo")
            return binary(L"%");
        if (name == L"subtract" || name == L"difference")
            return binary(L"-");
        if (name == L"power")
            return binary(L"^");
        if (name == L"equal" || name == L"equals")
            return binary(L"=");
        if (name == L"notequal")
            return binary(L"!=");
        if (name == L"lessthan")
            return binary(L"<");
        if (name == L"lessorequal" || name == L"lessthanorequal")
            return binary(L"<=");
        if (name == L"greaterthan")
            return binary(L">");
        if (name == L"greaterorequal" || name == L"greaterthanorequal")
            return binary(L">=");
        if (name == L"negative" || name == L"negate")
        {
            if (args.size() != 1)
                Fail(ParseErrorCode::IncorrectArguments, L"Negate requires one argument.");
            return MakeNode(NodeKind::Unary, L"-", std::move(args));
        }
    }
    if (!IsFunction(name))
        Fail(ParseErrorCode::Unsupported, L"Function is unsupported.");
    const bool correct = IsGraphFunction(name)              ? !args.empty()
                         : name == L"min" || name == L"max" ? !args.empty()
                         : name == L"root"                  ? args.size() == 2
                         : name == L"log"                   ? args.size() == 1 || args.size() == 2
                                                            : args.size() == 1;
    if (!correct)
        Fail(ParseErrorCode::IncorrectArguments, L"Incorrect number of function arguments.");
    return MakeNode(NodeKind::Call, name, std::move(args));
}

enum class TokenKind
{
    End,
    Number,
    Name,
    Operation,
    Open,
    Close,
    ListOpen,
    ListClose,
    FormulaOpen,
    FormulaClose,
    Separator
};
struct Token
{
    TokenKind kind;
    std::wstring text;
    double number = 0;
};

struct LinearParser
{
    const std::wstring &input;
    Graphing::LocalizationType localization;
    bool formula;
    std::size_t position = 0;
    std::size_t count = 0;
    unsigned depth = 0;
    Token token{TokenKind::End, {}};
    TokenKind previous = TokenKind::End;

    wchar_t Decimal() const
    {
        return localization == Graphing::LocalizationType::DecimalCommaAndListSemicolon ? L',' : L'.';
    }
    wchar_t Separator() const
    {
        return localization == Graphing::LocalizationType::DecimalPointAndListComma ? L',' : L';';
    }

    void Advance()
    {
        previous = token.kind;
        while (position < input.size() && IsSpace(input[position]))
            ++position;
        if (++count > MAX_NODES)
            Fail(ParseErrorCode::ComplexityLimit, L"Expression exceeds the token limit.");
        if (position == input.size())
        {
            token = {TokenKind::End, {}};
            return;
        }
        const wchar_t character = input[position];
        const std::size_t start = position++;
        if (IsDigit(character) || character == Decimal())
        {
            bool digits = IsDigit(character);
            bool decimal = character == Decimal();
            while (position < input.size() && (IsDigit(input[position]) || input[position] == Decimal()))
            {
                if (input[position] == Decimal())
                {
                    if (decimal)
                        Fail(ParseErrorCode::InvalidNumber, L"Number contains multiple decimal separators.");
                    decimal = true;
                }
                else
                    digits = true;
                ++position;
            }
            if (!digits)
                Fail(ParseErrorCode::InvalidNumber, L"Decimal separator requires digits.");
            if (position < input.size() && (input[position] == L'e' || input[position] == L'E') && position + 1 < input.size() &&
                (IsDigit(input[position + 1]) || input[position + 1] == L'+' || input[position + 1] == L'-'))
            {
                ++position;
                if (input[position] == L'+' || input[position] == L'-')
                    ++position;
                const auto exponentStart = position;
                while (position < input.size() && IsDigit(input[position]))
                    ++position;
                if (position == exponentStart)
                    Fail(ParseErrorCode::InvalidNumber, L"Exponent requires digits.");
            }
            std::wstring numeric = input.substr(start, position - start);
            std::replace(numeric.begin(), numeric.end(), L',', L'.');
            std::wistringstream stream(numeric);
            stream.imbue(std::locale::classic());
            double value = 0;
            stream >> value;
            if (stream.fail() || !std::isfinite(value))
                Fail(ParseErrorCode::OutOfDomain, L"Number is outside the finite real range.");
            token = {TokenKind::Number, std::move(numeric), value};
            return;
        }
        if (IsLetter(character) || character == L'π')
        {
            while (position < input.size() && (IsLetter(input[position]) || IsDigit(input[position]) || input[position] == L'_'))
                ++position;
            if (position - start > 128)
                Fail(ParseErrorCode::ComplexityLimit, L"Variable name exceeds 128 characters.");
            token = {TokenKind::Name, input.substr(start, position - start)};
            return;
        }
        switch (character)
        {
        case L'(':
            token = {TokenKind::Open, L"("};
            return;
        case L')':
            token = {TokenKind::Close, L")"};
            return;
        case L'{':
            token = {TokenKind::ListOpen, L"{"};
            return;
        case L'}':
            token = {TokenKind::ListClose, L"}"};
            return;
        case L'[':
            token = {TokenKind::FormulaOpen, L"["};
            return;
        case L']':
            token = {TokenKind::FormulaClose, L"]"};
            return;
        case L'+':
        case L'-':
        case L'*':
        case L'/':
        case L'^':
        case L'%':
        case L'=':
        case L'<':
        case L'>':
        case L'!':
            token = {TokenKind::Operation, std::wstring(1, character)};
            if (position < input.size() && input[position] == L'=' &&
                (character == L'<' || character == L'>' || character == L'!' || character == L'='))
            {
                ++position;
                token.text = character == L'=' ? L"=" : token.text + L"=";
            }
            if (token.text == L"!")
                Fail(ParseErrorCode::Unsupported, L"Factorial is unsupported.");
            return;
        case L'−':
            token = {TokenKind::Operation, L"-"};
            return;
        case L'×':
        case L'·':
        case L'⁢':
            token = {TokenKind::Operation, L"*"};
            return;
        case L'÷':
            token = {TokenKind::Operation, L"/"};
            return;
        case L'≤':
            token = {TokenKind::Operation, L"<="};
            return;
        case L'≥':
            token = {TokenKind::Operation, L">="};
            return;
        case L'≠':
            token = {TokenKind::Operation, L"!="};
            return;
        default:
            if (character == Separator())
            {
                token = {TokenKind::Separator, std::wstring(1, character)};
                return;
            }
            Fail(ParseErrorCode::InvalidToken, L"Invalid expression character.");
        }
    }

    bool IsOperation(const wchar_t *value) const
    {
        return token.kind == TokenKind::Operation && token.text == value;
    }
    bool IsComparison() const
    {
        return IsOperation(L"=") || IsOperation(L"!=") || IsOperation(L"<") || IsOperation(L"<=") || IsOperation(L">") ||
               IsOperation(L">=");
    }

    NodePtr Comparison()
    {
        auto result = Sum();
        if (IsComparison())
        {
            const auto operation = token.text;
            Advance();
            result = MakeNode(NodeKind::Binary, operation, {result, Sum()});
            if (IsComparison())
                Fail(ParseErrorCode::TooManyComparisons, L"Chained comparisons are unsupported.");
        }
        return result;
    }

    NodePtr Sum()
    {
        auto result = Product();
        while (IsOperation(L"+") || IsOperation(L"-"))
        {
            const auto operation = token.text;
            Advance();
            result = MakeNode(NodeKind::Binary, operation, {result, Product()});
        }
        return result;
    }

    NodePtr Product()
    {
        auto result = Unary();
        while (true)
        {
            const bool implicit = token.kind == TokenKind::Name || token.kind == TokenKind::Open ||
                                  (token.kind == TokenKind::Number && (previous == TokenKind::Close || previous == TokenKind::Name));
            if (!implicit && !IsOperation(L"*") && !IsOperation(L"/") && !IsOperation(L"%"))
                break;
            const auto operation = implicit ? L"*" : token.text;
            if (!implicit)
                Advance();
            result = MakeNode(NodeKind::Binary, operation, {result, Unary()});
        }
        return result;
    }

    NodePtr Unary()
    {
        if (++depth > MAX_DEPTH)
            Fail(ParseErrorCode::ComplexityLimit, L"Expression nesting exceeds 128.");
        NodePtr result;
        if (IsOperation(L"+") || IsOperation(L"-"))
        {
            const auto operation = token.text;
            Advance();
            result = MakeNode(NodeKind::Unary, operation, {Unary()});
        }
        else
        {
            result = Primary();
            if (IsOperation(L"^"))
            {
                Advance();
                result = MakeNode(NodeKind::Binary, L"^", {result, Unary()});
            }
        }
        --depth;
        return result;
    }

    std::vector<NodePtr> Arguments(TokenKind closing)
    {
        std::vector<NodePtr> args;
        if (token.kind != closing)
        {
            while (true)
            {
                args.push_back(Comparison());
                if (token.kind != TokenKind::Separator)
                    break;
                Advance();
            }
        }
        if (token.kind != closing)
            Fail(ParseErrorCode::ParenthesisMismatch, L"Closing delimiter is missing or mismatched.");
        Advance();
        return args;
    }

    NodePtr Primary()
    {
        if (token.kind == TokenKind::Number)
        {
            const double value = token.number;
            Advance();
            return MakeNode(NodeKind::Number, {}, {}, value);
        }
        if (token.kind == TokenKind::Open)
        {
            Advance();
            auto expression = Comparison();
            if (token.kind != TokenKind::Close)
                Fail(ParseErrorCode::ParenthesisMismatch, L"Closing parenthesis is missing.");
            Advance();
            return expression;
        }
        if (token.kind == TokenKind::ListOpen)
        {
            Advance();
            return MakeNode(NodeKind::List, {}, Arguments(TokenKind::ListClose));
        }
        if (token.kind == TokenKind::Name)
        {
            const auto original = token.text;
            const auto name = CanonicalFunction(original);
            Advance();
            if (token.kind == TokenKind::FormulaOpen)
            {
                if (!formula)
                    Fail(ParseErrorCode::Unsupported, L"Formula constructors require Formula input format.");
                Advance();
                return MakeCall(original, Arguments(TokenKind::FormulaClose), true);
            }
            if (IsFunction(name))
            {
                if (token.kind != TokenKind::Open)
                    Fail(ParseErrorCode::ExpectedFunctionParenthesis, L"Function requires parentheses.");
                Advance();
                return MakeCall(original, Arguments(TokenKind::Close), formula);
            }
            if (name == L"pi" || original == L"π")
                return MakeNode(NodeKind::Number, {}, {}, PI);
            if (name == L"e")
                return MakeNode(NodeKind::Number, {}, {}, std::exp(1.0));
            if (name == L"i")
                Fail(ParseErrorCode::OutOfDomain, L"Imaginary unit is unsupported in the real number field.");
            if (token.kind == TokenKind::Open && original.size() > 1)
                Fail(ParseErrorCode::Unsupported, L"Unknown function name; use explicit multiplication for a named variable.");
            return MakeNode(NodeKind::Variable, original);
        }
        Fail(token.kind == TokenKind::End ? ParseErrorCode::UnexpectedEnd : ParseErrorCode::UnexpectedToken,
             L"Expected a number, variable, function, or grouped expression.");
    }

    NodePtr Run()
    {
        Advance();
        if (token.kind == TokenKind::End)
            Fail(ParseErrorCode::EmptyExpression, L"Expression is empty.");
        auto result = Comparison();
        if (token.kind != TokenKind::End)
            Fail(ParseErrorCode::UnexpectedToken, L"Unexpected trailing expression token.");
        return result;
    }
};

std::wstring FormatNumber(double value, Graphing::LocalizationType localization)
{
    std::wostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    auto result = stream.str();
    if (localization == Graphing::LocalizationType::DecimalCommaAndListSemicolon)
        std::replace(result.begin(), result.end(), L'.', L',');
    return result;
}

std::wstring ToLinear(const NodePtr &node, Graphing::LocalizationType localization, unsigned depth = 0)
{
    if (!node || depth > MAX_DEPTH)
        throw std::invalid_argument("Invalid or excessively deep expression.");
    const auto child = [&](std::size_t index) { return ToLinear(node->children.at(index), localization, depth + 1); };
    if (node->kind == NodeKind::Number)
        return FormatNumber(node->number, localization);
    if (node->kind == NodeKind::Variable)
        return node->name;
    if (node->kind == NodeKind::Unary)
        return L"(" + node->name + child(0) + L")";
    if (node->kind == NodeKind::Binary)
        return L"(" + child(0) + node->name + child(1) + L")";
    const wchar_t separator = localization == Graphing::LocalizationType::DecimalPointAndListComma ? L',' : L';';
    std::wstring result = node->kind == NodeKind::List ? L"{" : node->name + L"(";
    for (std::size_t index = 0; index < node->children.size(); ++index)
    {
        if (index != 0)
            result += separator;
        result += child(index);
    }
    return result + (node->kind == NodeKind::List ? L"}" : L")");
}

std::wstring DecodeEntities(const std::wstring &text)
{
    std::wstring result;
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        if (text[index] != L'&')
        {
            result += text[index];
            continue;
        }
        const auto end = text.find(L';', index + 1);
        if (end == std::wstring::npos || end - index > 32)
            Fail(ParseErrorCode::UnknownMathMLEntity, L"Malformed MathML entity.");
        const auto name = text.substr(index + 1, end - index - 1);
        if (name == L"lt")
            result += L'<';
        else if (name == L"gt")
            result += L'>';
        else if (name == L"amp")
            result += L'&';
        else if (name == L"quot")
            result += L'"';
        else if (name == L"apos")
            result += L'\'';
        else if (name == L"pi")
            result += L'π';
        else if (name == L"minus")
            result += L'−';
        else if (name == L"times" || name == L"InvisibleTimes")
            result += L'*';
        else if (name == L"ApplyFunction")
            result += L'⁡';
        else if (name == L"le")
            result += L'≤';
        else if (name == L"ge")
            result += L'≥';
        else if (name == L"ne")
            result += L'≠';
        else if (!name.empty() && name.front() == L'#')
        {
            std::size_t digit = 1;
            unsigned base = 10;
            if (digit < name.size() && (name[digit] == L'x' || name[digit] == L'X'))
            {
                base = 16;
                ++digit;
            }
            if (digit == name.size())
                Fail(ParseErrorCode::UnknownMathMLEntity, L"Numeric entity requires digits.");
            unsigned code = 0;
            for (; digit < name.size(); ++digit)
            {
                const wchar_t character = name[digit];
                unsigned value = IsDigit(character)                       ? character - L'0'
                                 : character >= L'a' && character <= L'f' ? character - L'a' + 10
                                 : character >= L'A' && character <= L'F' ? character - L'A' + 10
                                                                          : 16;
                if (value >= base || code > (0x10ffff - value) / base)
                    Fail(ParseErrorCode::UnknownMathMLEntity, L"Invalid numeric MathML entity.");
                code = code * base + value;
            }
            if (code == 0 || (code < 32 && code != 9 && code != 10 && code != 13) || (code >= 0xd800 && code <= 0xdfff))
                Fail(ParseErrorCode::UnknownMathMLEntity, L"Invalid Unicode MathML entity.");
            if constexpr (sizeof(wchar_t) == 2)
            {
                if (code > 0xffff)
                {
                    result += static_cast<wchar_t>(0xd800 + ((code - 0x10000) >> 10));
                    result += static_cast<wchar_t>(0xdc00 + ((code - 0x10000) & 0x3ff));
                }
                else
                    result += static_cast<wchar_t>(code);
            }
            else
                result += static_cast<wchar_t>(code);
        }
        else
            Fail(ParseErrorCode::UnknownMathMLEntity, L"MathML entity is unsupported; external entities are disabled.");
        index = end;
    }
    return result;
}

struct XmlNode
{
    std::wstring name;
    std::wstring text;
    std::map<std::wstring, std::wstring> attributes;
    std::vector<XmlNode> children;
};

struct XmlParser
{
    const std::wstring &input;
    std::size_t position = 0;
    std::size_t nodes = 0;

    void SkipSpace()
    {
        while (position < input.size() && IsSpace(input[position]))
            ++position;
    }
    bool Starts(const wchar_t *text) const
    {
        return input.compare(position, std::char_traits<wchar_t>::length(text), text) == 0;
    }
    std::wstring Name()
    {
        const auto start = position;
        if (position >= input.size() || (!IsLetter(input[position]) && input[position] != L'_'))
            Fail(ParseErrorCode::InvalidMathML, L"Invalid XML name.");
        ++position;
        while (position < input.size() && (IsLetter(input[position]) || IsDigit(input[position]) || input[position] == L'_' ||
                                           input[position] == L':' || input[position] == L'-' || input[position] == L'.'))
            ++position;
        return input.substr(start, position - start);
    }

    XmlNode Element(unsigned depth)
    {
        if (++nodes > MAX_NODES || depth > MAX_DEPTH)
            Fail(ParseErrorCode::ComplexityLimit, L"MathML exceeds structural limits.");
        if (!Starts(L"<") || Starts(L"<!") || Starts(L"<?") || Starts(L"</"))
            Fail(ParseErrorCode::InvalidMathML, L"Expected a MathML element; declarations and external entities are disabled.");
        ++position;
        const auto qualified = Name();
        const auto colon = qualified.find(L':');
        XmlNode result;
        result.name = colon == std::wstring::npos ? qualified : qualified.substr(colon + 1);
        while (true)
        {
            SkipSpace();
            if (Starts(L"/>"))
            {
                position += 2;
                return result;
            }
            if (Starts(L">"))
            {
                ++position;
                break;
            }
            const auto attribute = Name();
            SkipSpace();
            if (!Starts(L"="))
                Fail(ParseErrorCode::InvalidMathML, L"XML attribute requires equals.");
            ++position;
            SkipSpace();
            if (position >= input.size() || (input[position] != L'\'' && input[position] != L'"'))
                Fail(ParseErrorCode::InvalidMathML, L"XML attribute requires a quoted value.");
            const wchar_t quote = input[position++];
            const auto start = position;
            while (position < input.size() && input[position] != quote)
            {
                if (input[position] == L'<')
                    Fail(ParseErrorCode::InvalidMathML, L"Invalid XML attribute character.");
                ++position;
            }
            if (position == input.size())
                Fail(ParseErrorCode::InvalidMathML, L"Unterminated XML attribute.");
            if (!result.attributes.emplace(attribute, DecodeEntities(input.substr(start, position - start))).second)
                Fail(ParseErrorCode::InvalidMathML, L"Duplicate XML attribute.");
            ++position;
        }
        while (!Starts(L"</"))
        {
            if (position >= input.size())
                Fail(ParseErrorCode::InvalidMathML, L"Unterminated MathML element.");
            if (Starts(L"<"))
                result.children.push_back(Element(depth + 1));
            else
            {
                const auto start = position;
                while (position < input.size() && input[position] != L'<')
                    ++position;
                result.text += DecodeEntities(input.substr(start, position - start));
            }
        }
        position += 2;
        if (Name() != qualified)
            Fail(ParseErrorCode::InvalidMathML, L"MathML closing element is mismatched.");
        SkipSpace();
        if (!Starts(L">"))
            Fail(ParseErrorCode::InvalidMathML, L"Invalid closing MathML element.");
        ++position;
        return result;
    }

    XmlNode Run()
    {
        SkipSpace();
        auto result = Element(0);
        SkipSpace();
        if (position != input.size())
            Fail(ParseErrorCode::InvalidMathML, L"MathML requires exactly one root element.");
        return result;
    }
};

std::wstring Trim(const std::wstring &value)
{
    const auto start = value.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos)
        return {};
    return value.substr(start, value.find_last_not_of(L" \t\r\n") - start + 1);
}

NodePtr ReadMathML(const XmlNode &xml, Graphing::LocalizationType localization);

std::wstring MathMLToken(const XmlNode &xml, Graphing::LocalizationType localization)
{
    if (xml.name == L"mi" || xml.name == L"mn" || xml.name == L"mo")
    {
        if (!xml.children.empty())
            Fail(ParseErrorCode::InvalidMathML, L"MathML token cannot contain elements.");
        auto text = Trim(xml.text);
        if (xml.name == L"mo" && text == L"⁡")
            return {};
        if (text.empty())
            Fail(ParseErrorCode::InvalidMathML, L"MathML token cannot be empty.");
        if (xml.name == L"mn")
        {
            const auto numeric = LinearParser{text, localization, false}.Run();
            if (numeric->kind != NodeKind::Number &&
                !(numeric->kind == NodeKind::Unary && numeric->children.size() == 1 && numeric->children.front()->kind == NodeKind::Number))
                Fail(ParseErrorCode::InvalidMathML, L"MathML mn must contain one numeric literal.");
        }
        if (xml.name == L"mi" && text != L"π" &&
            (!IsLetter(text.front()) || std::any_of(text.begin() + 1, text.end(), [](wchar_t character) {
                return !IsLetter(character) && !IsDigit(character) && character != L'_';
            })))
            Fail(ParseErrorCode::InvalidMathML, L"MathML mi must contain one identifier.");
        if (xml.name == L"mo")
        {
            static const std::set<std::wstring> operators = {L"+", L"-", L"−", L"*",  L"×", L"·",  L"⁢", L"/",  L"÷",
                                                             L"^", L"%", L"=", L"!=", L"<", L"<=", L">",   L">=", L"≤",
                                                             L"≥", L"≠", L"(", L")",  L"{", L"}",  L",",   L";"};
            if (operators.count(text) == 0)
                Fail(ParseErrorCode::Unsupported, L"MathML operator is unsupported.");
        }
        return text;
    }
    if (xml.name == L"mfenced")
    {
        const auto open = xml.attributes.count(L"open") ? xml.attributes.at(L"open") : L"(";
        const auto close = xml.attributes.count(L"close") ? xml.attributes.at(L"close") : L")";
        if (!((open == L"(" && close == L")") || (open == L"{" && close == L"}")))
            Fail(ParseErrorCode::Unsupported, L"MathML fence style is unsupported.");
        const wchar_t separator = localization == Graphing::LocalizationType::DecimalPointAndListComma ? L',' : L';';
        std::wstring result = open;
        for (std::size_t index = 0; index < xml.children.size(); ++index)
        {
            if (index != 0)
                result += separator;
            result += ToLinear(ReadMathML(xml.children[index], localization), localization);
        }
        return result + close;
    }
    return ToLinear(ReadMathML(xml, localization), localization);
}

NodePtr ReadMathML(const XmlNode &xml, Graphing::LocalizationType localization)
{
    const bool token = xml.name == L"mi" || xml.name == L"mn" || xml.name == L"mo";
    if (!token && !Trim(xml.text).empty())
        Fail(ParseErrorCode::InvalidMathML, L"MathML container cannot contain plain text.");
    if (xml.name == L"mfrac" || xml.name == L"msup" || xml.name == L"mroot")
    {
        if (xml.children.size() != 2)
            Fail(ParseErrorCode::InvalidMathML, L"MathML fraction, power, or root requires two children.");
        std::vector<NodePtr> children{ReadMathML(xml.children[0], localization), ReadMathML(xml.children[1], localization)};
        if (xml.name == L"mroot")
            return MakeCall(L"root", std::move(children), false);
        return MakeNode(NodeKind::Binary, xml.name == L"mfrac" ? L"/" : L"^", std::move(children));
    }
    if (xml.name == L"msqrt")
    {
        if (xml.children.empty())
            Fail(ParseErrorCode::InvalidMathML, L"MathML square root requires a radicand.");
        XmlNode row;
        row.name = L"mrow";
        row.children = xml.children;
        return MakeCall(L"sqrt", {ReadMathML(row, localization)}, false);
    }
    if (xml.name == L"msgraph")
    {
        std::vector<NodePtr> children;
        for (const auto &child : xml.children)
            children.push_back(ReadMathML(child, localization));
        return MakeCall(L"show2d", std::move(children), false);
    }
    if (xml.name == L"mrow" && xml.children.size() == 2 && xml.children[0].name == L"mi" && xml.children[1].name == L"mfenced" &&
        IsGraphFunction(Lower(Trim(xml.children[0].text))))
    {
        std::vector<NodePtr> args;
        bool afterSeparator = false;
        for (const auto &argument : xml.children[1].children)
        {
            if (argument.name == L"mo" && (Trim(argument.text) == L"," || Trim(argument.text) == L";"))
            {
                if (args.empty() || afterSeparator)
                    Fail(ParseErrorCode::InvalidMathML, L"Graph argument separator requires a preceding expression.");
                afterSeparator = true;
                continue;
            }
            args.push_back(ReadMathML(argument, localization));
            afterSeparator = false;
        }
        if (afterSeparator)
            Fail(ParseErrorCode::InvalidMathML, L"Graph argument separator requires a following expression.");
        return MakeCall(Trim(xml.children[0].text), std::move(args), false);
    }
    if (xml.name == L"math" || xml.name == L"mrow" || xml.name == L"mstyle")
    {
        if (xml.children.size() == 1)
            return ReadMathML(xml.children.front(), localization);
        std::wstring linear;
        for (const auto &child : xml.children)
            linear += MathMLToken(child, localization) + L" ";
        return LinearParser{linear, localization, false}.Run();
    }
    if (token || xml.name == L"mfenced")
    {
        const auto linear = MathMLToken(xml, localization);
        return LinearParser{linear, localization, false}.Run();
    }
    Fail(ParseErrorCode::UnknownMathMLElement, L"MathML element is unsupported.");
}

void CheckTree(const NodePtr &root)
{
    std::vector<std::pair<NodePtr, unsigned>> pending{{root, 1}};
    std::size_t count = 0;
    while (!pending.empty())
    {
        const auto [node, depth] = pending.back();
        pending.pop_back();
        if (!node || ++count > MAX_NODES || depth > MAX_DEPTH)
            Fail(ParseErrorCode::ComplexityLimit, L"Expression exceeds node or depth limits.");
        if (node->kind == NodeKind::Number && !std::isfinite(node->number))
            Fail(ParseErrorCode::OutOfDomain, L"Number is outside the finite real range.");
        if ((node->kind == NodeKind::Unary && node->children.size() != 1) ||
            (node->kind == NodeKind::Binary && node->children.size() != 2) ||
            ((node->kind == NodeKind::Number || node->kind == NodeKind::Variable) && !node->children.empty()))
            Fail(ParseErrorCode::IncorrectArguments, L"Expression node has an invalid number of children.");
        for (const auto &child : node->children)
            pending.emplace_back(child, depth + 1);
    }
}

double ToRadians(double value, Graphing::EvalTrigUnitMode mode)
{
    if (mode == Graphing::EvalTrigUnitMode::Radians)
        return value;
    if (mode == Graphing::EvalTrigUnitMode::Degrees)
        return value * PI / 180;
    if (mode == Graphing::EvalTrigUnitMode::Grads)
        return value * PI / 200;
    return UNDEFINED;
}

double FromRadians(double value, Graphing::EvalTrigUnitMode mode)
{
    if (mode == Graphing::EvalTrigUnitMode::Radians)
        return value;
    if (mode == Graphing::EvalTrigUnitMode::Degrees)
        return value * 180 / PI;
    if (mode == Graphing::EvalTrigUnitMode::Grads)
        return value * 200 / PI;
    return UNDEFINED;
}

double EvaluateCall(const std::wstring &name, const std::vector<double> &args, const EvalContext &context)
{
    const double x = args.front();
    const double angle = ToRadians(x, context.trig_unit);
    if (name == L"sin")
        return std::sin(angle);
    if (name == L"cos")
        return std::cos(angle);
    if (name == L"tan" || name == L"sec")
    {
        const double cosine = std::cos(angle);
        if (std::abs(cosine) <= 1e-14)
            return UNDEFINED;
        return name == L"sec" ? 1 / cosine : std::sin(angle) / cosine;
    }
    if (name == L"cot" || name == L"csc")
    {
        const double sine = std::sin(angle);
        if (std::abs(sine) <= 1e-14)
            return UNDEFINED;
        return name == L"csc" ? 1 / sine : std::cos(angle) / sine;
    }
    if (name == L"asin")
        return FromRadians(std::asin(x), context.trig_unit);
    if (name == L"acos")
        return FromRadians(std::acos(x), context.trig_unit);
    if (name == L"atan")
        return FromRadians(std::atan(x), context.trig_unit);
    if (name == L"acot")
        return FromRadians(std::atan2(1, x), context.trig_unit);
    if (name == L"asec")
        return std::abs(x) >= 1 ? FromRadians(std::acos(1 / x), context.trig_unit) : UNDEFINED;
    if (name == L"acsc")
        return std::abs(x) >= 1 ? FromRadians(std::asin(1 / x), context.trig_unit) : UNDEFINED;
    if (name == L"sinh")
        return std::sinh(x);
    if (name == L"cosh")
        return std::cosh(x);
    if (name == L"tanh")
        return std::tanh(x);
    if (name == L"coth")
        return x == 0 ? UNDEFINED : 1 / std::tanh(x);
    if (name == L"sech")
        return 1 / std::cosh(x);
    if (name == L"csch")
        return x == 0 ? UNDEFINED : 1 / std::sinh(x);
    if (name == L"asinh")
        return std::asinh(x);
    if (name == L"acosh")
        return std::acosh(x);
    if (name == L"atanh")
        return std::abs(x) < 1 ? std::atanh(x) : UNDEFINED;
    if (name == L"acoth")
        return std::abs(x) > 1 ? std::atanh(1 / x) : UNDEFINED;
    if (name == L"asech")
        return x > 0 && x <= 1 ? std::acosh(1 / x) : UNDEFINED;
    if (name == L"acsch")
        return x == 0 ? UNDEFINED : std::asinh(1 / x);
    if (name == L"sqrt")
        return std::sqrt(x);
    if (name == L"cbrt")
        return std::cbrt(x);
    if (name == L"root")
    {
        const double index = args[1];
        if (index == 0 || (x == 0 && index < 0))
            return UNDEFINED;
        if (x < 0)
        {
            if (std::trunc(index) != index || std::fmod(std::abs(index), 2) != 1)
                return UNDEFINED;
            return -std::pow(-x, 1 / index);
        }
        return std::pow(x, 1 / index);
    }
    if (name == L"abs")
        return std::abs(x);
    if (name == L"ln")
        return x > 0 ? std::log(x) : UNDEFINED;
    if (name == L"log10")
        return x > 0 ? std::log10(x) : UNDEFINED;
    if (name == L"log")
    {
        if (x <= 0)
            return UNDEFINED;
        if (args.size() == 1)
            return std::log10(x);
        return args[1] > 0 && args[1] != 1 ? std::log(x) / std::log(args[1]) : UNDEFINED;
    }
    if (name == L"exp")
        return std::exp(x);
    if (name == L"floor")
        return std::floor(x);
    if (name == L"ceil")
        return std::ceil(x);
    if (name == L"sign")
        return (x > 0) - (x < 0);
    if (name == L"min")
        return *std::min_element(args.begin(), args.end());
    if (name == L"max")
        return *std::max_element(args.begin(), args.end());
    return UNDEFINED;
}

double EvaluateNode(const NodePtr &node, const EvalContext &context, unsigned depth, std::size_t &budget)
{
    if (!node || depth > MAX_DEPTH || budget == 0)
        return UNDEFINED;
    --budget;
    if (node->kind == NodeKind::Number)
        return std::isfinite(node->number) ? node->number : UNDEFINED;
    if (node->kind == NodeKind::Variable)
    {
        const auto found = context.variables.find(node->name);
        return found != context.variables.end() && std::isfinite(found->second) ? found->second : UNDEFINED;
    }
    if (node->kind == NodeKind::List)
        return UNDEFINED;
    std::vector<double> args;
    for (const auto &child : node->children)
    {
        if (!child)
            return UNDEFINED;
        if (node->kind == NodeKind::Call && (node->name == L"min" || node->name == L"max") && child->kind == NodeKind::List)
        {
            for (const auto &item : child->children)
                args.push_back(EvaluateNode(item, context, depth + 2, budget));
        }
        else
            args.push_back(EvaluateNode(child, context, depth + 1, budget));
    }
    if (args.empty() || std::any_of(args.begin(), args.end(), [](double value) { return !std::isfinite(value); }))
        return UNDEFINED;
    double result = UNDEFINED;
    if (node->kind == NodeKind::Unary && args.size() == 1)
    {
        if (node->name == L"-")
            result = -args[0];
        if (node->name == L"+")
            result = args[0];
    }
    else if (node->kind == NodeKind::Binary && args.size() == 2)
    {
        const double left = args[0];
        const double right = args[1];
        if (node->name == L"+")
            result = left + right;
        else if (node->name == L"-")
            result = left - right;
        else if (node->name == L"*")
            result = left * right;
        else if (node->name == L"/")
            result = right == 0 ? UNDEFINED : left / right;
        else if (node->name == L"^")
            result = left == 0 && right <= 0 ? UNDEFINED : std::pow(left, right);
        else if (node->name == L"%")
            result = right == 0 || std::trunc(left) != left || std::trunc(right) != right ? UNDEFINED : std::fmod(left, right);
        else if (node->name == L"=")
            result = left == right;
        else if (node->name == L"!=")
            result = left != right;
        else if (node->name == L"<")
            result = left < right;
        else if (node->name == L"<=")
            result = left <= right;
        else if (node->name == L">")
            result = left > right;
        else if (node->name == L">=")
            result = left >= right;
    }
    else if (node->kind == NodeKind::Call)
    {
        const bool valid = node->name == L"min" || node->name == L"max" ? !args.empty()
                           : node->name == L"root"                      ? args.size() == 2
                           : node->name == L"log"                       ? args.size() <= 2
                                                                        : args.size() == 1;
        if (valid)
            result = EvaluateCall(node->name, args, context);
    }
    return std::isfinite(result) ? result : UNDEFINED;
}

std::wstring EscapeXml(const std::wstring &text)
{
    std::wstring result;
    for (const wchar_t value : text)
    {
        if (value == L'&')
            result += L"&amp;";
        else if (value == L'<')
            result += L"&lt;";
        else if (value == L'>')
            result += L"&gt;";
        else if (value == L'"')
            result += L"&quot;";
        else if (value == L'\'')
            result += L"&apos;";
        else
            result += value;
    }
    return result;
}

std::wstring ToMathML(const NodePtr &node, Graphing::LocalizationType localization, const std::wstring &prefix)
{
    const auto tag = [&](const wchar_t *name, const std::wstring &value, const std::wstring &attributes = L"") {
        return L"<" + prefix + name + attributes + L">" + value + L"</" + prefix + name + L">";
    };
    const auto child = [&](std::size_t index) { return ToMathML(node->children.at(index), localization, prefix); };
    const auto fence = [&](const std::wstring &value) { return tag(L"mfenced", value, L" separators=\"\""); };
    if (node->kind == NodeKind::Number)
        return tag(L"mn", FormatNumber(node->number, localization));
    if (node->kind == NodeKind::Variable)
        return tag(L"mi", EscapeXml(node->name));
    if (node->kind == NodeKind::Unary)
        return tag(L"mrow", tag(L"mo", EscapeXml(node->name)) + fence(child(0)));
    if (node->kind == NodeKind::Binary)
    {
        if (node->name == L"/")
            return tag(L"mfrac", child(0) + child(1));
        if (node->name == L"^")
            return tag(L"msup", child(0) + child(1));
        return tag(L"mrow", fence(child(0)) + tag(L"mo", EscapeXml(node->name)) + fence(child(1)));
    }
    std::wstring args;
    for (std::size_t index = 0; index < node->children.size(); ++index)
        args += child(index);
    if (node->kind == NodeKind::List)
        return tag(L"mfenced", args, L" open=\"{\" close=\"}\" separators=\",\"");
    if (node->name == L"sqrt")
        return tag(L"msqrt", args);
    if (node->name == L"root")
        return tag(L"mroot", args);
    return tag(L"mrow", tag(L"mi", EscapeXml(node->name)) +
                            tag(L"mfenced", args, IsGraphFunction(node->name) ? L" separators=\"\"" : L" separators=\",\""));
}

std::wstring FormulaName(const std::wstring &operation)
{
    if (operation == L"+")
        return L"Sum";
    if (operation == L"-")
        return L"Subtract";
    if (operation == L"*")
        return L"Product";
    if (operation == L"/")
        return L"Divide";
    if (operation == L"^")
        return L"Power";
    if (operation == L"=")
        return L"Equal";
    if (operation == L"!=")
        return L"NotEqual";
    if (operation == L"<")
        return L"LessThan";
    if (operation == L"<=")
        return L"LessOrEqual";
    if (operation == L">")
        return L"GreaterThan";
    if (operation == L">=")
        return L"GreaterOrEqual";
    if (operation == L"%")
        return L"Modulo";
    return operation;
}

std::wstring ToFormula(const NodePtr &node, Graphing::LocalizationType localization, const std::map<std::wstring, std::size_t> *ids)
{
    if (node->kind == NodeKind::Number)
        return FormatNumber(node->number, localization);
    if (node->kind == NodeKind::Variable)
        return ids ? L"Var(" + std::to_wstring(ids->at(node->name)) + L")" : node->name;
    std::wstring result = node->kind == NodeKind::List     ? L"List"
                          : node->kind == NodeKind::Unary  ? node->name == L"-" ? L"Negate" : L"Aggregate"
                          : node->kind == NodeKind::Binary ? FormulaName(node->name)
                                                           : node->name;
    result += L"[";
    const wchar_t separator = localization == Graphing::LocalizationType::DecimalPointAndListComma ? L',' : L';';
    for (std::size_t index = 0; index < node->children.size(); ++index)
    {
        if (index != 0)
            result += separator;
        result += ToFormula(node->children[index], localization, ids);
    }
    return result + L"]";
}

std::wstring ToLatex(const NodePtr &node)
{
    const auto child = [&](std::size_t index) { return ToLatex(node->children.at(index)); };
    if (node->kind == NodeKind::Number)
        return FormatNumber(node->number, Graphing::LocalizationType::DecimalPointAndListComma);
    if (node->kind == NodeKind::Variable)
    {
        std::wstring result;
        for (const wchar_t character : node->name)
            result += character == L'_' ? L"\\_" : std::wstring(1, character);
        return result;
    }
    if (node->kind == NodeKind::Unary)
        return L"\\left(" + node->name + child(0) + L"\\right)";
    if (node->kind == NodeKind::Binary)
    {
        if (node->name == L"/")
            return L"\\frac{" + child(0) + L"}{" + child(1) + L"}";
        if (node->name == L"^")
            return L"{" + child(0) + L"}^{" + child(1) + L"}";
        const auto operation = node->name == L"*"    ? L"\\cdot "
                               : node->name == L"<=" ? L"\\le "
                               : node->name == L">=" ? L"\\ge "
                               : node->name == L"!=" ? L"\\ne "
                                                     : node->name;
        return L"\\left(" + child(0) + operation + child(1) + L"\\right)";
    }
    if (node->kind == NodeKind::Call && node->name == L"sqrt")
        return L"\\sqrt{" + child(0) + L"}";
    if (node->kind == NodeKind::Call && node->name == L"root")
        return L"\\sqrt[" + child(1) + L"]{" + child(0) + L"}";
    std::wstring result = node->kind == NodeKind::List ? L"\\left\\{" : L"\\operatorname{" + node->name + L"}\\left(";
    for (std::size_t index = 0; index < node->children.size(); ++index)
    {
        if (index != 0)
            result += L",";
        result += child(index);
    }
    return result + (node->kind == NodeKind::List ? L"\\right\\}" : L"\\right)");
}
} // namespace

ParseResult Parse(const std::wstring &input, Graphing::FormatType format, Graphing::LocalizationType localization)
{
    try
    {
        if (input.size() > MAX_EXPRESSION_INPUT)
            Fail(ParseErrorCode::ComplexityLimit, L"Input exceeds 65,536 characters.");
        if (localization == Graphing::LocalizationType::Unknown)
            localization = Graphing::LocalizationType::DecimalPointAndListComma;
        if (localization != Graphing::LocalizationType::DecimalPointAndListComma &&
            localization != Graphing::LocalizationType::DecimalPointAndListSemicolon &&
            localization != Graphing::LocalizationType::DecimalCommaAndListSemicolon)
            Fail(ParseErrorCode::Unsupported, L"Localization setting is unsupported.");
        NodePtr root;
        switch (format)
        {
        case Graphing::FormatType::Linear:
        case Graphing::FormatType::LinearInput:
            root = LinearParser{input, localization, false}.Run();
            break;
        case Graphing::FormatType::Formula:
        case Graphing::FormatType::FormulaWithoutAggregate:
            root = LinearParser{input, localization, true}.Run();
            break;
        case Graphing::FormatType::MathML:
            root = ReadMathML(XmlParser{input}.Run(), localization);
            break;
        default:
            Fail(ParseErrorCode::Unsupported, L"Input format is unsupported; binary and base64 encodings are not implemented.");
        }
        CheckTree(root);
        return {root, 0, 0, {}};
    }
    catch (const ParseFailure &error)
    {
        return {{}, static_cast<int>(error.code), error.type, error.message};
    }
}

double Evaluate(const NodePtr &node, const EvalContext &context)
{
    std::size_t budget = MAX_NODES;
    return EvaluateNode(node, context, 0, budget);
}

std::set<std::wstring> FindVariables(const NodePtr &node)
{
    std::set<std::wstring> result;
    if (!node)
        return result;
    try
    {
        CheckTree(node);
    }
    catch (const ParseFailure &)
    {
        throw std::invalid_argument("Invalid expression tree.");
    }
    std::vector<NodePtr> pending{node};
    while (!pending.empty())
    {
        const auto current = pending.back();
        pending.pop_back();
        if (current->kind == NodeKind::Variable)
            result.insert(current->name);
        pending.insert(pending.end(), current->children.begin(), current->children.end());
    }
    return result;
}

std::wstring Serialize(const NodePtr &node, Graphing::FormatType format, Graphing::LocalizationType localization,
                       const std::wstring &mathml_prefix)
{
    if (!node)
        throw std::invalid_argument("Cannot serialize an empty expression.");
    try
    {
        CheckTree(node);
    }
    catch (const ParseFailure &)
    {
        throw std::invalid_argument("Expression exceeds structural limits.");
    }
    if (localization == Graphing::LocalizationType::Unknown)
        localization = Graphing::LocalizationType::DecimalPointAndListComma;
    if (localization != Graphing::LocalizationType::DecimalPointAndListComma &&
        localization != Graphing::LocalizationType::DecimalPointAndListSemicolon &&
        localization != Graphing::LocalizationType::DecimalCommaAndListSemicolon)
        throw std::invalid_argument("Localization setting is unsupported.");
    switch (format)
    {
    case Graphing::FormatType::Linear:
    case Graphing::FormatType::LinearInput:
        return ToLinear(node, localization);
    case Graphing::FormatType::Formula:
    case Graphing::FormatType::FormulaWithoutAggregate:
        return ToFormula(node, localization, nullptr);
    case Graphing::FormatType::InvariantFormula: {
        std::map<std::wstring, std::size_t> ids;
        for (const auto &name : FindVariables(node))
            ids.emplace(name, ids.size());
        return ToFormula(node, Graphing::LocalizationType::DecimalPointAndListComma, &ids);
    }
    case Graphing::FormatType::MathML:
    case Graphing::FormatType::MathMLNoWrapper: {
        std::wstring namespaceName = mathml_prefix;
        if (!namespaceName.empty() && namespaceName.back() == L':')
            namespaceName.pop_back();
        const bool validName = !namespaceName.empty() && IsLetter(namespaceName.front()) &&
                               std::all_of(namespaceName.begin(), namespaceName.end(), [](wchar_t character) {
                                   return IsLetter(character) || IsDigit(character) || character == L'_';
                               });
        if (!mathml_prefix.empty() && !validName)
            throw std::invalid_argument("MathML prefix must be an XML namespace identifier.");
        const auto prefix = namespaceName.empty() ? L"" : namespaceName + L":";
        const auto content = ToMathML(node, localization, prefix);
        if (format == Graphing::FormatType::MathMLNoWrapper)
            return content;
        const auto declaration = namespaceName.empty() ? L"xmlns" : L"xmlns:" + namespaceName;
        return L"<" + prefix + L"math " + declaration + L"=\"http://www.w3.org/1998/Math/MathML\">" + content + L"</" + prefix + L"math>";
    }
    case Graphing::FormatType::Latex:
        return ToLatex(node);
    default:
        throw std::invalid_argument("Serialization format is unsupported; binary and base64 are not implemented.");
    }
}
} // namespace graphing::detail
