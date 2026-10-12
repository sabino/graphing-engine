#pragma once

#include <cstdint>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
using HRESULT = std::int32_t;
using BYTE = std::uint8_t;
inline constexpr HRESULT S_OK = 0;
inline constexpr HRESULT S_FALSE = 1;
inline constexpr HRESULT E_FAIL = static_cast<HRESULT>(0x80004005u);
inline constexpr HRESULT E_NOTIMPL = static_cast<HRESULT>(0x80004001u);
inline constexpr HRESULT E_INVALIDARG = static_cast<HRESULT>(0x80070057u);
inline constexpr HRESULT E_POINTER = static_cast<HRESULT>(0x80004003u);
inline constexpr HRESULT E_ABORT = static_cast<HRESULT>(0x80004004u);
#endif
