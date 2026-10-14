#pragma once

#include <windows.h>

#include <memory>
#include <string>

#include <WebView2.h>
#include <wil/com.h>

#include "core/PageMessages.h"

namespace mdr {

class WebViewEnv;

// One WebView2 control showing the preview page inside a parent window
// (the docking panel or a reading-mode overlay).
//
// Security policy enforced here:
//  * the only navigation allowed is to the bundled page itself; links are
//    handled by the page posting them to the plugin (see LinkPolicy);
//  * no new windows, downloads, permission prompts or external URI launches;
//  * files from disk are served only on the doc origin, only for media types
//    in PreviewMimeType, and only from regular disk files;
//  * messages are accepted only from the page's own origin and parsed strictly.
class WebViewHost {
 public:
  class Delegate {
   public:
    virtual void OnPageReady(WebViewHost& host) = 0;
    virtual void OnPageMessage(WebViewHost& host, PageMessage message) = 0;
    virtual void OnFocusLeaving(WebViewHost& host) = 0;
    virtual void OnHostFailed(WebViewHost& host, const std::wstring& what, HRESULT result) = 0;

   protected:
    ~Delegate() = default;
  };

  struct Options {
    std::wstring webRoot;          // folder with index.html (plugins\MarkdownReader\web)
    HWND keyTarget = nullptr;      // Notepad++ main window: receives forwarded shortcuts
    bool devTools = false;
    COLORREF background = RGB(255, 255, 255);
  };

  explicit WebViewHost(Delegate& delegate);
  ~WebViewHost();
  WebViewHost(const WebViewHost&) = delete;
  WebViewHost& operator=(const WebViewHost&) = delete;

  // Asynchronous; the delegate's OnPageReady fires when the page can take messages.
  void Create(HWND parent, WebViewEnv& environment, const Options& options);
  void Close();

  bool IsCreating() const { return state_ == State::Creating; }
  bool IsCreated() const { return state_ == State::Created; }
  bool IsPageReady() const { return pageReady_; }
  HWND Parent() const { return parent_; }

  void FitToParent();
  void SetVisible(bool visible);
  void Focus();
  void NotifyParentMoved();
  void SetTheme(bool dark, COLORREF background);

  // Dropped while the page is not ready; the delegate re-sends state from OnPageReady.
  void Post(const std::wstring& json);

  ICoreWebView2* WebView() const { return webview_.get(); }
  ICoreWebView2Environment* Environment() const { return environment_.get(); }

 private:
  enum class State { None, Creating, Created, Failed };

  void OnControllerCreated(ICoreWebView2Controller* controller);
  void ConfigureSettings();
  void RegisterHandlers();
  void ServeDocumentResource(ICoreWebView2WebResourceRequestedEventArgs* args);
  void ForwardAccelerator(ICoreWebView2AcceleratorKeyPressedEventArgs* args);
  void Fail(const wchar_t* what, HRESULT result);

  Delegate& delegate_;
  Options options_;
  State state_ = State::None;
  unsigned generation_ = 0;  // invalidates callbacks from an earlier Create()
  bool pageReady_ = false;
  bool visible_ = true;
  bool focusWhenCreated_ = false;
  bool dark_ = false;
  HWND parent_ = nullptr;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  wil::com_ptr<ICoreWebView2Environment> environment_;
  wil::com_ptr<ICoreWebView2Controller> controller_;
  wil::com_ptr<ICoreWebView2> webview_;
};

}  // namespace mdr
