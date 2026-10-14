#pragma once

// Thin typed wrappers over the Notepad++ (NPPM_*) and Scintilla (SCI_*)
// messages the plugin uses.

#include <windows.h>

#include <string>

#include "PluginInterface.h"

namespace mdr::npp {

void Init(const NppData& data);
const NppData& Data();
HWND Window();

int CurrentView();                // 0 = main, 1 = sub
HWND ScintillaForView(int view);
HWND CurrentScintilla();
UINT_PTR CurrentBufferId();
UINT_PTR BufferInView(int view);             // 0 if the view is hidden
std::wstring BufferPath(UINT_PTR bufferId);  // "new 1" for untitled buffers
int CurrentLangType();
std::wstring LanguageName(int langType);     // for L_USER: "udf - <UDL name>" of the current buffer
std::wstring PluginsConfigDir();
bool IsDarkModeEnabled();
bool OpenFile(const std::wstring& path);
void SetMenuCheck(int cmdId, bool checked);

// ---- Scintilla --------------------------------------------------------------------

LRESULT Sci(HWND sci, UINT message, WPARAM wParam = 0, LPARAM lParam = 0);
std::string DocumentBytes(HWND sci);  // raw buffer in the document's code page
unsigned CodePage(HWND sci);
size_t DocumentLength(HWND sci);
int FirstVisibleDocLine(HWND sci);
void ScrollToDocLine(HWND sci, int line);
void GotoDocLine(HWND sci, int line);

}  // namespace mdr::npp
