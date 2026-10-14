#include "core/DocUrl.h"

#include <array>
#include <cwctype>
#include <vector>

#include "core/TextConvert.h"

namespace mdr {

namespace {

bool IsAsciiAlpha(wchar_t ch) {
  return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z');
}

bool IsUnreserved(unsigned char ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
         ch == '-' || ch == '.' || ch == '_' || ch == '~';
}

std::wstring PercentEncodeSegment(std::wstring_view segment) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::wstring out;
  for (unsigned char ch : WideToUtf8(segment)) {
    if (IsUnreserved(ch)) {
      out.push_back(static_cast<wchar_t>(ch));
    } else {
      out.push_back(L'%');
      out.push_back(static_cast<wchar_t>(kHex[ch >> 4]));
      out.push_back(static_cast<wchar_t>(kHex[ch & 0xF]));
    }
  }
  return out;
}

int HexValue(wchar_t ch) {
  if (ch >= L'0' && ch <= L'9') return ch - L'0';
  if (ch >= L'a' && ch <= L'f') return ch - L'a' + 10;
  if (ch >= L'A' && ch <= L'F') return ch - L'A' + 10;
  return -1;
}

// Percent-decodes as UTF-8. Rejects malformed escapes and raw non-ASCII.
std::optional<std::wstring> PercentDecode(std::wstring_view text) {
  std::string bytes;
  for (size_t i = 0; i < text.size(); ++i) {
    const wchar_t ch = text[i];
    if (ch == L'%') {
      if (i + 2 >= text.size()) return std::nullopt;
      const int hi = HexValue(text[i + 1]);
      const int lo = HexValue(text[i + 2]);
      if (hi < 0 || lo < 0) return std::nullopt;
      bytes.push_back(static_cast<char>((hi << 4) | lo));
      i += 2;
    } else if (ch < 0x80) {
      bytes.push_back(static_cast<char>(ch));
    } else {
      return std::nullopt;
    }
  }
  return Utf8ToWide(bytes);
}

bool IsReservedDeviceName(std::wstring_view segment) {
  static constexpr std::array<std::wstring_view, 22> kNames = {
      L"con",  L"prn",  L"aux",  L"nul",  L"com1", L"com2", L"com3", L"com4", L"com5", L"com6", L"com7",
      L"com8", L"com9", L"lpt1", L"lpt2", L"lpt3", L"lpt4", L"lpt5", L"lpt6", L"lpt7", L"lpt8", L"lpt9"};
  std::wstring base = ToLower(segment.substr(0, segment.find(L'.')));
  while (!base.empty() && base.back() == L' ') base.pop_back();
  for (auto name : kNames) {
    if (base == name) return true;
  }
  return false;
}

// A single decoded path component that is safe to join with backslashes.
bool IsSafeSegment(std::wstring_view segment) {
  if (segment.empty() || segment == L"." || segment == L"..") return false;
  for (wchar_t ch : segment) {
    if (ch < 0x20 || ch == 0xFFFD) return false;
    switch (ch) {
      case L'\\': case L'/': case L':': case L'*': case L'?': case L'"': case L'<': case L'>': case L'|':
        return false;
      default:
        break;
    }
  }
  return !IsReservedDeviceName(segment);
}

std::vector<std::wstring_view> Split(std::wstring_view text, wchar_t separator) {
  std::vector<std::wstring_view> parts;
  size_t start = 0;
  while (true) {
    const size_t end = text.find(separator, start);
    parts.push_back(text.substr(start, end == std::wstring_view::npos ? std::wstring_view::npos : end - start));
    if (end == std::wstring_view::npos) break;
    start = end + 1;
  }
  return parts;
}

bool StartsWithNoCase(std::wstring_view text, std::wstring_view prefix) {
  return text.size() >= prefix.size() && ToLower(text.substr(0, prefix.size())) == ToLower(prefix);
}

}  // namespace

