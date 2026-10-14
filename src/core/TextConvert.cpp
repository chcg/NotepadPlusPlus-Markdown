#include "core/TextConvert.h"

#include <windows.h>

#include <climits>

namespace mdr {

namespace {

std::wstring ToWide(std::string_view bytes, UINT codePage) {
  if (bytes.empty() || bytes.size() > INT_MAX) return {};
  const int size = static_cast<int>(bytes.size());
  const int needed = ::MultiByteToWideChar(codePage, 0, bytes.data(), size, nullptr, 0);
  if (needed <= 0) return {};
  std::wstring out(static_cast<size_t>(needed), L'\0');
  ::MultiByteToWideChar(codePage, 0, bytes.data(), size, out.data(), needed);
  return out;
}

}  // namespace

std::string WideToUtf8(std::wstring_view text) {
  if (text.empty() || text.size() > INT_MAX) return {};
  const int size = static_cast<int>(text.size());
  const int needed = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), size, nullptr, 0, nullptr, nullptr);
  if (needed <= 0) return {};
  std::string out(static_cast<size_t>(needed), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, text.data(), size, out.data(), needed, nullptr, nullptr);
  return out;
}

std::wstring Utf8ToWide(std::string_view text) {
  return ToWide(text, CP_UTF8);
}

std::string CodePageToUtf8(std::string_view bytes, unsigned codePage) {
  if (codePage == CP_UTF8) return std::string(bytes);
  return WideToUtf8(ToWide(bytes, codePage == 0 ? CP_ACP : codePage));
}

std::wstring ToLower(std::wstring_view text) {
  std::wstring out(text);
  if (!out.empty() && out.size() <= INT_MAX) {
    ::CharLowerBuffW(out.data(), static_cast<DWORD>(out.size()));
  }
  return out;
}

}  // namespace mdr
