#include "graphing/Scene.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <iomanip>
#include <limits>
#include <sstream>

namespace graphing
{
namespace
{
constexpr std::size_t MAX_PIXELS = 4 * 1024 * 1024;
bool ValidSize(const Scene &scene)
{
    return scene.width && scene.height && scene.width <= 4096 && scene.height <= 4096 &&
           static_cast<std::size_t>(scene.width) * scene.height <= MAX_PIXELS;
}
bool Finite(Point point)
{
    return std::isfinite(point.x) && std::isfinite(point.y);
}
void Blend(std::vector<std::uint8_t> &pixels, std::size_t index, Graphing::Color color)
{
    const double sourceAlpha = color.A / 255.0;
    const double destinationAlpha = pixels[index + 3] / 255.0;
    const double alpha = sourceAlpha + destinationAlpha * (1 - sourceAlpha);
    const unsigned channels[] = {color.R, color.G, color.B};
    for (unsigned channel = 0; channel < 3; ++channel)
    {
        pixels[index + channel] =
            alpha == 0 ? 0
                       : static_cast<std::uint8_t>(std::lround(
                             (channels[channel] * sourceAlpha + pixels[index + channel] * destinationAlpha * (1 - sourceAlpha)) / alpha));
    }
    pixels[index + 3] = static_cast<std::uint8_t>(std::lround(alpha * 255));
}
bool DrawDash(Graphing::Renderer::LineStyle style, double distance)
{
    using Graphing::Renderer::LineStyle;
    switch (style)
    {
    case LineStyle::Dot:
        return std::fmod(distance, 5) < 1.5;
    case LineStyle::Dash:
        return std::fmod(distance, 12) < 8;
    case LineStyle::DashDot: {
        const auto phase = std::fmod(distance, 17);
        return phase < 8 || (phase >= 12 && phase < 13.5);
    }
    case LineStyle::DashDotDot: {
        const auto phase = std::fmod(distance, 22);
        return phase < 8 || (phase >= 12 && phase < 13.5) || (phase >= 17 && phase < 18.5);
    }
    default:
        return true;
    }
}
std::wstring EscapeXml(const std::wstring &value)
{
    std::wstring result;
    for (wchar_t character : value)
    {
        switch (character)
        {
        case L'&':
            result += L"&amp;";
            break;
        case L'<':
            result += L"&lt;";
            break;
        case L'>':
            result += L"&gt;";
            break;
        case L'\"':
            result += L"&quot;";
            break;
        default:
            result += character;
        }
    }
    return result;
}
std::wstring ColorText(Graphing::Color color)
{
    std::wostringstream text;
    text << L"rgb(" << unsigned(color.R) << L',' << unsigned(color.G) << L',' << unsigned(color.B) << L')';
    return text.str();
}
const wchar_t *DashText(Graphing::Renderer::LineStyle style)
{
    using Graphing::Renderer::LineStyle;
    switch (style)
    {
    case LineStyle::Dot:
        return L"1.5 3.5";
    case LineStyle::Dash:
        return L"8 4";
    case LineStyle::DashDot:
        return L"8 4 1.5 3.5";
    case LineStyle::DashDotDot:
        return L"8 4 1.5 3.5 1.5 3.5";
    default:
        return L"";
    }
}
std::vector<unsigned> Glyph(wchar_t character)
{
    // Compact geometry for bitmap tick labels. SVG retains the exact Unicode label text.
    static constexpr unsigned letters[][5] = {{126, 17, 17, 17, 126}, {127, 73, 73, 73, 54}, {62, 65, 65, 65, 34},  {127, 65, 65, 34, 28},
                                              {127, 73, 73, 73, 65},  {127, 9, 9, 9, 1},     {62, 65, 73, 73, 122}, {127, 8, 8, 8, 127},
                                              {0, 65, 127, 65, 0},    {32, 64, 65, 63, 1},   {127, 8, 20, 34, 65},  {127, 64, 64, 64, 64},
                                              {127, 2, 12, 2, 127},   {127, 4, 8, 16, 127},  {62, 65, 65, 65, 62},  {127, 9, 9, 9, 6},
                                              {62, 65, 81, 33, 94},   {127, 9, 25, 41, 70},  {38, 73, 73, 73, 50},  {1, 1, 127, 1, 1},
                                              {63, 64, 64, 64, 63},   {31, 32, 64, 32, 31},  {63, 64, 56, 64, 63},  {99, 20, 8, 20, 99},
                                              {7, 8, 112, 8, 7},      {97, 81, 73, 69, 67}};
    static constexpr unsigned digits[][5] = {{62, 81, 73, 69, 62},  {0, 66, 127, 64, 0},  {66, 97, 81, 73, 70}, {33, 65, 69, 75, 49},
                                             {24, 20, 18, 127, 16}, {39, 69, 69, 69, 57}, {60, 74, 73, 73, 48}, {1, 113, 9, 5, 3},
                                             {54, 73, 73, 73, 54},  {6, 73, 73, 41, 30}};
    character = std::towupper(character);
    if (character >= L'A' && character <= L'Z')
        return std::vector<unsigned>(letters[character - L'A'], letters[character - L'A'] + 5);
    if (character >= L'0' && character <= L'9')
        return std::vector<unsigned>(digits[character - L'0'], digits[character - L'0'] + 5);
    if (character == L' ')
        return {0, 0, 0, 0, 0};
    if (character == L'.')
        return {0, 96, 96, 0, 0};
    if (character == L'-')
        return {8, 8, 8, 8, 8};
    if (character == L'+')
        return {8, 8, 62, 8, 8};
    return {127, 65, 65, 65, 127};
}
void Append32(std::vector<BYTE> &bytes, std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<BYTE>(value >> shift));
}
void Append16(std::vector<BYTE> &bytes, unsigned value)
{
    bytes.push_back(static_cast<BYTE>(value));
    bytes.push_back(static_cast<BYTE>(value >> 8));
}
void AppendBigEndian32(std::vector<BYTE> &bytes, std::uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        bytes.push_back(static_cast<BYTE>(value >> shift));
}
constexpr std::array<std::uint32_t, 256> CreateCrcTable()
{
    std::array<std::uint32_t, 256> table{};
    for (unsigned index = 0; index < table.size(); ++index)
    {
        std::uint32_t remainder = index;
        for (unsigned bit = 0; bit < 8; ++bit)
            remainder = (remainder >> 1) ^ ((remainder & 1) ? 0xedb88320u : 0);
        table[index] = remainder;
    }
    return table;
}
void AppendPngChunk(std::vector<BYTE> &output, const char *type, const std::vector<BYTE> &data)
{
    constexpr auto crcTable = CreateCrcTable();
    AppendBigEndian32(output, static_cast<std::uint32_t>(data.size()));
    std::uint32_t crc = 0xffffffffu;
    const auto append = [&](BYTE byte) {
        output.push_back(byte);
        crc = crcTable[(crc ^ byte) & 255] ^ (crc >> 8);
    };
    for (unsigned index = 0; index < 4; ++index)
        append(static_cast<BYTE>(type[index]));
    for (auto byte : data)
        append(byte);
    AppendBigEndian32(output, crc ^ 0xffffffffu);
}
} // namespace

