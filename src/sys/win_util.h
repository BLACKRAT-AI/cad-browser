// CAD Browser - small Win32 helpers shared by the sys/ layer.
// This header must not depend on CEF.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cad::sys {

// ---------- text ----------
std::string ToUtf8(std::wstring_view w);
std::wstring ToUtf16(std::string_view s);

// Trims ASCII whitespace and control characters from both ends.
std::string TrimAscii(std::string_view s);

// Human sizes. FormatBytes picks a unit; the others force one.
std::string FormatBytes(uint64_t bytes, int decimals = 1);
std::string FormatGB(uint64_t bytes, int decimals = 1);
std::string FormatMB(uint64_t bytes, int decimals = 0);

// Fixed-precision double to string, no iostreams and no locale surprises.
std::string FormatDouble(double v, int decimals);

// "0x10DE". Vendor and device ids are only recognisable in hex.
std::string FormatHex32(uint32_t value, int min_digits = 4);

bool IEqualsAscii(std::string_view a, std::string_view b);
bool ContainsNoCase(std::string_view haystack, std::string_view needle);

// ---------- registry ----------
std::optional<std::wstring> RegReadString(HKEY root,
                                          const wchar_t* subkey,
                                          const wchar_t* value);
std::optional<uint32_t> RegReadDword(HKEY root,
                                     const wchar_t* subkey,
                                     const wchar_t* value);
bool RegWriteString(HKEY root,
                    const wchar_t* subkey,
                    const wchar_t* value,
                    const std::wstring& data);
bool RegDeleteValueIfPresent(HKEY root,
                             const wchar_t* subkey,
                             const wchar_t* value);

// ---------- paths ----------
std::wstring ExecutablePathW();
std::string ExecutablePath();
// %LOCALAPPDATA%\CADBrowser, created if missing. Empty string on failure.
std::wstring UserDataDirW();

// ---------- misc ----------
// Formats a Win32 error code as "0x80070005: Access is denied".
std::string Win32ErrorString(DWORD err);

}  // namespace cad::sys
