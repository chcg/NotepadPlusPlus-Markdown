#include "core/LinkPolicy.h"

#include "core/DocUrl.h"
#include "core/TextConvert.h"

namespace mdr {

namespace {

LinkAction Blocked(std::wstring reason) {
  return {LinkAction::Kind::Block, {}, std::move(reason)};
}

// Scheme as written before the first ':', lowercased; empty if there is none.
std::wstring SchemeOf(std::wstring_view url) {
  const size_t colon = url.find(L':');
  if (colon == std::wstring_view::npos || colon == 0) return {};
  for (size_t i = 0; i < colon; ++i) {
    const wchar_t ch = url[i];
    const bool ok = (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
                    (i > 0 && ((ch >= L'0' && ch <= L'9') || ch == L'+' || ch == L'-' || ch == L'.'));
    if (!ok) return {};
  }
  return ToLower(url.substr(0, colon));
}

// Host of an http(s) URL (lowercased, without port or credentials).
std::wstring HostOf(std::wstring_view url) {
  const size_t start = url.find(L"://");
  if (start == std::wstring_view::npos) return {};
  std::wstring_view rest = url.substr(start + 3);
  rest = rest.substr(0, rest.find_first_of(L"/?#"));
  const size_t at = rest.rfind(L'@');
  if (at != std::wstring_view::npos) rest = rest.substr(at + 1);
  if (!rest.empty() && rest.front() != L'[') rest = rest.substr(0, rest.find(L':'));
  return ToLower(rest);
}

bool HasUnsafeCharacters(std::wstring_view url) {
  for (wchar_t ch : url) {
    if (ch <= 0x20 || ch == 0x7F || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'\\' || ch == L'^' || ch == L'`' || ch == L'|') {
      return true;
    }
  }
  return false;
}

}  // namespace

bool IsOpenableTextDocument(std::wstring_view path) {
  static constexpr std::wstring_view kExtensions[] = {
      L"md",   L"markdown", L"mdown", L"mkd",  L"mkdn", L"mdx",  L"mdwn", L"txt",  L"text", L"log",  L"csv",
      L"tsv",  L"json",     L"jsonc", L"xml",  L"yaml", L"yml",  L"toml", L"ini",  L"cfg",  L"conf", L"rst",
      L"adoc", L"asciidoc", L"org",   L"tex",  L"html", L"htm",  L"css",  L"scss", L"less", L"ts",   L"tsx",
      L"jsx",  L"mjs",      L"cjs",   L"py",   L"rb",   L"go",   L"rs",   L"java", L"kt",   L"cs",   L"fs",
      L"cpp",  L"cc",       L"cxx",   L"c",    L"h",    L"hpp",  L"hh",   L"hxx",  L"sql",  L"sh",   L"bash",
      L"zsh",  L"psm1",     L"psd1",  L"diff", L"patch", L"gradle", L"cmake", L"proto", L"graphql"};
  const std::wstring ext = ExtensionOf(path);
  if (ext.empty()) return false;
  for (auto allowed : kExtensions) {
    if (ext == allowed) return true;
  }
  return false;
}

LinkAction ClassifyLink(std::wstring_view url) {
  if (url.empty()) return Blocked(L"empty link");
  if (url.size() > 8192) return Blocked(L"link too long");

  const std::wstring scheme = SchemeOf(url);
  if (scheme == L"mailto") {
    if (HasUnsafeCharacters(url)) return Blocked(L"unsafe characters in link");
    return {LinkAction::Kind::OpenExternal, std::wstring(url), {}};
  }
  if (scheme != L"http" && scheme != L"https") {
    return Blocked(L"scheme not allowed: " + (scheme.empty() ? std::wstring(L"(none)") : scheme));
  }
  if (HasUnsafeCharacters(url)) return Blocked(L"unsafe characters in link");

  const std::wstring host = HostOf(url);
  if (host.empty()) return Blocked(L"link has no host");
  if (host == kAppHost) return Blocked(L"internal page link");

  if (host == kDocHost) {
    if (scheme != L"https") return Blocked(L"local link with wrong scheme");
    auto path = FilePathFromDocUrl(url);
    if (!path) return Blocked(L"not a valid local path");
    if (!IsOpenableTextDocument(*path)) return Blocked(L"not a text document: " + *path);
    return {LinkAction::Kind::OpenInEditor, std::move(*path), {}};
  }

  return {LinkAction::Kind::OpenExternal, std::wstring(url), {}};
}

}  // namespace mdr
