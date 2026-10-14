#pragma once

#include <windows.h>

#include <functional>

namespace mdr {

// Reading mode: a window laid exactly over one editor view's Scintilla
// control, hosting a WebView with the rendered document.
//
// The overlay is a sibling of the Scintilla window (same parent, above it in
// z-order) rather than replacing or hiding it: Notepad++ uses the editor's
// visibility to manage views, so the editor stays where it is, underneath.
// A subclass on the Scintilla window keeps the overlay's position in sync.
class ReadingOverlay {
 public:
  struct Events {
    std::function<void()> resized;
    std::function<void()> focused;
  };

  ReadingOverlay() = default;
  ~ReadingOverlay();
  ReadingOverlay(const ReadingOverlay&) = delete;
  ReadingOverlay& operator=(const ReadingOverlay&) = delete;

  bool Create(HINSTANCE instance, HWND scintilla, Events events);
  void Destroy();

  void SetActive(bool active);  // shown whenever active and the editor itself is visible
  bool IsActive() const { return active_; }
  bool IsCreated() const { return hwnd_ != nullptr; }
  HWND Hwnd() const { return hwnd_; }
  void SetBackground(COLORREF color);

 private:
  void SyncToEditor();
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
  static LRESULT CALLBACK EditorSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id,
                                             DWORD_PTR data);

  HWND hwnd_ = nullptr;
  HWND scintilla_ = nullptr;
  HBRUSH background_ = nullptr;
  bool active_ = false;
  bool syncing_ = false;
  Events events_;
};

}  // namespace mdr
