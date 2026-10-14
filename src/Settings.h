#pragma once

#include <string>

namespace mdr {

enum class ThemeSetting { Auto, Light, Dark };

// Stored in <plugins config dir>\MarkdownReader.ini. "Settings..." opens the
// file in Notepad++; saving it reloads the settings.
struct Settings {
  std::wstring extensions = L"md markdown mdown mkd mkdn mdwn mdx";
  bool autoOpenPanel = true;          // show the panel when a Markdown document is activated
  bool syncScroll = true;
  bool readingModeByDefault = false;  // open Markdown documents in reading mode
  ThemeSetting theme = ThemeSetting::Auto;  // Auto follows Notepad++ dark mode
  bool allowRemoteImages = true;
  bool mermaid = true;
  bool devTools = false;              // F12 / Inspect in the preview, for troubleshooting
  std::wstring customCss;             // path; empty = <config dir>\MarkdownReader\custom.css

  static Settings Load(const std::wstring& iniPath);
  void Save(const std::wstring& iniPath) const;
};

}  // namespace mdr
