#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace mdr {

// "md markdown .MDOWN" -> {"md", "markdown", "mdown"}
std::vector<std::wstring> ParseExtensionList(std::wstring_view list);

bool HasMarkdownExtension(std::wstring_view path, const std::vector<std::wstring>& extensions);

// For UDL buffers Notepad++ reports names like "udf - Markdown (preinstalled)".
bool LanguageNameIsMarkdown(std::wstring_view languageName);

}  // namespace mdr