std::vector<std::uint8_t> Rasterize(const Scene &scene)
{
    if (!ValidSize(scene) || scene.strokes.size() > 100000 || scene.fills.size() > 100000 || scene.labels.size() > 512)
        return {};
    const auto count = static_cast<std::size_t>(scene.width) * scene.height;
    std::vector<std::uint8_t> pixels(count * 4);
    for (std::size_t index = 0; index < count; ++index)
    {
        pixels[index * 4] = scene.background.R;
        pixels[index * 4 + 1] = scene.background.G;
        pixels[index * 4 + 2] = scene.background.B;
        pixels[index * 4 + 3] = scene.background.A;
    }
    std::size_t work = 0;
    for (const auto &fill : scene.fills)
    {
        if (fill.polygon.size() < 3 || fill.polygon.size() > 1024 || !std::all_of(fill.polygon.begin(), fill.polygon.end(), Finite))
            continue;
        double minY = scene.height, maxY = 0;
        for (auto point : fill.polygon)
        {
            minY = std::min(minY, point.y);
            maxY = std::max(maxY, point.y);
        }
        const int firstRow = static_cast<int>(std::clamp(std::floor(minY), 0.0, double(scene.height)));
        const int lastRow = static_cast<int>(std::clamp(std::ceil(maxY), 0.0, double(scene.height)));
        for (int y = firstRow; y < lastRow; ++y)
        {
            std::vector<double> crossings;
            for (std::size_t index = 0; index < fill.polygon.size(); ++index)
            {
                if (++work > 50000000)
                    return {};
                const auto a = fill.polygon[index], b = fill.polygon[(index + 1) % fill.polygon.size()];
                const double scanY = y + 0.5;
                if ((a.y <= scanY && b.y > scanY) || (b.y <= scanY && a.y > scanY))
                    crossings.push_back(a.x + (scanY - a.y) * (b.x - a.x) / (b.y - a.y));
            }
            std::sort(crossings.begin(), crossings.end());
            for (std::size_t index = 0; index + 1 < crossings.size(); index += 2)
            {
                const int left = static_cast<int>(std::clamp(std::ceil(crossings[index] - 0.5), 0.0, double(scene.width)));
                const int right = static_cast<int>(std::clamp(std::ceil(crossings[index + 1] - 0.5), 0.0, double(scene.width)));
                for (int x = left; x < right; ++x)
                {
                    if (++work > 50000000)
                        return {};
                    Blend(pixels, (static_cast<std::size_t>(y) * scene.width + x) * 4, fill.color);
                }
            }
        }
    }
    std::vector<unsigned> painted(count, 0);
    unsigned generation = 0;
    for (const auto &stroke : scene.strokes)
    {
        if (!std::isfinite(stroke.width) || stroke.width <= 0)
            continue;
        if (++generation == 0)
        {
            std::fill(painted.begin(), painted.end(), 0);
            ++generation;
        }
        const double radius = std::clamp(double(stroke.width) / 2, 0.5, 512.0);
        double phase = 0;
        for (std::size_t index = 1; index < stroke.points.size(); ++index)
        {
            const auto a = stroke.points[index - 1], b = stroke.points[index];
            if (!Finite(a) || !Finite(b))
                continue;
            const double length = std::hypot(b.x - a.x, b.y - a.y);
            if (length > 8192)
                continue;
            const double dx = b.x - a.x, dy = b.y - a.y;
            const bool alongX = std::abs(dx) >= std::abs(dy);
            const double minimum = alongX ? std::min(a.x, b.x) : std::min(a.y, b.y);
            const double maximum = alongX ? std::max(a.x, b.x) : std::max(a.y, b.y);
            const unsigned extent = alongX ? scene.width : scene.height;
            const int start = static_cast<int>(std::clamp(std::floor(minimum - radius), 0.0, double(extent)));
            const int end = static_cast<int>(std::clamp(std::ceil(maximum + radius), 0.0, double(extent)));
            const double majorDelta = alongX ? dx : dy;
            const double minorDelta = alongX ? dy : dx;
            const double majorOrigin = alongX ? a.x : a.y;
            const double minorOrigin = alongX ? a.y : a.x;
            const double margin = majorDelta == 0 ? radius : radius * length / std::abs(majorDelta);
            for (int major = start; major < end; ++major)
            {
                const double center = majorDelta == 0 ? minorOrigin : minorOrigin + (major + 0.5 - majorOrigin) * minorDelta / majorDelta;
                const unsigned minorExtent = alongX ? scene.height : scene.width;
                const int first = static_cast<int>(std::clamp(std::floor(center - margin), 0.0, double(minorExtent)));
                const int last = static_cast<int>(std::clamp(std::ceil(center + margin), 0.0, double(minorExtent)));
                for (int minor = first; minor < last; ++minor)
                {
                    if (++work > 50000000)
                        return {};
                    const unsigned x = alongX ? major : minor, y = alongX ? minor : major;
                    const double fraction =
                        length == 0 ? 0 : std::clamp(((x + 0.5 - a.x) * dx + (y + 0.5 - a.y) * dy) / (length * length), 0.0, 1.0);
                    const auto pixel = static_cast<std::size_t>(y) * scene.width + x;
                    if (painted[pixel] == generation || !DrawDash(stroke.style, phase + fraction * length) ||
                        std::hypot(x + 0.5 - a.x - fraction * dx, y + 0.5 - a.y - fraction * dy) > radius)
                        continue;
                    painted[pixel] = generation;
                    Blend(pixels, pixel * 4, stroke.color);
                }
            }
            phase += length;
        }
    }
    for (const auto &label : scene.labels)
    {
        if (!Finite(label.position))
            continue;
        double cursor = label.position.x;
        for (std::size_t index = 0; index < std::min<std::size_t>(label.text.size(), 128); ++index)
        {
            const auto glyph = Glyph(label.text[index]);
            for (unsigned column = 0; column < 5; ++column)
                for (unsigned row = 0; row < 7; ++row)
                {
                    const double x = cursor + column, y = label.position.y - 7 + row;
                    if (!(glyph[column] & (1u << row)) || x < 0 || x >= scene.width || y < 0 || y >= scene.height)
                        continue;
                    Blend(pixels, (static_cast<std::size_t>(y) * scene.width + static_cast<std::size_t>(x)) * 4, label.color);
                }
            cursor += 6;
        }
    }
    return pixels;
}

