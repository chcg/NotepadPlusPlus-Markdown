// The six exports Notepad++ looks up in every plugin DLL.

#include <windows.h>

#include "Plugin.h"
#include "PluginInterface.h"

BOOL APIENTRY DllMain(HINSTANCE module, DWORD reason, LPVOID /*reserved*/) {
  if (reason == DLL_PROCESS_ATTACH) {
    ::DisableThreadLibraryCalls(module);
    mdr::Plugin::Instance().Attach(module);
  }
  return TRUE;
}

extern "C" __declspec(dllexport) BOOL isUnicode() {
  return TRUE;
}

extern "C" __declspec(dllexport) void setInfo(NppData data) {
  mdr::Plugin::Instance().SetInfo(data);
}

extern "C" __declspec(dllexport) const wchar_t* getName() {
  return L"MarkdownReader";
}

extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* count) {
  return mdr::Plugin::Instance().Commands(count);
}

extern "C" __declspec(dllexport) void beNotified(SCNotification* notification) {
  mdr::Plugin::Instance().OnNotification(notification);
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT /*message*/, WPARAM /*wParam*/, LPARAM /*lParam*/) {
  return TRUE;
}
