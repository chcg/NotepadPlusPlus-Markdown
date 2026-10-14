#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "PluginInterface.h"
#include "PreviewPanel.h"
#include "ReadingOverlay.h"
#include "Settings.h"
#include "WebViewEnv.h"
#include "WebViewHost.h"

namespace mdr {

// Owns all plugin state and wires Notepad++ events to the preview surfaces:
// the docking panel (follows the active document) and one reading-mode
// overlay per editor view.
class Plugin final : public WebViewHost::Delegate {
 public:
  static Plugin& Instance();

  void Attach(HINSTANCE module) { module_ = module; }
  void SetInfo(const NppData& data);
  FuncItem* Commands(int* count);
  void OnNotification(SCNotification* notification);

  // Plugins menu commands
  void TogglePanel();
  void ToggleReadingMode();
  void ToggleSyncScroll();
  void ToggleReadingByDefault();
  void ExportHtml();
  void ExportPdf();
  void Print();
  void CopyHtml();
  void OpenSettings();
  void ShowAbout();

 private:
  // Order defines the Plugins menu. kCmdPanel must stay first: Notepad++
  // restores the docked panel by running the command at that index.
  enum Command : int {
    kCmdPanel,
    kCmdReading,
    kCmdSep1,
    kCmdSyncScroll,
    kCmdReadingByDefault,
    kCmdSep2,
    kCmdExportHtml,
    kCmdExportPdf,
    kCmdPrint,
    kCmdCopyHtml,
    kCmdSep3,
    kCmdSettings,
    kCmdAbout,
    kCommandCount
  };

  struct BufferState {
    bool seen = false;
    bool reading = false;  // reading mode on for this document
    bool forced = false;   // render even though it is not detected as Markdown
  };

  struct ReadingView {
    ReadingOverlay overlay;
    std::unique_ptr<WebViewHost> host;
    UINT_PTR bufferId = 0;
  };

  enum class ExportKind { SaveHtml, CopyHtml };

  Plugin() = default;

  // Notepad++ events
  void OnReady();
  void OnShutdown();
  void OnActiveBufferChanged();
  void OnDocumentModified(HWND scintilla);
  void OnEditorScrolled(HWND scintilla);
  void OnFileSaved(UINT_PTR bufferId);
  void OnThemeChanged();

  // Surfaces
  void ShowPanel(bool show);
  bool PanelOpen() const;
  void EnsurePanelHost();
  void RefreshPanel(bool reset);
  void EnsureReadingView(int view);
  void UpdateReadingView(int view, UINT_PTR bufferId, bool active);
  void ScheduleRender();
  void RenderActiveDocument();
  void RenderTo(WebViewHost& host, UINT_PTR bufferId, HWND scintilla, bool reset);
  WebViewHost* HostShowingActiveDocument();
  int ViewOf(const WebViewHost& host) const;  // -1 for the panel

  // Helpers
  bool IsMarkdown(UINT_PTR bufferId);
  bool IsDark() const;
  COLORREF Background() const;
  WebViewHost::Options HostOptions() const;
  std::wstring SettingsMessage() const;
  void PushSettings();
  void ReloadSettings();
  void UpdateMenuChecks();
  void OpenLink(const std::wstring& url);
  void RequestExport(ExportKind kind);
  void FinishExport(int id, const std::string& html);
  void Defer(std::function<void()> task);
  void ReportError(const std::wstring& what, HRESULT result);

  // WebViewHost::Delegate
  void OnPageReady(WebViewHost& host) override;
  void OnPageMessage(WebViewHost& host, PageMessage message) override;
  void OnFocusLeaving(WebViewHost& host) override;
  void OnHostFailed(WebViewHost& host, const std::wstring& what, HRESULT result) override;

  static LRESULT CALLBACK MessageWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

  HINSTANCE module_ = nullptr;
  HWND messageWindow_ = nullptr;
  bool ready_ = false;
  std::array<FuncItem, kCommandCount> commands_{};

  std::wstring configDir_;
  std::wstring iniPath_;
  std::wstring webRoot_;
  Settings settings_;
  std::vector<std::wstring> extensions_;

  WebViewEnv environment_;
  PreviewPanel panel_;
  std::unique_ptr<WebViewHost> panelHost_;
  UINT_PTR panelBufferId_ = 0;   // document the panel currently shows (0 = placeholder)
  bool panelDismissed_ = false;  // user closed the panel; don't auto-open it again this session
  std::array<ReadingView, 2> reading_;
  std::map<UINT_PTR, BufferState> buffers_;

  int64_t renderSeq_ = 0;
  ULONGLONG ignoreEditorScrollUntil_ = 0;
  int exportSeq_ = 0;
  std::map<int, ExportKind> pendingExports_;
  std::vector<std::function<void()>> deferred_;
  bool errorShown_ = false;
};

}  // namespace mdr