std::vector<BYTE> EncodeBitmap(const Scene &scene)
{
    const auto pixels = Rasterize(scene);
    if (pixels.empty())
        return {};
    // BITMAPV4HEADER preserves explicit RGBA masks and alpha across standard BMP readers.
    constexpr unsigned offset = 14 + 108;
    std::vector<BYTE> bytes;
    bytes.reserve(offset + pixels.size());
    bytes.push_back('B');
    bytes.push_back('M');
    Append32(bytes, static_cast<std::uint32_t>(offset + pixels.size()));
    Append32(bytes, 0);
    Append32(bytes, offset);
    Append32(bytes, 108);
    Append32(bytes, scene.width);
    Append32(bytes, static_cast<std::uint32_t>(-static_cast<std::int32_t>(scene.height)));
    Append16(bytes, 1);
    Append16(bytes, 32);
    Append32(bytes, 3);
    Append32(bytes, static_cast<std::uint32_t>(pixels.size()));
    Append32(bytes, 3780);
    Append32(bytes, 3780);
    Append32(bytes, 0);
    Append32(bytes, 0);
    Append32(bytes, 0x00ff0000);
    Append32(bytes, 0x0000ff00);
    Append32(bytes, 0x000000ff);
    Append32(bytes, 0xff000000);
    Append32(bytes, 0x73524742); // LCS_sRGB.
    while (bytes.size() < offset)
        bytes.push_back(0);
    for (std::size_t index = 0; index < pixels.size(); index += 4)
    {
        bytes.push_back(pixels[index + 2]);
        bytes.push_back(pixels[index + 1]);
        bytes.push_back(pixels[index]);
        bytes.push_back(pixels[index + 3]);
    }
    return bytes;
}

