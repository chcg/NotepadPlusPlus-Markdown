#include "core/PageMessages.h"

#include <nlohmann/json.hpp>

#include "core/TextConvert.h"

namespace mdr {

namespace {

using nlohmann::json;

constexpr int kMaxLine = 100'000'000;
constexpr size_t kMaxUrlLength = 8192;
constexpr size_t kMaxTitleLength = 1024;
constexpr size_t kMaxExportBytes = size_t{512} * 1024 * 1024;

// Scintilla text may contain invalid UTF-8; replace rather than throw.
std::wstring Dump(const json& value) {
  return Utf8ToWide(value.dump(-1, ' ', false, json::error_handler_t::replace));
}

std::optional<int> IntField(const json& message, const char* name, int min, int max) {
  auto it = message.find(name);
  if (it == message.end() || !it->is_number()) return std::nullopt;
  const double value = it->get<double>();
  if (!(value >= min && value <= max)) return std::nullopt;
  return static_cast<int>(value);
}

std::optional<std::string> StringField(const json& message, const char* name, size_t maxBytes) {
  auto it = message.find(name);
  if (it == message.end() || !it->is_string()) return std::nullopt;
  const auto& value = it->get_ref<const std::string&>();
  if (value.size() > maxBytes) return std::nullopt;
  return value;
}

}  // namespace

std::wstring BuildRenderMessage(const RenderRequest& request) {
  json message = {
      {"type", "render"},
      {"seq", request.seq},
      {"text", request.textUtf8},
      {"baseHref", WideToUtf8(request.baseHref)},
      {"reset", request.reset},
  };
  if (request.scrollLine) message["scrollLine"] = *request.scrollLine;
  return Dump(message);
}

std::wstring BuildSettingsMessage(const PageSettings& settings) {
  return Dump({
      {"type", "settings"},
      {"theme", settings.dark ? "dark" : "light"},
      {"allowRemoteImages", settings.allowRemoteImages},
      {"mermaid", settings.mermaid},
      {"syncScroll", settings.syncScroll},
      {"customCss", settings.customCssUtf8},
  });
}

std::wstring BuildScrollMessage(int line) {
  return Dump({{"type", "scrollTo"}, {"line", line}});
}

std::wstring BuildExportMessage(int id, bool dark) {
  return Dump({{"type", "export"}, {"id", id}, {"theme", dark ? "dark" : "light"}});
}

std::optional<PageMessage> ParsePageMessage(std::wstring_view text) {
  const json message = json::parse(WideToUtf8(text), nullptr, /*allow_exceptions=*/false);
  if (!message.is_object()) return std::nullopt;
  auto type = StringField(message, "type", 32);
  if (!type) return std::nullopt;

  PageMessage result;
  if (*type == "ready") {
    result.type = PageMessage::Type::Ready;
  } else if (*type == "rendered") {
    auto it = message.find("seq");
    if (it == message.end() || !it->is_number_integer()) return std::nullopt;
    result.type = PageMessage::Type::Rendered;
    result.seq = it->get<int64_t>();
  } else if (*type == "reveal" || *type == "gotoLine") {
    auto line = IntField(message, "line", 0, kMaxLine);
    if (!line) return std::nullopt;
    result.type = *type == "reveal" ? PageMessage::Type::Reveal : PageMessage::Type::GotoLine;
    result.line = *line;
  } else if (*type == "link") {
    auto url = StringField(message, "url", kMaxUrlLength);
    if (!url || url->empty()) return std::nullopt;
    result.type = PageMessage::Type::Link;
    result.url = Utf8ToWide(*url);
  } else if (*type == "exportResult") {
    auto id = IntField(message, "id", 0, INT32_MAX);
    auto html = StringField(message, "html", kMaxExportBytes);
    auto title = StringField(message, "title", kMaxTitleLength);
    if (!id || !html || !title) return std::nullopt;
    result.type = PageMessage::Type::ExportResult;
    result.id = *id;
    result.html = std::move(*html);
    result.title = Utf8ToWide(*title);
  } else {
    return std::nullopt;
  }
  return result;
}

}  // namespace mdr
