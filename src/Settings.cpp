#include "Settings.h"

#include <windows.h>

#include "core/TextConvert.h"

namespace mdr {

namespace {

constexpr wchar_t kSection[] = L"MarkdownReader";

std::wstring ReadString(const std::wstring& ini, const wchar_t* key, const std::wstring& fallback) {
  wchar_t buffer[2048] = {};
  ::GetPrivateProfileStringW(kSection, key, fallback.c_str(), buffer, static_cast<DWORD>(std::size(buffer)), ini.c_str());
  return buffer;
}

bool ReadBool(const std::wstring& ini, const wchar_t* key, bool fallback) {
  const std::wstring value = ToLower(ReadString(ini, key, fallback ? L"1" : L"0"));
  if (value == L"1" || value == L"true" || value == L"yes" || value == L"on") return true;
  if (value == L"0" || value == L"false" || value == L"no" || value == L"off") return false;
  return fallback;
}

void Write(const std::wstring& ini, const wchar_t* key, const std::wstring& value) {
  ::WritePrivateProfileStringW(kSection, key, value.c_str(), ini.c_str());
}

void Write(const std::wstring& ini, const wchar_t* key, bool value) {
  Write(ini, key, std::wstring(value ? L"1" : L"0"));
}

const wchar_t* ThemeName(ThemeSetting theme) {
  switch (theme) {
    case ThemeSetting::Light: return L"light";
    case ThemeSetting::Dark: return L"dark";
    default: return L"auto";
  }
}

}  // namespace

Settings Settings::Load(const std::wstring& iniPath) {
  Settings s;
  s.extensions = ReadString(iniPath, L"extensions", s.extensions);
  s.autoOpenPanel = ReadBool(iniPath, L"autoOpenPanel", s.autoOpenPanel);
  s.syncScroll = ReadBool(iniPath, L"syncScroll", s.syncScroll);
  s.readingModeByDefault = ReadBool(iniPath, L"readingModeByDefault", s.readingModeByDefault);
  const std::wstring theme = ToLower(ReadString(iniPath, L"theme", L"auto"));
  s.theme = theme == L"light" ? ThemeSetting::Light : theme == L"dark" ? ThemeSetting::Dark : ThemeSetting::Auto;
  s.allowRemoteImages = ReadBool(iniPath, L"allowRemoteImages", s.allowRemoteImages);
  s.mermaid = ReadBool(iniPath, L"mermaid", s.mermaid);
  s.devTools = ReadBool(iniPath, L"devTools", s.devTools);
  s.customCss = ReadString(iniPath, L"customCss", s.customCss);
  return s;
}

void Settings::Save(const std::wstring& iniPath) const {
  Write(iniPath, L"extensions", extensions);
  Write(iniPath, L"autoOpenPanel", autoOpenPanel);
  Write(iniPath, L"syncScroll", syncScroll);
  Write(iniPath, L"readingModeByDefault", readingModeByDefault);
  Write(iniPath, L"theme", std::wstring(ThemeName(theme)));
  Write(iniPath, L"allowRemoteImages", allowRemoteImages);
  Write(iniPath, L"mermaid", mermaid);
  Write(iniPath, L"devTools", devTools);
  Write(iniPath, L"customCss", customCss);
}

}  // namespace mdr