std::vector<BYTE> EncodePng(const Scene &scene)
{
    const auto pixels = Rasterize(scene);
    if (pixels.empty())
        return {};
    const auto rowBytes = static_cast<std::size_t>(scene.width) * 4;
    const auto scanlineBytes = rowBytes + 1;
    const auto rawSize = scanlineBytes * scene.height;
    std::vector<BYTE> compressed;
    compressed.reserve(rawSize + (rawSize / 65535 + 1) * 5 + 6);
    compressed.push_back(0x78);
    compressed.push_back(0x01);
    std::uint32_t adlerLow = 1, adlerHigh = 0;
    // Stored DEFLATE blocks need no compression dependency. Each row uses PNG filter zero.
    for (std::size_t offset = 0; offset < rawSize;)
    {
        const auto length = static_cast<unsigned>(std::min<std::size_t>(65535, rawSize - offset));
        compressed.push_back(offset + length == rawSize ? 1 : 0);
        Append16(compressed, length);
        Append16(compressed, length ^ 0xffffu);
        for (unsigned index = 0; index < length; ++index)
        {
            const auto position = offset + index;
            const auto column = position % scanlineBytes;
            const BYTE byte = column == 0 ? 0 : pixels[(position / scanlineBytes) * rowBytes + column - 1];
            compressed.push_back(byte);
            adlerLow += byte;
            if (adlerLow >= 65521)
                adlerLow -= 65521;
            adlerHigh += adlerLow;
            if (adlerHigh >= 65521)
                adlerHigh -= 65521;
        }
        offset += length;
    }
    AppendBigEndian32(compressed, (adlerHigh << 16) | adlerLow);
    std::vector<BYTE> header;
    AppendBigEndian32(header, scene.width);
    AppendBigEndian32(header, scene.height);
    header.insert(header.end(), {8, 6, 0, 0, 0});
    std::vector<BYTE> output{137, 80, 78, 71, 13, 10, 26, 10};
    output.reserve(compressed.size() + 57);
    AppendPngChunk(output, "IHDR", header);
    AppendPngChunk(output, "IDAT", compressed);
    AppendPngChunk(output, "IEND", {});
    return output;
}