std::wstring DocBaseUrlForDirectory(std::wstring_view directory) {
  std::wstring url(kDocOrigin);
  url.push_back(L'/');

  std::wstring dir(directory);
  for (auto& ch : dir) {
    if (ch == L'/') ch = L'\\';
  }
  while (dir.size() > 3 && dir.back() == L'\\') dir.pop_back();

  std::vector<std::wstring_view> segments;
  if (dir.size() >= 2 && IsAsciiAlpha(dir[0]) && dir[1] == L':' && (dir.size() == 2 || dir[2] == L'\\')) {
    url += std::wstring(1, static_cast<wchar_t>(std::towupper(dir[0]))) + L":/";
    if (dir.size() > 3) segments = Split(std::wstring_view(dir).substr(3), L'\\');
  } else if (dir.size() > 2 && dir[0] == L'\\' && dir[1] == L'\\' && dir[2] != L'?' && dir[2] != L'.') {
    url += L"UNC/";
    segments = Split(std::wstring_view(dir).substr(2), L'\\');
    if (segments.size() < 2) return std::wstring(kDocOrigin) + L"/";
  } else {
    return url;
  }

  for (auto segment : segments) {
    if (segment.empty()) continue;
    url += PercentEncodeSegment(segment);
    url.push_back(L'/');
  }
  return url;
}

std::optional<std::wstring> FilePathFromDocUrl(std::wstring_view url) {
  const std::wstring prefix = std::wstring(kDocOrigin) + L"/";
  if (!StartsWithNoCase(url, prefix)) return std::nullopt;

  std::wstring_view path = url.substr(prefix.size());
  path = path.substr(0, path.find_first_of(L"?#"));
  if (path.empty() || path.back() == L'/') return std::nullopt;

  std::vector<std::wstring> segments;
  for (auto raw : Split(path, L'/')) {
    auto decoded = PercentDecode(raw);
    if (!decoded) return std::nullopt;
    segments.push_back(std::move(*decoded));
  }

  std::wstring result;
  size_t first = 0;
  if (segments[0].size() == 2 && IsAsciiAlpha(segments[0][0]) && segments[0][1] == L':') {
    result = segments[0] + L"\\";
    first = 1;
  } else if (segments[0] == L"UNC") {
    // Need at least server, share and one more component.
    if (segments.size() < 4) return std::nullopt;
    result = L"\\\\";
    first = 1;
  } else {
    return std::nullopt;
  }
  if (first >= segments.size()) return std::nullopt;

  for (size_t i = first; i < segments.size(); ++i) {
    if (!IsSafeSegment(segments[i])) return std::nullopt;
    result += segments[i];
    if (i + 1 < segments.size()) result.push_back(L'\\');
  }
  return result;
}

std::optional<std::wstring> PreviewMimeType(std::wstring_view path) {
  struct Entry {
    std::wstring_view extension;
    std::wstring_view mime;
  };
  static constexpr std::array<Entry, 17> kTypes = {{
      {L"png", L"image/png"},       {L"jpg", L"image/jpeg"},  {L"jpeg", L"image/jpeg"},
      {L"gif", L"image/gif"},       {L"webp", L"image/webp"}, {L"bmp", L"image/bmp"},
      {L"ico", L"image/x-icon"},    {L"svg", L"image/svg+xml"}, {L"avif", L"image/avif"},
      {L"apng", L"image/apng"},     {L"mp4", L"video/mp4"},   {L"webm", L"video/webm"},
      {L"ogv", L"video/ogg"},       {L"mp3", L"audio/mpeg"},  {L"ogg", L"audio/ogg"},
      {L"wav", L"audio/wav"},       {L"m4a", L"audio/mp4"},
  }};
  const std::wstring ext = ExtensionOf(path);
  for (const auto& entry : kTypes) {
    if (ext == entry.extension) return std::wstring(entry.mime);
  }
  return std::nullopt;
}

std::wstring DirectoryOf(std::wstring_view path) {
  const size_t slash = path.find_last_of(L"\\/");
  if (slash == std::wstring_view::npos) return {};
  if (slash == 2 && path.size() > 1 && path[1] == L':') return std::wstring(path.substr(0, 3));  // "C:\"
  return std::wstring(path.substr(0, slash));
}

std::wstring ExtensionOf(std::wstring_view path) {
  const size_t slash = path.find_last_of(L"\\/");
  const std::wstring_view name = slash == std::wstring_view::npos ? path : path.substr(slash + 1);
  const size_t dot = name.find_last_of(L'.');
  if (dot == std::wstring_view::npos || dot == 0) return {};
  return ToLower(name.substr(dot + 1));
}

}  // namespace mdr
