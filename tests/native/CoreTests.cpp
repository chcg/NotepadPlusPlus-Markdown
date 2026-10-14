#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <windows.h>

#include "core/ClipboardHtml.h"
#include "core/DocUrl.h"
#include "core/LinkPolicy.h"
#include "core/MarkdownDetect.h"
#include "core/PageMessages.h"
#include "core/TextConvert.h"

using namespace mdr;
using Kind = LinkAction::Kind;

namespace doctest {
template <>
struct StringMaker<std::wstring> {
  static String convert(const std::wstring& value) { return WideToUtf8(value).c_str(); }
};
}  // namespace doctest

// ---- DocUrl ---------------------------------------------------------------------

TEST_CASE("document directories map to doc-origin base URLs") {
  CHECK(DocBaseUrlForDirectory(L"C:\\docs\\guide") == L"https://doc.markdownreader.example/C:/docs/guide/");
  CHECK(DocBaseUrlForDirectory(L"c:\\docs\\guide\\") == L"https://doc.markdownreader.example/C:/docs/guide/");
  CHECK(DocBaseUrlForDirectory(L"C:\\") == L"https://doc.markdownreader.example/C:/");
  CHECK(DocBaseUrlForDirectory(L"C:\\My Docs\\Café #1") ==
        L"https://doc.markdownreader.example/C:/My%20Docs/Caf%C3%A9%20%231/");
  CHECK(DocBaseUrlForDirectory(L"\\\\server\\share\\team docs") ==
        L"https://doc.markdownreader.example/UNC/server/share/team%20docs/");
}

TEST_CASE("non-absolute directories map to the empty doc root") {
  CHECK(DocBaseUrlForDirectory(L"") == L"https://doc.markdownreader.example/");
  CHECK(DocBaseUrlForDirectory(L"relative\\dir") == L"https://doc.markdownreader.example/");
  CHECK(DocBaseUrlForDirectory(L"\\\\?\\C:\\x") == L"https://doc.markdownreader.example/");
  CHECK(DocBaseUrlForDirectory(L"\\\\server") == L"https://doc.markdownreader.example/");
}

TEST_CASE("doc URLs map back to file paths") {
  CHECK(FilePathFromDocUrl(L"https://doc.markdownreader.example/C:/docs/guide/images/a.png") ==
        L"C:\\docs\\guide\\images\\a.png");
  CHECK(FilePathFromDocUrl(L"https://doc.markdownreader.example/C:/x.png") == L"C:\\x.png");  // drive root (NppMarkdownPanel #152)
  CHECK(FilePathFromDocUrl(L"https://doc.markdownreader.example/C:/My%20Docs/Caf%C3%A9%20%231/x.png?v=2#frag") ==
        L"C:\\My Docs\\Café #1\\x.png");
  CHECK(FilePathFromDocUrl(L"https://DOC.markdownreader.example/UNC/server/share/img.png") == L"\\\\server\\share\\img.png");
}

TEST_CASE("round trip directory -> base URL -> file path") {
  for (std::wstring dir : {L"C:\\docs", L"D:\\a b\\ä ö ü\\#%&+", L"\\\\srv\\share\\x y"}) {
    auto path = FilePathFromDocUrl(DocBaseUrlForDirectory(dir) + L"file%20name.md");
    REQUIRE(path.has_value());
    CHECK(*path == dir + L"\\file name.md");
  }
}

