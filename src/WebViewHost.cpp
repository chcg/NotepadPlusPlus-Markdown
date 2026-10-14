#include "WebViewHost.h"

#include <shlwapi.h>
#include <wrl.h>

#include <wil/resource.h>

#include "WebViewEnv.h"
#include "core/DocUrl.h"

namespace mdr {

using Microsoft::WRL::Callback;

namespace {

COREWEBVIEW2_COLOR ToColor(COLORREF color) {
  return {255, GetRValue(color), GetGValue(color), GetBValue(color)};
}

bool StartsWith(std::wstring_view text, std::wstring_view prefix) {
  return text.substr(0, prefix.size()) == prefix;
}

// Regular disk files only: no directories, pipes, devices or consoles.
wil::com_ptr<IStream> OpenDiskFile(const std::wstring& path) {
  {
    wil::unique_hfile file(::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file || ::GetFileType(file.get()) != FILE_TYPE_DISK) return nullptr;
  }
  wil::com_ptr<IStream> stream;
  if (FAILED(::SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_NONE, FILE_ATTRIBUTE_NORMAL, FALSE,
                                      nullptr, &stream))) {
    return nullptr;
  }
  return stream;
}

// Event handlers must never let an exception escape into WebView2/Notepad++.
template <typename F>
HRESULT Guarded(F&& body) {
  try {
    body();
  } catch (...) {
  }
  return S_OK;
}

}  // namespace

WebViewHost::WebViewHost(Delegate& delegate) : delegate_(delegate) {}

WebViewHost::~WebViewHost() {
  Close();
}

void WebViewHost::Create(HWND parent, WebViewEnv& environment, const Options& options) {
  if (state_ == State::Creating || state_ == State::Created) return;
  state_ = State::Creating;
  pageReady_ = false;
  parent_ = parent;
  options_ = options;
  const unsigned generation = ++generation_;
  std::weak_ptr<bool> alive = alive_;

  environment.Get([this, alive, generation](ICoreWebView2Environment* env, HRESULT result) {
    if (alive.expired() || generation != generation_) return;
    if (!env) {
      Fail(L"WebView2 could not be started", result);
      return;
    }
    environment_ = env;
    const HRESULT hr = env->CreateCoreWebView2Controller(
        parent_, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                     [this, alive, generation](HRESULT created, ICoreWebView2Controller* controller) -> HRESULT {
                       if (alive.expired() || generation != generation_) {
                         if (controller) controller->Close();
                         return S_OK;
                       }
                       if (FAILED(created) || !controller) {
                         Fail(L"The preview could not be created", FAILED(created) ? created : E_FAIL);
                         return S_OK;
                       }
                       return Guarded([&] { OnControllerCreated(controller); });
                     })
                     .Get());
    if (FAILED(hr)) Fail(L"The preview could not be created", hr);
  });
}

void WebViewHost::OnControllerCreated(ICoreWebView2Controller* controller) {
  controller_ = controller;
  controller_->get_CoreWebView2(&webview_);
  auto webview3 = webview_.try_query<ICoreWebView2_3>();
  if (!webview3) {
    Fail(L"The installed WebView2 runtime is too old", E_NOINTERFACE);
    return;
  }
  state_ = State::Created;

  ConfigureSettings();
  RegisterHandlers();
  webview3->SetVirtualHostNameToFolderMapping(std::wstring(kAppHost).c_str(), options_.webRoot.c_str(),
                                              COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY);
  webview_->AddWebResourceRequestedFilter((std::wstring(kDocOrigin) + L"/*").c_str(), COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);

  SetTheme(dark_, options_.background);
  FitToParent();
  controller_->put_IsVisible(visible_ ? TRUE : FALSE);
  webview_->Navigate(std::wstring(kPageUrl).c_str());
  if (focusWhenCreated_) Focus();
}

void WebViewHost::ConfigureSettings() {
  wil::com_ptr<ICoreWebView2Settings> settings;
  if (FAILED(webview_->get_Settings(&settings))) return;
  settings->put_IsScriptEnabled(TRUE);  // our page script; document content is sanitized and CSP-restricted
  settings->put_IsWebMessageEnabled(TRUE);
  settings->put_AreHostObjectsAllowed(FALSE);
  settings->put_AreDefaultScriptDialogsEnabled(FALSE);
  settings->put_IsStatusBarEnabled(FALSE);
  settings->put_IsZoomControlEnabled(TRUE);
  settings->put_AreDefaultContextMenusEnabled(TRUE);
  settings->put_AreDevToolsEnabled(options_.devTools ? TRUE : FALSE);
  if (auto settings3 = settings.try_query<ICoreWebView2Settings3>()) {
    // Browser shortcuts (F5, Ctrl+P, Ctrl+F, ...) are off; Notepad++ shortcuts are forwarded instead.
    settings3->put_AreBrowserAcceleratorKeysEnabled(options_.devTools ? TRUE : FALSE);
  }
  if (auto settings4 = settings.try_query<ICoreWebView2Settings4>()) {
    settings4->put_IsPasswordAutosaveEnabled(FALSE);
    settings4->put_IsGeneralAutofillEnabled(FALSE);
  }
  if (auto settings6 = settings.try_query<ICoreWebView2Settings6>()) {
    settings6->put_IsSwipeNavigationEnabled(FALSE);
  }
}

