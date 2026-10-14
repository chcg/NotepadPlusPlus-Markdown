#pragma once

// Decides what happens when the user clicks a link in the preview.
//
// The page never navigates; it posts the link's resolved URL to the plugin,
// which applies this policy. Anything not explicitly allowed is blocked.
// (NppMarkdownPanel shipped a code-execution bug in 2026 by passing links
// straight to ShellExecute; this exists so that cannot happen here.)

#include <string>
#include <string_view>

namespace mdr {

struct LinkAction {
  enum class Kind {
    Block,         // do nothing
    OpenExternal,  // http(s)/mailto: hand the URL to the default browser / mail client
    OpenInEditor,  // local text document: open it in Notepad++
  };

  Kind kind = Kind::Block;
  std::wstring target;  // URL (OpenExternal) or file path (OpenInEditor)
  std::wstring reason;  // why it was blocked, for the log
};

LinkAction ClassifyLink(std::wstring_view resolvedUrl);

// Text/source files that may be opened in Notepad++ from a link. Files that
// Windows itself would execute (.bat, .cmd, .ps1, .js, .vbs, .exe, .lnk, ...)
// are never on this list.
bool IsOpenableTextDocument(std::wstring_view path);

}  // namespace mdr
