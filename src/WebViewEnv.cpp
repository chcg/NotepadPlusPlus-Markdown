#include "WebViewEnv.h"

#include <shlobj.h>
#include <wrl.h>

namespace mdr {

using Microsoft::WRL::Callback;

void WebViewEnv::Get(Ready callback) {
  if (environment_ || FAILED(error_)) {
    callback(environment_.get(), error_);
    return;
  }
  waiting_.push_back(std::move(callback));
  if (creating_) return;
  creating_ = true;

  ::SHCreateDirectoryExW(nullptr, userDataFolder_.c_str(), nullptr);
  const HRESULT hr = ::CreateCoreWebView2EnvironmentWithOptions(
      nullptr, userDataFolder_.empty() ? nullptr : userDataFolder_.c_str(), nullptr,
      Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
          [this](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
            Complete(environment, SUCCEEDED(result) && !environment ? E_FAIL : result);
            return S_OK;
          })
          .Get());
  if (FAILED(hr)) Complete(nullptr, hr);
}

void WebViewEnv::Complete(ICoreWebView2Environment* environment, HRESULT result) {
  creating_ = false;
  environment_ = environment;
  error_ = result;
  auto waiting = std::move(waiting_);
  waiting_.clear();
  for (auto& callback : waiting) callback(environment_.get(), error_);
  // A failure is not sticky across sessions, but within one session we do
  // not retry: the runtime is missing or the folder is unusable.
}

std::wstring WebViewEnv::RuntimeVersion() {
  wil::unique_cotaskmem_string version;
  if (FAILED(::GetAvailableCoreWebView2BrowserVersionString(nullptr, &version)) || !version) return {};
  return version.get();
}

void WebViewEnv::Reset() {
  environment_.reset();
  waiting_.clear();
  creating_ = false;
  error_ = S_OK;
}

}  // namespace mdr