std::wstring SerializeSvg(const Scene &scene)
{
    if (!ValidSize(scene) || scene.strokes.size() > 100000 || scene.fills.size() > 100000 || scene.labels.size() > 512)
        return L"";
    std::size_t points = 0;
    for (const auto &stroke : scene.strokes)
        points += stroke.points.size();
    for (const auto &fill : scene.fills)
        points += fill.polygon.size();
    if (points > 1000000)
        return L"";
    std::wostringstream svg;
    svg << std::setprecision(12) << L"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << scene.width << L"\" height=\"" << scene.height
        << L"\" viewBox=\"0 0 " << scene.width << L' ' << scene.height << L"\">";
    svg << L"<rect width=\"100%\" height=\"100%\" fill=\"" << ColorText(scene.background) << L"\" fill-opacity=\""
        << scene.background.A / 255.0 << L"\"/>";
    for (const auto &fill : scene.fills)
    {
        if (fill.polygon.size() < 3 || !std::all_of(fill.polygon.begin(), fill.polygon.end(), Finite))
            continue;
        svg << L"<polygon points=\"";
        for (auto point : fill.polygon)
            svg << point.x << L',' << point.y << L' ';
        svg << L"\" fill=\"" << ColorText(fill.color) << L"\" fill-opacity=\"" << fill.color.A / 255.0 << L"\"/>";
    }
    for (const auto &stroke : scene.strokes)
    {
        if (stroke.points.size() < 2 || !std::isfinite(stroke.width) || !std::all_of(stroke.points.begin(), stroke.points.end(), Finite))
            continue;
        svg << L"<polyline fill=\"none\" points=\"";
        for (auto point : stroke.points)
            svg << point.x << L',' << point.y << L' ';
        svg << L"\" stroke=\"" << ColorText(stroke.color) << L"\" stroke-opacity=\"" << stroke.color.A / 255.0 << L"\" stroke-width=\""
            << stroke.width << L"\" stroke-dasharray=\"" << DashText(stroke.style) << L"\"/>";
    }
    for (const auto &label : scene.labels)
    {
        if (!Finite(label.position))
            continue;
        svg << L"<text x=\"" << label.position.x << L"\" y=\"" << label.position.y
            << L"\" font-family=\"sans-serif\" font-size=\"12\" fill=\"" << ColorText(label.color) << L"\" fill-opacity=\""
            << label.color.A / 255.0 << L"\">" << EscapeXml(label.text) << L"</text>";
    }
    svg << L"</svg>";
    return svg.str();
}
} // namespace graphing
