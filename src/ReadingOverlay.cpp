#include "ReadingOverlay.h"

#include <commctrl.h>

namespace mdr {

namespace {

constexpr wchar_t kClassName[] = L"MarkdownReaderOverlay";
constexpr UINT_PTR kSubclassId = 0x4D445252;  // 'MDRR'
constexpr UINT kMsgTakeFocus = WM_APP + 0x31;

}  // namespace

ReadingOverlay::~ReadingOverlay() {
  Destroy();
}

bool ReadingOverlay::Create(HINSTANCE instance, HWND scintilla, Events events) {
  if (hwnd_) return true;
  events_ = std::move(events);
  scintilla_ = scintilla;

  WNDCLASSEXW wc{sizeof(wc)};
  if (!::GetClassInfoExW(instance, kClassName, &wc)) {
    wc = {sizeof(wc)};
    wc.lpfnWndProc = &ReadingOverlay::WindowProc;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!::RegisterClassExW(&wc)) return false;
  }

  hwnd_ = ::CreateWindowExW(0, kClassName, L"Markdown reading view", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0,
                            0, 0, ::GetParent(scintilla), nullptr, instance, this);
  if (!hwnd_) return false;

  // Siblings that overlap must both clip each other, or the editor would paint over us.
  const LONG_PTR style = ::GetWindowLongPtrW(scintilla_, GWL_STYLE);
  if (!(style & WS_CLIPSIBLINGS)) ::SetWindowLongPtrW(scintilla_, GWL_STYLE, style | WS_CLIPSIBLINGS);
  ::SetWindowSubclass(scintilla_, &ReadingOverlay::EditorSubclassProc, kSubclassId, reinterpret_cast<DWORD_PTR>(this));
  return true;
}

void ReadingOverlay::Destroy() {
  if (scintilla_) ::RemoveWindowSubclass(scintilla_, &ReadingOverlay::EditorSubclassProc, kSubclassId);
  if (hwnd_) ::DestroyWindow(hwnd_);
  if (background_) ::DeleteObject(background_);
  hwnd_ = nullptr;
  scintilla_ = nullptr;
  background_ = nullptr;
  active_ = false;
}

void ReadingOverlay::SetActive(bool active) {
  active_ = active;
  SyncToEditor();
}

void ReadingOverlay::SetBackground(COLORREF color) {
  if (background_) ::DeleteObject(background_);
  background_ = ::CreateSolidBrush(color);
  if (hwnd_) ::InvalidateRect(hwnd_, nullptr, TRUE);
}

void ReadingOverlay::SyncToEditor() {
  if (!hwnd_ || syncing_) return;
  syncing_ = true;
  const bool show = active_ && ::IsWindowVisible(scintilla_);
  if (show) {
    RECT rc{};
    ::GetWindowRect(scintilla_, &rc);
    ::MapWindowPoints(nullptr, ::GetParent(scintilla_), reinterpret_cast<POINT*>(&rc), 2);
    ::SetWindowPos(hwnd_, HWND_TOP, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
  } else if (::IsWindowVisible(hwnd_)) {
    ::ShowWindow(hwnd_, SW_HIDE);
  }
  syncing_ = false;
}

LRESULT CALLBACK ReadingOverlay::EditorSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR,
                                                    DWORD_PTR data) {
  const LRESULT result = ::DefSubclassProc(hwnd, message, wParam, lParam);
  auto* self = reinterpret_cast<ReadingOverlay*>(data);
  switch (message) {
    case WM_WINDOWPOSCHANGED:
    case WM_SHOWWINDOW:
      if (self->active_) self->SyncToEditor();
      break;
    case WM_SETFOCUS:
      // Something (tab switch, Find result, ...) focused the hidden editor;
      // keep keyboard focus in the reading view.
      if (self->active_ && self->hwnd_ && ::IsWindowVisible(self->hwnd_)) {
        ::PostMessageW(self->hwnd_, kMsgTakeFocus, 0, 0);
      }
      break;
    case WM_NCDESTROY:
      ::RemoveWindowSubclass(hwnd, &ReadingOverlay::EditorSubclassProc, kSubclassId);
      self->scintilla_ = nullptr;
      break;
    default:
      break;
  }
  return result;
}

LRESULT CALLBACK ReadingOverlay::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  auto* self = reinterpret_cast<ReadingOverlay*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (message) {
    case WM_NCCREATE: {
      const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
      ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
      break;
    }
    case WM_SIZE:
      if (self && self->events_.resized) self->events_.resized();
      return 0;
    case WM_SETFOCUS:
    case kMsgTakeFocus:
      if (self && self->active_ && self->events_.focused) self->events_.focused();
      return 0;
    case WM_ERASEBKGND:
      if (self && self->background_) {
        RECT rc{};
        ::GetClientRect(hwnd, &rc);
        ::FillRect(reinterpret_cast<HDC>(wParam), &rc, self->background_);
        return 1;
      }
      break;
    default:
      break;
  }
  return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace mdr