void WebViewHost::RegisterHandlers() {
  EventRegistrationToken token{};

  // The page is the only document this WebView may ever show.
  webview_->add_NavigationStarting(
      Callback<ICoreWebView2NavigationStartingEventHandler>(
          [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
            return Guarded([&] {
              wil::unique_cotaskmem_string uri;
              if (FAILED(args->get_Uri(&uri)) || !uri || std::wstring_view(uri.get()) != kPageUrl) {
                args->put_Cancel(TRUE);
              }
            });
          })
          .Get(),
      &token);

  webview_->add_FrameNavigationStarting(
      Callback<ICoreWebView2NavigationStartingEventHandler>(
          [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
            return args->put_Cancel(TRUE);
          })
          .Get(),
      &token);

  webview_->add_NewWindowRequested(
      Callback<ICoreWebView2NewWindowRequestedEventHandler>(
          [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
            return args->put_Handled(TRUE);  // no window is created
          })
          .Get(),
      &token);

  webview_->add_PermissionRequested(
      Callback<ICoreWebView2PermissionRequestedEventHandler>(
          [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
            return args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
          })
          .Get(),
      &token);

  if (auto webview4 = webview_.try_query<ICoreWebView2_4>()) {
    webview4->add_DownloadStarting(
        Callback<ICoreWebView2DownloadStartingEventHandler>(
            [](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* args) -> HRESULT { return args->put_Cancel(TRUE); })
            .Get(),
        &token);
  }

  if (auto webview18 = webview_.try_query<ICoreWebView2_18>()) {
    webview18->add_LaunchingExternalUriScheme(
        Callback<ICoreWebView2LaunchingExternalUriSchemeEventHandler>(
            [](ICoreWebView2*, ICoreWebView2LaunchingExternalUriSchemeEventArgs* args) -> HRESULT {
              return args->put_Cancel(TRUE);
            })
            .Get(),
        &token);
  }

  webview_->add_WebMessageReceived(
      Callback<ICoreWebView2WebMessageReceivedEventHandler>(
          [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
            return Guarded([&] {
              wil::unique_cotaskmem_string source;
              if (FAILED(args->get_Source(&source)) || !source ||
                  !StartsWith(source.get(), std::wstring(kAppOrigin) + L"/")) {
                return;
              }
              wil::unique_cotaskmem_string json;
              if (FAILED(args->get_WebMessageAsJson(&json)) || !json) return;
              auto message = ParsePageMessage(json.get());
              if (!message) return;
              if (message->type == PageMessage::Type::Ready) {
                pageReady_ = true;
                delegate_.OnPageReady(*this);
              } else {
                delegate_.OnPageMessage(*this, std::move(*message));
              }
            });
          })
          .Get(),
      &token);

  webview_->add_WebResourceRequested(
      Callback<ICoreWebView2WebResourceRequestedEventHandler>(
          [this](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
            return Guarded([&] { ServeDocumentResource(args); });
          })
          .Get(),
      &token);

  webview_->add_ProcessFailed(
      Callback<ICoreWebView2ProcessFailedEventHandler>(
          [this](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT {
            return Guarded([&] {
              COREWEBVIEW2_PROCESS_FAILED_KIND kind{};
              args->get_ProcessFailedKind(&kind);
              if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED ||
                  kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE) {
                pageReady_ = false;
                webview_->Reload();
              } else if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED) {
                Fail(L"The preview's browser process exited", E_FAIL);
              }
            });
          })
          .Get(),
      &token);

  controller_->add_AcceleratorKeyPressed(
      Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
          [this](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
            return Guarded([&] { ForwardAccelerator(args); });
          })
          .Get(),
      &token);

  controller_->add_MoveFocusRequested(
      Callback<ICoreWebView2MoveFocusRequestedEventHandler>(
          [this](ICoreWebView2Controller*, ICoreWebView2MoveFocusRequestedEventArgs* args) -> HRESULT {
            return Guarded([&] {
              args->put_Handled(TRUE);
              delegate_.OnFocusLeaving(*this);
            });
          })
          .Get(),
      &token);
}