TEST_CASE("unsafe doc URLs are rejected") {
  const std::wstring_view bad[] = {
      L"https://app.markdownreader.example/C:/x.png",           // wrong origin
      L"https://evil.example/C:/x.png",
      L"https://doc.markdownreader.example/",                   // no file
      L"https://doc.markdownreader.example/C:/docs/",           // directory
      L"https://doc.markdownreader.example/docs/x.png",         // not absolute
      L"https://doc.markdownreader.example/C:/a/../b.png",      // dot segments
      L"https://doc.markdownreader.example/C:/a/./b.png",
      L"https://doc.markdownreader.example/C:/a/%2e%2e/b.png",
      L"https://doc.markdownreader.example/C:/a%5Cb.png",       // encoded backslash
      L"https://doc.markdownreader.example/C:/a%2Fb.png",       // encoded slash
      L"https://doc.markdownreader.example/C:/a%3Ab.png",       // alternate data stream
      L"https://doc.markdownreader.example/C:/a//b.png",        // empty segment
      L"https://doc.markdownreader.example/C:/CON.png",         // device names
      L"https://doc.markdownreader.example/C:/docs/nul",
      L"https://doc.markdownreader.example/C:/x%00.png",        // control characters
      L"https://doc.markdownreader.example/C:/x%ZZ.png",        // malformed escape
      L"https://doc.markdownreader.example/C:/x%2",
      L"https://doc.markdownreader.example/UNC/server/x.png",   // UNC without share
      L"https://doc.markdownreader.example/UNC/./pipe/x",
      L"https://doc.markdownreader.example/C:/%5Cserver%5Cshare%5Ctool.exe",
  };
  for (auto url : bad) {
    CAPTURE(WideToUtf8(url));
    CHECK_FALSE(FilePathFromDocUrl(url).has_value());
  }
}

TEST_CASE("preview MIME types are an allowlist") {
  CHECK(PreviewMimeType(L"C:\\a\\B.PNG") == L"image/png");
  CHECK(PreviewMimeType(L"x.svg") == L"image/svg+xml");
  CHECK(PreviewMimeType(L"x.mp4") == L"video/mp4");
  CHECK_FALSE(PreviewMimeType(L"x.html").has_value());
  CHECK_FALSE(PreviewMimeType(L"x.md").has_value());
  CHECK_FALSE(PreviewMimeType(L"x.exe").has_value());
  CHECK_FALSE(PreviewMimeType(L"noextension").has_value());
}

TEST_CASE("path helpers") {
  CHECK(DirectoryOf(L"C:\\a\\b.md") == L"C:\\a");
  CHECK(DirectoryOf(L"C:\\b.md") == L"C:\\");
  CHECK(DirectoryOf(L"new 1") == L"");
  CHECK(DirectoryOf(L"\\\\srv\\share\\b.md") == L"\\\\srv\\share");
  CHECK(ExtensionOf(L"C:\\a.b\\README.MD") == L"md");
  CHECK(ExtensionOf(L"C:\\a.b\\Makefile") == L"");
  CHECK(ExtensionOf(L".gitignore") == L"");
}

// ---- LinkPolicy -------------------------------------------------------------------

TEST_CASE("external web and mail links open externally") {
  for (std::wstring_view url : {L"https://example.com/", L"http://intranet/wiki?a=1#b", L"HTTPS://Example.com/x",
                                 L"mailto:someone@example.com"}) {
    auto action = ClassifyLink(url);
    CAPTURE(WideToUtf8(url));
    CHECK(action.kind == Kind::OpenExternal);
    CHECK(action.target == url);
  }
}

TEST_CASE("local text documents open in Notepad++") {
  auto action = ClassifyLink(L"https://doc.markdownreader.example/C:/docs/sub/other.md#section");
  CHECK(action.kind == Kind::OpenInEditor);
  CHECK(action.target == L"C:\\docs\\sub\\other.md");
  CHECK(ClassifyLink(L"https://doc.markdownreader.example/UNC/srv/share/notes.txt").kind == Kind::OpenInEditor);
  CHECK(ClassifyLink(L"https://doc.markdownreader.example/C:/src/build.py").kind == Kind::OpenInEditor);
}

