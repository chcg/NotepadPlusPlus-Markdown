#include "core/MarkdownDetect.h"

#include "core/DocUrl.h"
#include "core/TextConvert.h"

namespace mdr {

std::vector<std::wstring> ParseExtensionList(std::wstring_view list) {
  std::vector<std::wstring> result;
  std::wstring current;
  auto flush = [&] {
    while (!current.empty() && current.front() == L'.') current.erase(current.begin());
    if (!current.empty()) result.push_back(ToLower(current));
    current.clear();
  };
  for (wchar_t ch : list) {
    if (ch == L' ' || ch == L',' || ch == L';' || ch == L'\t') {
      flush();
    } else {
      current.push_back(ch);
    }
  }
  flush();
  return result;
}

bool HasMarkdownExtension(std::wstring_view path, const std::vector<std::wstring>& extensions) {
  const std::wstring ext = ExtensionOf(path);
  if (ext.empty()) return false;
  for (const auto& candidate : extensions) {
    if (ext == candidate) return true;
  }
  return false;
}

bool LanguageNameIsMarkdown(std::wstring_view languageName) {
  return ToLower(languageName).find(L"markdown") != std::wstring::npos;
}

}  // namespace mdr