void WebViewHost::ServeDocumentResource(ICoreWebView2WebResourceRequestedEventArgs* args) {
  wil::com_ptr<ICoreWebView2WebResourceRequest> request;
  wil::unique_cotaskmem_string uri;
  if (FAILED(args->get_Request(&request)) || FAILED(request->get_Uri(&uri)) || !uri) return;

  wil::com_ptr<IStream> stream;
  std::wstring mime;
  if (auto path = FilePathFromDocUrl(uri.get())) {
    if (auto type = PreviewMimeType(*path)) {
      stream = OpenDiskFile(*path);
      mime = *type;
    }
  }

  wil::com_ptr<ICoreWebView2WebResourceResponse> response;
  if (stream) {
    const std::wstring headers =
        L"Content-Type: " + mime + L"\r\nCache-Control: no-cache\r\nX-Content-Type-Options: nosniff";
    environment_->CreateWebResourceResponse(stream.get(), 200, L"OK", headers.c_str(), &response);
  } else {
    environment_->CreateWebResourceResponse(nullptr, 404, L"Not Found", L"", &response);
  }
  if (response) args->put_Response(response.get());
}

// Notepad++ shortcuts must keep working while the preview has focus. Keys
// with Ctrl/Alt and function keys are re-posted to the Notepad++ window, where
// its message loop runs them through its accelerator table, so the user's own
// shortcut mapping (including this plugin's commands) applies.
void WebViewHost::ForwardAccelerator(ICoreWebView2AcceleratorKeyPressedEventArgs* args) {
  COREWEBVIEW2_KEY_EVENT_KIND kind{};
  args->get_KeyEventKind(&kind);
  if (kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN && kind != COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) return;
  if (!options_.keyTarget) return;

  UINT key = 0;
  args->get_VirtualKey(&key);
  if (key == VK_CONTROL || key == VK_MENU || key == VK_SHIFT || key == VK_LWIN || key == VK_RWIN) return;

  const bool ctrl = ::GetKeyState(VK_CONTROL) < 0;
  const bool alt = kind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN || ::GetKeyState(VK_MENU) < 0;

  bool forward = false;
  if (key >= VK_F1 && key <= VK_F24) {
    forward = !(key == VK_F12 && options_.devTools);
  } else if (ctrl || alt) {
    // Copy and select-all act on the preview itself.
    const bool pageKey = ctrl && !alt && (key == 'C' || key == 'A' || key == VK_INSERT);
    forward = !pageKey;
  }
  if (!forward) return;

  COREWEBVIEW2_PHYSICAL_KEY_STATUS status{};
  args->get_PhysicalKeyStatus(&status);
  const LPARAM lParam = static_cast<LPARAM>((status.RepeatCount & 0xFFFF) | ((status.ScanCode & 0xFF) << 16) |
                                            (status.IsExtendedKey ? (1u << 24) : 0u) | (alt ? (1u << 29) : 0u) |
                                            (status.WasKeyDown ? (1u << 30) : 0u));
  args->put_Handled(TRUE);
  ::PostMessageW(options_.keyTarget, kind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN ? WM_SYSKEYDOWN : WM_KEYDOWN,
                 key, lParam);
}

void WebViewHost::Fail(const wchar_t* what, HRESULT result) {
  state_ = State::Failed;
  pageReady_ = false;
  delegate_.OnHostFailed(*this, what, result);
}

void WebViewHost::Close() {
  ++generation_;
  if (controller_) controller_->Close();
  controller_.reset();
  webview_.reset();
  environment_.reset();
  state_ = State::None;
  pageReady_ = false;
}

void WebViewHost::FitToParent() {
  if (!controller_ || !parent_) return;
  RECT bounds{};
  ::GetClientRect(parent_, &bounds);
  controller_->put_Bounds(bounds);
}

void WebViewHost::SetVisible(bool visible) {
  visible_ = visible;
  if (controller_) controller_->put_IsVisible(visible ? TRUE : FALSE);
}

void WebViewHost::Focus() {
  if (controller_) {
    focusWhenCreated_ = false;
    controller_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
  } else {
    focusWhenCreated_ = true;
  }
}

void WebViewHost::NotifyParentMoved() {
  if (controller_) controller_->NotifyParentWindowPositionChanged();
}

void WebViewHost::SetTheme(bool dark, COLORREF background) {
  dark_ = dark;
  options_.background = background;
  if (!controller_) return;
  if (auto controller2 = controller_.try_query<ICoreWebView2Controller2>()) {
    controller2->put_DefaultBackgroundColor(ToColor(background));
  }
  if (auto webview13 = webview_.try_query<ICoreWebView2_13>()) {
    wil::com_ptr<ICoreWebView2Profile> profile;
    if (SUCCEEDED(webview13->get_Profile(&profile)) && profile) {
      profile->put_PreferredColorScheme(dark ? COREWEBVIEW2_PREFERRED_COLOR_SCHEME_DARK
                                             : COREWEBVIEW2_PREFERRED_COLOR_SCHEME_LIGHT);
    }
  }
}

void WebViewHost::Post(const std::wstring& json) {
  if (webview_ && pageReady_) webview_->PostWebMessageAsJson(json.c_str());
}

}  // namespace mdr
