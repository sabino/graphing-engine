#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace graphing::detail
{
inline std::wstring DecodeUtf8(std::string_view input)
{
    std::wstring result;
    for (std::size_t index = 0; index < input.size();)
    {
        const auto first = static_cast<unsigned char>(input[index++]);
        std::uint32_t value = first;
        unsigned continuation = 0;
        std::uint32_t minimum = 0;
        if (first < 0x80)
        {
        }
        else if (first >= 0xc2 && first <= 0xdf)
        {
            value = first & 0x1f;
            continuation = 1;
            minimum = 0x80;
        }
        else if (first >= 0xe0 && first <= 0xef)
        {
            value = first & 0x0f;
            continuation = 2;
            minimum = 0x800;
        }
        else if (first >= 0xf0 && first <= 0xf4)
        {
            value = first & 0x07;
            continuation = 3;
            minimum = 0x10000;
        }
        else
            throw std::invalid_argument("Invalid UTF-8 input");
        if (index + continuation > input.size())
            throw std::invalid_argument("Incomplete UTF-8 input");
        for (unsigned count = 0; count < continuation; ++count)
        {
            const auto byte = static_cast<unsigned char>(input[index++]);
            if ((byte & 0xc0) != 0x80)
                throw std::invalid_argument("Invalid UTF-8 continuation");
            value = (value << 6) | (byte & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            throw std::invalid_argument("Invalid Unicode scalar");
        if constexpr (sizeof(wchar_t) == 2)
        {
            if (value > 0xffff)
            {
                value -= 0x10000;
                result.push_back(static_cast<wchar_t>(0xd800 + (value >> 10)));
                result.push_back(static_cast<wchar_t>(0xdc00 + (value & 0x3ff)));
                continue;
            }
        }
        result.push_back(static_cast<wchar_t>(value));
    }
    return result;
}

inline std::string EncodeUtf8(std::wstring_view input)
{
    std::string result;
    for (std::size_t index = 0; index < input.size(); ++index)
    {
        std::uint32_t value = static_cast<std::uint32_t>(input[index]);
        if constexpr (sizeof(wchar_t) == 2)
        {
            if (value >= 0xd800 && value <= 0xdbff)
            {
                if (++index >= input.size())
                    throw std::invalid_argument("Incomplete UTF-16 input");
                const auto low = static_cast<std::uint32_t>(input[index]);
                if (low < 0xdc00 || low > 0xdfff)
                    throw std::invalid_argument("Invalid UTF-16 continuation");
                value = 0x10000 + ((value - 0xd800) << 10) + low - 0xdc00;
            }
        }
        if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            throw std::invalid_argument("Invalid Unicode scalar");
        if (value < 0x80)
            result.push_back(static_cast<char>(value));
        else if (value < 0x800)
        {
            result.push_back(static_cast<char>(0xc0 | (value >> 6)));
            result.push_back(static_cast<char>(0x80 | (value & 0x3f)));
        }
        else if (value < 0x10000)
        {
            result.push_back(static_cast<char>(0xe0 | (value >> 12)));
            result.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (value & 0x3f)));
        }
        else
        {
            result.push_back(static_cast<char>(0xf0 | (value >> 18)));
            result.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (value & 0x3f)));
        }
    }
    return result;
}
} // namespace graphing::detail