TEST_CASE("everything else is blocked") {
  const std::wstring_view blocked[] = {
      // From tests/manual/xss.md, as the page resolves them:
      L"https://doc.markdownreader.example/C:/docs/evil.cmd",
      L"https://doc.markdownreader.example/C:/docs/calc.exe",
      L"https://doc.markdownreader.example/C:/docs/%5Cserver%5Cshare%5Ctool.exe",
      L"shell:AppsFolder\\Microsoft.WindowsCalculator_8wekyb3d8bbwe!App",
      L"file:///C:/Windows/System32/calc.exe",
      L"ms-settings:privacy",
      L"javascript:alert(1)",
      L"data:text/html,<script>alert(1)</script>",
      L"vbscript:msgbox(1)",
      // Other executables and shortcuts, even if a link names them:
      L"https://doc.markdownreader.example/C:/x/run.bat",
      L"https://doc.markdownreader.example/C:/x/run.ps1",
      L"https://doc.markdownreader.example/C:/x/run.js",
      L"https://doc.markdownreader.example/C:/x/run.vbs",
      L"https://doc.markdownreader.example/C:/x/link.lnk",
      L"https://doc.markdownreader.example/C:/x/page.url",
      L"https://doc.markdownreader.example/C:/x/setup.msi",
      L"https://doc.markdownreader.example/C:/x/app.hta",
      L"https://doc.markdownreader.example/C:/x/image.png",  // not a text document
      L"https://doc.markdownreader.example/C:/x/noext",
      // The page itself and malformed URLs:
      L"https://app.markdownreader.example/index.html",
      L"http://doc.markdownreader.example/C:/x/a.md",
      L"https:///nohost",
      L"https://example.com/a b",
      L"https://example.com/\"onmouseover",
      L"https://example.com\\@evil.example",
      L"C:\\Windows\\calc.exe",
      L"\\\\server\\share\\tool.exe",
      L"",
  };
  for (auto url : blocked) {
    CAPTURE(WideToUtf8(url));
    CHECK(ClassifyLink(url).kind == Kind::Block);
  }
}

// ---- Markdown detection ------------------------------------------------------------

TEST_CASE("markdown detection") {
  const auto exts = ParseExtensionList(L"md markdown, .MDOWN;mkd");
  CHECK(exts == std::vector<std::wstring>{L"md", L"markdown", L"mdown", L"mkd"});
  CHECK(HasMarkdownExtension(L"C:\\a\\README.MD", exts));
  CHECK(HasMarkdownExtension(L"x.mdown", exts));
  CHECK_FALSE(HasMarkdownExtension(L"x.txt", exts));
  CHECK_FALSE(HasMarkdownExtension(L"new 1", exts));
  CHECK(LanguageNameIsMarkdown(L"udf - Markdown (preinstalled)"));
  CHECK(LanguageNameIsMarkdown(L"udf - Markdown (preinstalled dark mode)"));
  CHECK_FALSE(LanguageNameIsMarkdown(L"udf - Pascal"));
  CHECK_FALSE(LanguageNameIsMarkdown(L"normal"));
}

// ---- Text conversion ----------------------------------------------------------------

TEST_CASE("code page conversion to UTF-8") {
  CHECK(CodePageToUtf8("caf\xC3\xA9", CP_UTF8) == "caf\xC3\xA9");
  CHECK(CodePageToUtf8("caf\xE9", 1252) == "caf\xC3\xA9");
  CHECK(CodePageToUtf8("", 1252).empty());
  CHECK(Utf8ToWide("\xEF\xBB\xBFhi") == L"\xFEFFhi");
  CHECK(WideToUtf8(L"Ünïcödé ✓") == "\xC3\x9Cn\xC3\xAF" "c\xC3\xB6" "d\xC3\xA9 \xE2\x9C\x93");
}

// ---- Page messages ---------------------------------------------------------------------

TEST_CASE("render message carries text, base and flags") {
  RenderRequest request;
  request.seq = 7;
  request.textUtf8 = "# T\n\"quoted\" \\ \x01";
  request.baseHref = L"https://doc.markdownreader.example/C:/d/";
  request.reset = true;
  request.scrollLine = 12;
  const std::wstring json = BuildRenderMessage(request);
  CHECK(json.find(L"\"type\":\"render\"") != std::wstring::npos);
  CHECK(json.find(L"\"seq\":7") != std::wstring::npos);
  CHECK(json.find(L"\"text\":\"# T\\n\\\"quoted\\\" \\\\ \\u0001\"") != std::wstring::npos);
  CHECK(json.find(L"\"scrollLine\":12") != std::wstring::npos);
  CHECK(json.find(L"\"reset\":true") != std::wstring::npos);
}

