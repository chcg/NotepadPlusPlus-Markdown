#include "NppApi.h"

#include <cwchar>

#include "Notepad_plus_msgs.h"
#include "Scintilla.h"

namespace mdr::npp {

namespace {

NppData g_data;

// Two-call pattern used by several NPPM_* string getters: first call with a
// null buffer returns the length.
template <typename Send>
std::wstring GetString(Send send) {
  const LRESULT length = send(0, nullptr);
  if (length <= 0) return {};
  std::wstring text(static_cast<size_t>(length) + 1, L'\0');
  send(static_cast<WPARAM>(text.size()), text.data());
  text.resize(wcsnlen(text.c_str(), text.size()));
  return text;
}

}  // namespace

void Init(const NppData& data) {
  g_data = data;
}

const NppData& Data() {
  return g_data;
}

HWND Window() {
  return g_data._nppHandle;
}

int CurrentView() {
  return static_cast<int>(::SendMessage(g_data._nppHandle, NPPM_GETCURRENTVIEW, 0, 0));
}

HWND ScintillaForView(int view) {
  return view == SUB_VIEW ? g_data._scintillaSecondHandle : g_data._scintillaMainHandle;
}

HWND CurrentScintilla() {
  int which = 0;
  ::SendMessage(g_data._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&which));
  return ScintillaForView(which);
}

UINT_PTR CurrentBufferId() {
  return static_cast<UINT_PTR>(::SendMessage(g_data._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0));
}

UINT_PTR BufferInView(int view) {
  const auto index = ::SendMessage(g_data._nppHandle, NPPM_GETCURRENTDOCINDEX, 0, view);
  if (index < 0) return 0;
  return static_cast<UINT_PTR>(::SendMessage(g_data._nppHandle, NPPM_GETBUFFERIDFROMPOS, static_cast<WPARAM>(index), view));
}

std::wstring BufferPath(UINT_PTR bufferId) {
  return GetString([&](WPARAM, wchar_t* buffer) {
    return ::SendMessage(g_data._nppHandle, NPPM_GETFULLPATHFROMBUFFERID, bufferId, reinterpret_cast<LPARAM>(buffer));
  });
}

int CurrentLangType() {
  int langType = L_TEXT;
  ::SendMessage(g_data._nppHandle, NPPM_GETCURRENTLANGTYPE, 0, reinterpret_cast<LPARAM>(&langType));
  return langType;
}

std::wstring LanguageName(int langType) {
  return GetString([&](WPARAM, wchar_t* buffer) {
    return ::SendMessage(g_data._nppHandle, NPPM_GETLANGUAGENAME, static_cast<WPARAM>(langType), reinterpret_cast<LPARAM>(buffer));
  });
}

std::wstring PluginsConfigDir() {
  return GetString([&](WPARAM size, wchar_t* buffer) {
    return ::SendMessage(g_data._nppHandle, NPPM_GETPLUGINSCONFIGDIR, size, reinterpret_cast<LPARAM>(buffer));
  });
}

bool IsDarkModeEnabled() {
  return ::SendMessage(g_data._nppHandle, NPPM_ISDARKMODEENABLED, 0, 0) != FALSE;
}

bool OpenFile(const std::wstring& path) {
  return ::SendMessage(g_data._nppHandle, NPPM_DOOPEN, 0, reinterpret_cast<LPARAM>(path.c_str())) != FALSE;
}

void SetMenuCheck(int cmdId, bool checked) {
  ::SendMessage(g_data._nppHandle, NPPM_SETMENUITEMCHECK, static_cast<WPARAM>(cmdId), checked ? TRUE : FALSE);
}

LRESULT Sci(HWND sci, UINT message, WPARAM wParam, LPARAM lParam) {
  return ::SendMessage(sci, message, wParam, lParam);
}

size_t DocumentLength(HWND sci) {
  return static_cast<size_t>(Sci(sci, SCI_GETLENGTH));
}

std::string DocumentBytes(HWND sci) {
  const size_t length = DocumentLength(sci);
  if (length == 0) return {};
  // Moves the gap so the buffer is contiguous; copy immediately.
  const auto* data = reinterpret_cast<const char*>(Sci(sci, SCI_GETCHARACTERPOINTER));
  return data ? std::string(data, length) : std::string();
}

unsigned CodePage(HWND sci) {
  return static_cast<unsigned>(Sci(sci, SCI_GETCODEPAGE));
}

int FirstVisibleDocLine(HWND sci) {
  const auto visible = Sci(sci, SCI_GETFIRSTVISIBLELINE);
  return static_cast<int>(Sci(sci, SCI_DOCLINEFROMVISIBLE, static_cast<WPARAM>(visible)));
}

void ScrollToDocLine(HWND sci, int line) {
  const auto visible = Sci(sci, SCI_VISIBLEFROMDOCLINE, static_cast<WPARAM>(line));
  Sci(sci, SCI_SETFIRSTVISIBLELINE, static_cast<WPARAM>(visible));
}

void GotoDocLine(HWND sci, int line) {
  Sci(sci, SCI_ENSUREVISIBLE, static_cast<WPARAM>(line));
  Sci(sci, SCI_GOTOLINE, static_cast<WPARAM>(line));
  ScrollToDocLine(sci, line > 3 ? line - 3 : 0);
}

}  // namespace mdr::npp
