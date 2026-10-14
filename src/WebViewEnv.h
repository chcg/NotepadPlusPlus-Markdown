#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include <WebView2.h>
#include <wil/com.h>

namespace mdr {

// One WebView2 environment shared by every preview surface (panel and
// reading-mode overlays), created lazily on first use.
//
// The user data folder is explicit: the default would be next to
// notepad++.exe, which is not writable under Program Files. Environment
// options are left at their defaults so that every Notepad++ instance
// (-multiInst) creates an identical environment and can share the folder.
class WebViewEnv {
 public:
  using Ready = std::function<void(ICoreWebView2Environment* environment, HRESULT result)>;

  void SetUserDataFolder(std::wstring folder) { userDataFolder_ = std::move(folder); }

  // Calls back on the UI thread once the environment exists (or failed).
  void Get(Ready callback);

  // Installed WebView2 runtime version, or empty if there is none.
  static std::wstring RuntimeVersion();

  void Reset();

 private:
  void Complete(ICoreWebView2Environment* environment, HRESULT result);

  std::wstring userDataFolder_;
  wil::com_ptr<ICoreWebView2Environment> environment_;
  HRESULT error_ = S_OK;
  bool creating_ = false;
  std::vector<Ready> waiting_;
};

}  // namespace mdr