TEST_CASE("invalid UTF-8 in the document does not break the render message") {
  RenderRequest request;
  request.textUtf8 = "ok \xFF\xFE bad";
  const std::wstring json = BuildRenderMessage(request);
  CHECK(json.find(L"ok \xFFFD") != std::wstring::npos);
}

TEST_CASE("page messages are parsed strictly") {
  auto reveal = ParsePageMessage(LR"({"type":"reveal","line":42})");
  REQUIRE(reveal);
  CHECK(reveal->type == PageMessage::Type::Reveal);
  CHECK(reveal->line == 42);

  auto link = ParsePageMessage(LR"({"type":"link","href":"x.md","url":"https://doc.markdownreader.example/C:/x.md"})");
  REQUIRE(link);
  CHECK(link->url == L"https://doc.markdownreader.example/C:/x.md");

  auto exported = ParsePageMessage(LR"({"type":"exportResult","id":3,"html":"<p>é</p>","title":"T"})");
  REQUIRE(exported);
  CHECK(exported->html == "<p>\xC3\xA9</p>");

  const std::wstring_view bad[] = {
      L"", L"not json", L"[]", L"{}", LR"({"type":"nope"})", LR"({"type":"reveal"})",
      LR"({"type":"reveal","line":-1})", LR"({"type":"reveal","line":"3"})", LR"({"type":"reveal","line":1e12})",
      LR"({"type":"link"})", LR"({"type":"link","url":""})", LR"({"type":"exportResult","id":1,"html":"x"})",
      LR"({"type":"rendered","seq":1.5})",
  };
  for (auto json : bad) {
    CAPTURE(WideToUtf8(json));
    CHECK_FALSE(ParsePageMessage(json).has_value());
  }
}

// ---- Clipboard HTML ---------------------------------------------------------------------

namespace {
size_t HeaderValue(const std::string& cf, const char* key) {
  const size_t at = cf.find(key);
  REQUIRE(at != std::string::npos);
  return std::stoul(cf.substr(at + strlen(key), 10));
}
}  // namespace

TEST_CASE("CF_HTML offsets point at the document and the <main> fragment") {
  const std::string html = "<!doctype html><html><head><style>p{}</style></head><body>\n<main class=\"markdown-body\"><p>caf\xC3\xA9</p></main>\n</body></html>";
  const std::string cf = BuildClipboardHtml(html);
  const size_t startHtml = HeaderValue(cf, "StartHTML:");
  const size_t endHtml = HeaderValue(cf, "EndHTML:");
  const size_t startFragment = HeaderValue(cf, "StartFragment:");
  const size_t endFragment = HeaderValue(cf, "EndFragment:");
  CHECK(cf.substr(startHtml, 15) == "<!doctype html>");
  CHECK(endHtml == cf.size());
  CHECK(cf.substr(startFragment, endFragment - startFragment) == "<main class=\"markdown-body\"><p>caf\xC3\xA9</p></main>");
  CHECK(cf.substr(startFragment - 20, 20) == "<!--StartFragment-->");
  CHECK(cf.substr(endFragment, 18) == "<!--EndFragment-->");
}

TEST_CASE("CF_HTML falls back to the body, then the whole input") {
  const std::string cf = BuildClipboardHtml("<html><body><p>x</p></body></html>");
  CHECK(cf.substr(HeaderValue(cf, "StartFragment:"), 8) == "<p>x</p>");
  const std::string bare = BuildClipboardHtml("<p>y</p>");
  CHECK(bare.substr(HeaderValue(bare, "StartFragment:"), 8) == "<p>y</p>");
}
