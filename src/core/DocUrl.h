#pragma once

// URLs the preview page uses, and the mapping between document-relative
// URLs and files on disk.
//
// The page is served from the app origin (bundled assets). The document's
// own files (images, linked .md files) are addressed on the doc origin by
// their absolute path, so relative links with ../ resolve correctly:
//
//   C:\docs\guide          <->  https://doc.markdownreader.example/C:/docs/guide/
//   \\server\share\docs    <->  https://doc.markdownreader.example/UNC/server/share/docs/

#include <optional>
#include <string>
#include <string_view>

namespace mdr {

inline constexpr std::wstring_view kAppHost = L"app.markdownreader.example";
inline constexpr std::wstring_view kDocHost = L"doc.markdownreader.example";
inline constexpr std::wstring_view kAppOrigin = L"https://app.markdownreader.example";
inline constexpr std::wstring_view kDocOrigin = L"https://doc.markdownreader.example";
inline constexpr std::wstring_view kPageUrl = L"https://app.markdownreader.example/index.html";

// Base URL (with trailing slash) for a document directory. Anything that is
// not an absolute drive or UNC path (e.g. an untitled buffer) maps to the doc
// origin root, where nothing resolves.
std::wstring DocBaseUrlForDirectory(std::wstring_view directory);

// Inverse mapping for a URL on the doc origin. Query and fragment are ignored.
// Returns nullopt for other origins and for anything that does not decode to
// a plain absolute file path (dot segments, device names, illegal characters).
std::optional<std::wstring> FilePathFromDocUrl(std::wstring_view url);

// MIME type for the file types the preview may load from disk
// (images, audio, video); nullopt for everything else.
std::optional<std::wstring> PreviewMimeType(std::wstring_view path);

// Directory part of a full file path ("C:\a\b.md" -> "C:\a"); empty if the
// path has no directory (untitled buffers are named like "new 1").
std::wstring DirectoryOf(std::wstring_view path);

// Lowercase extension without the dot ("C:\a\B.MD" -> "md").
std::wstring ExtensionOf(std::wstring_view path);

}  // namespace mdr
