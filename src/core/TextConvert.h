#pragma once

#include <string>
#include <string_view>

namespace mdr {

std::string WideToUtf8(std::wstring_view text);

// Invalid sequences become U+FFFD.
std::wstring Utf8ToWide(std::string_view text);

// Converts text stored in a Windows code page to UTF-8. CP_UTF8 (65001)
// is returned unchanged and 0 means the system ANSI code page, matching
// what Scintilla's SCI_GETCODEPAGE reports.
std::string CodePageToUtf8(std::string_view bytes, unsigned codePage);

std::wstring ToLower(std::wstring_view text);

}  // namespace mdr
