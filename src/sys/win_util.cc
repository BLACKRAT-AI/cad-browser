#include "sys/win_util.h"

#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace cad::sys {

std::string ToUtf8(std::wstring_view w) {
  if (w.empty())
    return {};
  const int needed =
      ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                            nullptr, 0, nullptr, nullptr);
  if (needed <= 0)
    return {};
  std::string out(static_cast<size_t>(needed), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                        out.data(), needed, nullptr, nullptr);
  return out;
}

std::wstring ToUtf16(std::string_view s) {
  if (s.empty())
    return {};
  const int needed = ::MultiByteToWideChar(
      CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (needed <= 0)
    return {};
  std::wstring out(static_cast<size_t>(needed), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        out.data(), needed);
  return out;
}

std::string TrimAscii(std::string_view s) {
  auto is_trim = [](unsigned char c) { return c <= 0x20 || c == 0x7F; };
  size_t b = 0;
  size_t e = s.size();
  while (b < e && is_trim(static_cast<unsigned char>(s[b])))
    ++b;
  while (e > b && is_trim(static_cast<unsigned char>(s[e - 1])))
    --e;
  return std::string(s.substr(b, e - b));
}

std::string FormatDouble(double v, int decimals) {
  if (!std::isfinite(v))
    v = 0.0;
  char buf[64];
  ::_snprintf_s(buf, sizeof(buf), _TRUNCATE, "%.*f", decimals, v);
  return std::string(buf);
}

std::string FormatHex32(uint32_t value, int min_digits) {
  static const char kDigits[] = "0123456789ABCDEF";
  std::string body;
  do {
    body.insert(body.begin(), kDigits[value & 0x0F]);
    value >>= 4;
  } while (value != 0);
  while (static_cast<int>(body.size()) < min_digits)
    body.insert(body.begin(), '0');
  return "0x" + body;
}

std::string FormatBytes(uint64_t bytes, int decimals) {
  constexpr uint64_t kKB = 1024ull;
  constexpr uint64_t kMB = kKB * 1024ull;
  constexpr uint64_t kGB = kMB * 1024ull;
  constexpr uint64_t kTB = kGB * 1024ull;
  if (bytes >= kTB)
    return FormatDouble(static_cast<double>(bytes) / kTB, decimals) + " TB";
  if (bytes >= kGB)
    return FormatDouble(static_cast<double>(bytes) / kGB, decimals) + " GB";
  if (bytes >= kMB)
    return FormatDouble(static_cast<double>(bytes) / kMB, decimals) + " MB";
  if (bytes >= kKB)
    return FormatDouble(static_cast<double>(bytes) / kKB, decimals) + " KB";
  return std::to_string(bytes) + " B";
}

std::string FormatGB(uint64_t bytes, int decimals) {
  return FormatDouble(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0),
                      decimals) +
         " GB";
}

std::string FormatMB(uint64_t bytes, int decimals) {
  return FormatDouble(static_cast<double>(bytes) / (1024.0 * 1024.0),
                      decimals) +
         " MB";
}

bool IEqualsAscii(std::string_view a, std::string_view b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}

bool ContainsNoCase(std::string_view haystack, std::string_view needle) {
  if (needle.empty())
    return true;
  if (needle.size() > haystack.size())
    return false;
  const auto it =
      std::search(haystack.begin(), haystack.end(), needle.begin(),
                  needle.end(), [](char a, char b) {
                    return std::tolower(static_cast<unsigned char>(a)) ==
                           std::tolower(static_cast<unsigned char>(b));
                  });
  return it != haystack.end();
}

std::optional<std::wstring> RegReadString(HKEY root,
                                          const wchar_t* subkey,
                                          const wchar_t* value) {
  constexpr DWORD kFlags = RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ;
  DWORD type = 0;
  DWORD size = 0;
  LONG rc = ::RegGetValueW(root, subkey, value, kFlags, &type, nullptr, &size);
  if (rc != ERROR_SUCCESS || size == 0)
    return std::nullopt;
  std::wstring out(size / sizeof(wchar_t) + 1, L'\0');
  DWORD out_size = static_cast<DWORD>(out.size() * sizeof(wchar_t));
  rc = ::RegGetValueW(root, subkey, value, kFlags, &type, out.data(),
                      &out_size);
  if (rc != ERROR_SUCCESS)
    return std::nullopt;
  out.resize(::wcsnlen(out.c_str(), out.size()));
  return out;
}

std::optional<uint32_t> RegReadDword(HKEY root,
                                     const wchar_t* subkey,
                                     const wchar_t* value) {
  DWORD data = 0;
  DWORD size = sizeof(data);
  DWORD type = 0;
  const LONG rc =
      ::RegGetValueW(root, subkey, value, RRF_RT_REG_DWORD, &type, &data, &size);
  if (rc != ERROR_SUCCESS)
    return std::nullopt;
  return data;
}

bool RegWriteString(HKEY root,
                    const wchar_t* subkey,
                    const wchar_t* value,
                    const std::wstring& data) {
  HKEY key = nullptr;
  LONG rc = ::RegCreateKeyExW(root, subkey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                              KEY_SET_VALUE, nullptr, &key, nullptr);
  if (rc != ERROR_SUCCESS)
    return false;
  rc = ::RegSetValueExW(
      key, value, 0, REG_SZ, reinterpret_cast<const BYTE*>(data.c_str()),
      static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
  ::RegCloseKey(key);
  return rc == ERROR_SUCCESS;
}

bool RegDeleteValueIfPresent(HKEY root,
                             const wchar_t* subkey,
                             const wchar_t* value) {
  HKEY key = nullptr;
  if (::RegOpenKeyExW(root, subkey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
    return false;
  const LONG rc = ::RegDeleteValueW(key, value);
  ::RegCloseKey(key);
  return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
}

std::wstring ExecutablePathW() {
  std::wstring buf(MAX_PATH, L'\0');
  for (;;) {
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(),
                                         static_cast<DWORD>(buf.size()));
    if (n == 0)
      return {};
    if (n < buf.size()) {
      buf.resize(n);
      return buf;
    }
    if (buf.size() > 32768)
      return {};
    buf.resize(buf.size() * 2);
  }
}

std::string ExecutablePath() {
  return ToUtf8(ExecutablePathW());
}

std::wstring UserDataDirW() {
  PWSTR raw = nullptr;
  if (FAILED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw)))
    return {};
  std::wstring dir(raw);
  ::CoTaskMemFree(raw);
  dir += L"\\CADBrowser";
  ::CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

std::string Win32ErrorString(DWORD err) {
  char code[24];
  ::_snprintf_s(code, sizeof(code), _TRUNCATE, "0x%08lX", err);
  LPSTR msg = nullptr;
  const DWORD n = ::FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPSTR>(&msg), 0, nullptr);
  std::string out(code);
  if (n > 0 && msg) {
    out += ": ";
    out += TrimAscii(std::string(msg, n));
  }
  if (msg)
    ::LocalFree(msg);
  return out;
}

}  // namespace cad::sys
