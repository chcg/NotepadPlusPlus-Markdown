#pragma once

// JSON messages exchanged with the preview page (web/src/bridge.js documents
// the same protocol from the page's side).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mdr {

// ---- plugin -> page ----------------------------------------------------------

struct RenderRequest {
  int64_t seq = 0;
  std::string_view textUtf8;
  std::wstring baseHref;          // DocBaseUrlForDirectory(...) of the document
  bool reset = false;             // a different document: re-layout and scroll
  std::optional<int> scrollLine;  // with reset: first visible source line
};

struct PageSettings {
  bool dark = false;
  bool allowRemoteImages = true;
  bool mermaid = true;
  bool syncScroll = true;
  std::string customCssUtf8;
};

std::wstring BuildRenderMessage(const RenderRequest& request);
std::wstring BuildSettingsMessage(const PageSettings& settings);
std::wstring BuildScrollMessage(int line);
std::wstring BuildExportMessage(int id, bool dark);

// ---- page -> plugin ----------------------------------------------------------

struct PageMessage {
  enum class Type { Ready, Rendered, Reveal, GotoLine, Link, ExportResult };

  Type type = Type::Ready;
  int64_t seq = 0;
  int line = 0;
  int id = 0;
  std::wstring url;   // Link: URL resolved by the page against the document base
  std::string html;   // ExportResult (UTF-8)
  std::wstring title; // ExportResult
};

// Strict parse of a message from the page. Unknown types, missing fields and
// out-of-range values are rejected (nullopt).
std::optional<PageMessage> ParsePageMessage(std::wstring_view json);

}  // namespace mdr
