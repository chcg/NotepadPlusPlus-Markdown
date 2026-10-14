#include "core/ClipboardHtml.h"

#include <cstdio>

namespace mdr {

namespace {

constexpr std::string_view kStartMarker = "<!--StartFragment-->";
constexpr std::string_view kEndMarker = "<!--EndFragment-->";

// Byte range of the fragment within `html`: the whole <main> element (so
// styles scoped to .markdown-body still apply), else the body's content.
std::pair<size_t, size_t> FragmentRange(std::string_view html) {
  const size_t mainOpen = html.find("<main");
  const size_t mainClose = html.rfind("</main>");
  if (mainOpen != std::string_view::npos && mainClose != std::string_view::npos && mainClose > mainOpen) {
    return {mainOpen, mainClose + std::string_view("</main>").size()};
  }
  const size_t bodyOpen = html.find("<body");
  const size_t bodyStart = bodyOpen == std::string_view::npos ? bodyOpen : html.find('>', bodyOpen);
  const size_t bodyClose = html.rfind("</body>");
  if (bodyStart != std::string_view::npos && bodyClose != std::string_view::npos && bodyClose > bodyStart) {
    return {bodyStart + 1, bodyClose};
  }
  return {0, html.size()};
}

}  // namespace

std::string BuildClipboardHtml(std::string_view html) {
  const auto [fragmentStart, fragmentEnd] = FragmentRange(html);

  std::string body;
  body.reserve(html.size() + kStartMarker.size() + kEndMarker.size());
  body.append(html.substr(0, fragmentStart));
  body.append(kStartMarker);
  body.append(html.substr(fragmentStart, fragmentEnd - fragmentStart));
  body.append(kEndMarker);
  body.append(html.substr(fragmentEnd));

  // The header has fixed-width numbers, so its length is known up front.
  constexpr char kHeaderFormat[] =
      "Version:0.9\r\nStartHTML:%010zu\r\nEndHTML:%010zu\r\nStartFragment:%010zu\r\nEndFragment:%010zu\r\n";
  char header[160];
  const int headerLength = std::snprintf(header, sizeof(header), kHeaderFormat, size_t{0}, size_t{0}, size_t{0}, size_t{0});
  const size_t startHtml = static_cast<size_t>(headerLength);
  const size_t startFragment = startHtml + fragmentStart + kStartMarker.size();
  const size_t endFragment = startFragment + (fragmentEnd - fragmentStart);
  const size_t endHtml = startHtml + body.size();
  std::snprintf(header, sizeof(header), kHeaderFormat, startHtml, endHtml, startFragment, endFragment);

  return std::string(header) + body;
}

}  // namespace mdr
