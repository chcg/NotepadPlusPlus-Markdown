#include "Plugin.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <wil/com.h>
#include <wil/resource.h>
#include <wrl.h>

#include "NppApi.h"
#include "Notepad_plus_msgs.h"
#include "Scintilla.h"
#include "core/ClipboardHtml.h"
#include "core/DocUrl.h"
#include "core/LinkPolicy.h"
#include "core/MarkdownDetect.h"
#include "core/PageMessages.h"
#include "core/TextConvert.h"
#include "resource.h"

namespace mdr {

namespace {

constexpr wchar_t kPluginName[] = L"MarkdownReader";
constexpr wchar_t kMessageWindowClass[] = L"MarkdownReaderMessages";
constexpr UINT kMsgRunDeferred = WM_APP + 1;
constexpr UINT_PTR kRenderTimer = 1;

constexpr COLORREF kLightBackground = RGB(255, 255, 255);
constexpr COLORREF kDarkBackground = RGB(13, 17, 23);  // github-markdown-css dark

constexpr char kPlaceholder[] =
    "> **MarkdownReader** - the active document is not Markdown.\n"
    ">\n"
    "> Open a `.md` file, choose *Language > Markdown* for this document,\n"
    "> or use *Plugins > MarkdownReader > Toggle Reading Mode* to render it anyway.\n";

bool FileExists(const std::wstring& path) {
  const DWORD attributes = ::GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring ModuleDirectory(HINSTANCE module) {
  std::wstring path(MAX_PATH, L'\0');
  while (true) {
    const DWORD length = ::GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (length < path.size()) {
      path.resize(length);
      break;
    }
    path.resize(path.size() * 2);
  }
  return DirectoryOf(path);
}

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
  wil::unique_cotaskmem_string path;
  if (FAILED(::SHGetKnownFolderPath(id, 0, nullptr, &path))) return {};
  return path.get();
}

bool StartsWithNoCase(const std::wstring& text, const std::wstring& prefix) {
  return text.size() >= prefix.size() && ToLower(text.substr(0, prefix.size())) == ToLower(prefix);
}

std::string ReadSmallUtf8File(const std::wstring& path, size_t maxBytes) {
  wil::unique_hfile file(::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  LARGE_INTEGER size{};
  if (!file || !::GetFileSizeEx(file.get(), &size) || size.QuadPart <= 0 ||
      static_cast<unsigned long long>(size.QuadPart) > maxBytes) {
    return {};
  }
  std::string data(static_cast<size_t>(size.QuadPart), '\0');
  DWORD read = 0;
  if (!::ReadFile(file.get(), data.data(), static_cast<DWORD>(data.size()), &read, nullptr)) return {};
  data.resize(read);
  if (data.rfind("\xEF\xBB\xBF", 0) == 0) data.erase(0, 3);
  return data;
}

bool WriteUtf8File(const std::wstring& path, const std::string& data) {
  wil::unique_hfile file(
      ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (!file) return false;
  DWORD written = 0;
  return ::WriteFile(file.get(), data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
         written == data.size();
}

std::wstring FileNameWithoutExtension(const std::wstring& path) {
  const size_t slash = path.find_last_of(L"\\/");
  std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
  const size_t dot = name.find_last_of(L'.');
  if (dot != std::wstring::npos && dot > 0) name.resize(dot);
  return name.empty() ? std::wstring(L"document") : name;
}

std::wstring AskSavePath(HWND owner, const wchar_t* typeName, const wchar_t* pattern, const wchar_t* extension,
                         const std::wstring& folder, const std::wstring& fileName) {
  wil::com_ptr<IFileSaveDialog> dialog;
  if (FAILED(::CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return {};
  const COMDLG_FILTERSPEC filter{typeName, pattern};
  dialog->SetFileTypes(1, &filter);
  dialog->SetDefaultExtension(extension);
  dialog->SetFileName(fileName.c_str());
  if (!folder.empty()) {
    wil::com_ptr<IShellItem> item;
    if (SUCCEEDED(::SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&item)))) dialog->SetFolder(item.get());
  }
  if (FAILED(dialog->Show(owner))) return {};
  wil::com_ptr<IShellItem> result;
  wil::unique_cotaskmem_string path;
  if (FAILED(dialog->GetResult(&result)) || FAILED(result->GetDisplayName(SIGDN_FILESYSPATH, &path))) return {};
  return path.get();
}

bool SetClipboard(HWND owner, const std::string& cfHtml, const std::wstring& plainText) {
  if (!::OpenClipboard(owner)) return false;
  ::EmptyClipboard();
  bool ok = true;
  if (const UINT htmlFormat = ::RegisterClipboardFormatW(L"HTML Format")) {
    if (HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, cfHtml.size() + 1)) {
      memcpy(::GlobalLock(memory), cfHtml.c_str(), cfHtml.size() + 1);
      ::GlobalUnlock(memory);
      if (!::SetClipboardData(htmlFormat, memory)) {
        ::GlobalFree(memory);
        ok = false;
      }
    }
  }
  const size_t bytes = (plainText.size() + 1) * sizeof(wchar_t);
  if (HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, bytes)) {
    memcpy(::GlobalLock(memory), plainText.c_str(), bytes);
    ::GlobalUnlock(memory);
    if (!::SetClipboardData(CF_UNICODETEXT, memory)) ::GlobalFree(memory);
  }
  ::CloseClipboard();
  return ok;
}

// FuncItem callbacks are plain function pointers.
void CmdTogglePanel();
void CmdToggleReading();
void CmdToggleSyncScroll();
void CmdToggleReadingByDefault();
void CmdExportHtml();
void CmdExportPdf();
void CmdPrint();
void CmdCopyHtml();
void CmdSettings();
void CmdAbout();

}  // namespace

Plugin& Plugin::Instance() {
  static Plugin instance;
  return instance;
}

// ---- setup ---------------------------------------------------------------------------

void Plugin::SetInfo(const NppData& data) {
  npp::Init(data);

  configDir_ = npp::PluginsConfigDir();
  iniPath_ = configDir_ + L"\\MarkdownReader.ini";
  webRoot_ = ModuleDirectory(module_) + L"\\web";
  settings_ = Settings::Load(iniPath_);
  extensions_ = ParseExtensionList(settings_.extensions);

  // Portable Notepad++ keeps its config next to the exe; keep the WebView2
  // profile there too. Otherwise use local (non-roaming) app data.
  const std::wstring nppDir = ModuleDirectory(nullptr);
  const bool portable = !configDir_.empty() && StartsWithNoCase(configDir_, nppDir + L"\\");
  environment_.SetUserDataFolder(portable ? configDir_ + L"\\MarkdownReader\\WebView2"
                                          : KnownFolder(FOLDERID_LocalAppData) + L"\\MarkdownReader\\WebView2");

  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = &Plugin::MessageWindowProc;
  wc.hInstance = module_;
  wc.lpszClassName = kMessageWindowClass;
  ::RegisterClassExW(&wc);
  messageWindow_ = ::CreateWindowExW(0, kMessageWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, module_, nullptr);

  auto set = [this](Command index, const wchar_t* name, PFUNCPLUGINCMD function, bool checked = false) {
    wcscpy_s(commands_[index]._itemName, name);
    commands_[index]._pFunc = function;
    commands_[index]._init2Check = checked;
    commands_[index]._pShKey = nullptr;
  };
  set(kCmdPanel, L"Toggle Preview Panel", CmdTogglePanel);
  set(kCmdReading, L"Toggle Reading Mode", CmdToggleReading);
  set(kCmdSep1, L"", nullptr);
  set(kCmdSyncScroll, L"Sync Scrolling", CmdToggleSyncScroll, settings_.syncScroll);
  set(kCmdReadingByDefault, L"Open Markdown in Reading Mode", CmdToggleReadingByDefault, settings_.readingModeByDefault);
  set(kCmdSep2, L"", nullptr);
  set(kCmdExportHtml, L"Export to HTML...", CmdExportHtml);
  set(kCmdExportPdf, L"Export to PDF...", CmdExportPdf);
  set(kCmdPrint, L"Print...", CmdPrint);
  set(kCmdCopyHtml, L"Copy Rendered HTML", CmdCopyHtml);
  set(kCmdSep3, L"", nullptr);
  set(kCmdSettings, L"Settings...", CmdSettings);
  set(kCmdAbout, L"About MarkdownReader", CmdAbout);
}

FuncItem* Plugin::Commands(int* count) {
  *count = kCommandCount;
  return commands_.data();
}

void Plugin::OnNotification(SCNotification* notification) {
  try {
    const auto& header = notification->nmhdr;
    if (header.hwndFrom == npp::Window()) {
      switch (header.code) {
        case NPPN_READY: OnReady(); break;
        case NPPN_SHUTDOWN: OnShutdown(); break;
        case NPPN_BUFFERACTIVATED:
        case NPPN_LANGCHANGED:
        case NPPN_FILERENAMED: OnActiveBufferChanged(); break;
        case NPPN_FILESAVED: OnFileSaved(header.idFrom); break;
        case NPPN_FILECLOSED: buffers_.erase(header.idFrom); break;
        case NPPN_DARKMODECHANGED: OnThemeChanged(); break;
        case NPPN_GLOBALMODIFIED: ScheduleRender(); break;  // Replace All sends no SCN_MODIFIED
        default: break;
      }
    } else if (ready_) {
      const HWND scintilla = static_cast<HWND>(header.hwndFrom);
      switch (header.code) {
        case SCN_MODIFIED:
          if (notification->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) OnDocumentModified(scintilla);
          break;
        case SCN_UPDATEUI:
          if (notification->updated & SC_UPDATE_V_SCROLL) OnEditorScrolled(scintilla);
          break;
        default:
          break;
      }
    }
  } catch (...) {
    // Never let an exception unwind into Notepad++.
  }
}

void Plugin::OnReady() {
  ready_ = true;
  // The panel may have been restored (and registered) before Notepad++ was
  // ready; the WebView is only created now so startup stays fast.
  if (PanelOpen()) EnsurePanelHost();
  OnActiveBufferChanged();
}

void Plugin::OnShutdown() {
  ready_ = false;
  if (messageWindow_) ::KillTimer(messageWindow_, kRenderTimer);
  if (panelHost_) panelHost_->Close();
  for (auto& view : reading_) {
    if (view.host) view.host->Close();
    view.overlay.Destroy();
  }
  environment_.Reset();
  deferred_.clear();
}

// ---- documents ---------------------------------------------------------------------------

bool Plugin::IsMarkdown(UINT_PTR bufferId) {
  if (!bufferId) return false;
  if (buffers_[bufferId].forced) return true;
  if (HasMarkdownExtension(npp::BufferPath(bufferId), extensions_)) return true;
  // The language name of a UDL is only available for the current buffer.
  return bufferId == npp::CurrentBufferId() && npp::CurrentLangType() == L_USER &&
         LanguageNameIsMarkdown(npp::LanguageName(L_USER));
}

void Plugin::OnActiveBufferChanged() {
  if (!ready_) return;
  const int currentView = npp::CurrentView();
  const UINT_PTR current = npp::CurrentBufferId();
  const bool currentIsMarkdown = IsMarkdown(current);

  BufferState& state = buffers_[current];
  if (!state.seen) {
    state.seen = true;
    state.reading = currentIsMarkdown && settings_.readingModeByDefault;
  }
  if (!currentIsMarkdown) state.reading = false;

  // Both views: moving a document between views changes the other view too.
  for (int view = 0; view < 2; ++view) {
    const UINT_PTR id = view == currentView ? current : npp::BufferInView(view);
    const auto found = buffers_.find(id);
    const bool active = id && found != buffers_.end() && found->second.reading;
    UpdateReadingView(view, id, active);
  }

  if (currentIsMarkdown && !state.reading && settings_.autoOpenPanel && !panelDismissed_ && !PanelOpen()) {
    ShowPanel(true);
  }
  RefreshPanel(false);
  UpdateMenuChecks();
}

void Plugin::OnDocumentModified(HWND scintilla) {
  if (scintilla == npp::CurrentScintilla()) ScheduleRender();
}

void Plugin::ScheduleRender() {
  if (!messageWindow_) return;
  const size_t length = npp::DocumentLength(npp::CurrentScintilla());
  const UINT delay = length < 100'000 ? 150 : length < 1'000'000 ? 400 : 1000;
  ::SetTimer(messageWindow_, kRenderTimer, delay, nullptr);  // re-arming restarts the delay
}

void Plugin::RenderActiveDocument() {
  const UINT_PTR id = npp::CurrentBufferId();
  if (!IsMarkdown(id)) return;
  const HWND scintilla = npp::CurrentScintilla();
  if (PanelOpen() && panelHost_ && panelHost_->IsPageReady() && panelBufferId_ == id) {
    RenderTo(*panelHost_, id, scintilla, false);
  }
  auto& view = reading_[npp::CurrentView()];
  if (view.overlay.IsActive() && view.host && view.host->IsPageReady() && view.bufferId == id) {
    RenderTo(*view.host, id, scintilla, false);
  }
}

void Plugin::RenderTo(WebViewHost& host, UINT_PTR bufferId, HWND scintilla, bool reset) {
  const std::string text = CodePageToUtf8(npp::DocumentBytes(scintilla), npp::CodePage(scintilla));
  RenderRequest request;
  request.seq = ++renderSeq_;
  request.textUtf8 = text;
  request.baseHref = DocBaseUrlForDirectory(DirectoryOf(npp::BufferPath(bufferId)));
  request.reset = reset;
  if (reset) request.scrollLine = npp::FirstVisibleDocLine(scintilla);
  host.Post(BuildRenderMessage(request));
}

void Plugin::OnEditorScrolled(HWND scintilla) {
  if (!settings_.syncScroll || scintilla != npp::CurrentScintilla()) return;
  if (::GetTickCount64() < ignoreEditorScrollUntil_) return;  // echo of a scroll the panel asked for
  if (!PanelOpen() || !panelHost_ || panelBufferId_ != npp::CurrentBufferId()) return;
  panelHost_->Post(BuildScrollMessage(npp::FirstVisibleDocLine(scintilla)));
}

void Plugin::OnFileSaved(UINT_PTR bufferId) {
  const std::wstring path = npp::BufferPath(bufferId);
  const std::wstring css = settings_.customCss.empty() ? configDir_ + L"\\MarkdownReader\\custom.css" : settings_.customCss;
  if (ToLower(path) == ToLower(iniPath_)) {
    ReloadSettings();
  } else if (ToLower(path) == ToLower(css)) {
    PushSettings();
  }
}

// ---- panel ---------------------------------------------------------------------------------

bool Plugin::PanelOpen() const {
  return panel_.isCreated() && !panel_.isClosed();
}

void Plugin::ShowPanel(bool show) {
  if (show) {
    if (!panel_.isCreated()) {
      panel_.init(module_, npp::Window());
      panel_.SetEvents({
          .resized = [this] { if (panelHost_) panelHost_->FitToParent(); },
          .moved = [this] { if (panelHost_) panelHost_->NotifyParentMoved(); },
          .closed = [this] {
            panelDismissed_ = true;
            UpdateMenuChecks();
          },
          .focused = [this] { if (panelHost_) panelHost_->Focus(); },
      });
      tTbData data{};
      panel_.create(&data);
      data.uMask = DWS_DF_CONT_RIGHT;
      data.pszModuleName = panel_.getPluginFileName();
      data.dlgID = kCmdPanel;
      ::SendMessage(npp::Window(), NPPM_DMMREGASDCKDLG, 0, reinterpret_cast<LPARAM>(&data));
    }
    panel_.display(true);
    panel_.setClosed(false);
    panelDismissed_ = false;
    if (ready_) {
      EnsurePanelHost();
      RefreshPanel(true);
    }
  } else if (panel_.isCreated()) {
    panel_.display(false);
    panel_.setClosed(true);
    panelDismissed_ = true;
  }
  if (ready_) UpdateMenuChecks();
}

void Plugin::EnsurePanelHost() {
  if (!panelHost_) panelHost_ = std::make_unique<WebViewHost>(*this);
  if (!panelHost_->IsCreated() && !panelHost_->IsCreating()) panelHost_->Create(panel_.getHSelf(), environment_, HostOptions());
}

void Plugin::RefreshPanel(bool reset) {
  if (!PanelOpen() || !panelHost_ || !panelHost_->IsPageReady()) return;
  const UINT_PTR current = npp::CurrentBufferId();
  if (IsMarkdown(current)) {
    RenderTo(*panelHost_, current, npp::CurrentScintilla(), reset || panelBufferId_ != current);
    panelBufferId_ = current;
  } else if (reset || panelBufferId_ != 0) {
    RenderRequest request;
    request.seq = ++renderSeq_;
    request.textUtf8 = kPlaceholder;
    request.baseHref = DocBaseUrlForDirectory(L"");
    request.reset = true;
    panelHost_->Post(BuildRenderMessage(request));
    panelBufferId_ = 0;
  }
}

// ---- reading mode -------------------------------------------------------------------------

void Plugin::EnsureReadingView(int view) {
  auto& reading = reading_[view];
  if (!reading.overlay.IsCreated()) {
    reading.overlay.Create(module_, npp::ScintillaForView(view),
                           {
                               .resized = [this, view] { if (reading_[view].host) reading_[view].host->FitToParent(); },
                               .focused = [this, view] { if (reading_[view].host) reading_[view].host->Focus(); },
                           });
    reading.overlay.SetBackground(Background());
  }
  if (!reading.host) reading.host = std::make_unique<WebViewHost>(*this);
  if (reading.overlay.IsCreated() && !reading.host->IsCreated() && !reading.host->IsCreating()) {
    reading.host->Create(reading.overlay.Hwnd(), environment_, HostOptions());
  }
}

void Plugin::UpdateReadingView(int view, UINT_PTR bufferId, bool active) {
  auto& reading = reading_[view];
  if (active) {
    EnsureReadingView(view);
    const bool changed = reading.bufferId != bufferId || !reading.overlay.IsActive();
    reading.bufferId = bufferId;
    reading.overlay.SetActive(true);
    reading.host->SetVisible(true);
    reading.host->FitToParent();
    if (changed && reading.host->IsPageReady()) RenderTo(*reading.host, bufferId, npp::ScintillaForView(view), true);
    if (view == npp::CurrentView()) reading.host->Focus();
  } else if (reading.overlay.IsCreated()) {
    reading.overlay.SetActive(false);
    if (reading.host) reading.host->SetVisible(false);
  }
}

// ---- commands ------------------------------------------------------------------------------

void Plugin::TogglePanel() {
  ShowPanel(!PanelOpen());
}

void Plugin::ToggleReadingMode() {
  if (!ready_) return;
  const int view = npp::CurrentView();
  const UINT_PTR id = npp::CurrentBufferId();
  BufferState& state = buffers_[id];
  const bool enable = !state.reading;
  if (enable && !IsMarkdown(id)) state.forced = true;  // an explicit request renders any document
  state.reading = enable;
  UpdateReadingView(view, id, enable);
  if (!enable) ::SetFocus(npp::ScintillaForView(view));
  RefreshPanel(false);
  UpdateMenuChecks();
}

void Plugin::ToggleSyncScroll() {
  settings_.syncScroll = !settings_.syncScroll;
  settings_.Save(iniPath_);
  PushSettings();
  UpdateMenuChecks();
}

void Plugin::ToggleReadingByDefault() {
  settings_.readingModeByDefault = !settings_.readingModeByDefault;
  settings_.Save(iniPath_);
  UpdateMenuChecks();
}

WebViewHost* Plugin::HostShowingActiveDocument() {
  const UINT_PTR current = npp::CurrentBufferId();
  auto& view = reading_[npp::CurrentView()];
  if (view.overlay.IsActive() && view.host && view.host->IsPageReady() && view.bufferId == current) return view.host.get();
  if (PanelOpen() && panelHost_ && panelHost_->IsPageReady() && panelBufferId_ == current && current) return panelHost_.get();
  return nullptr;
}

void Plugin::RequestExport(ExportKind kind) {
  WebViewHost* host = HostShowingActiveDocument();
  if (!host) {
    ::MessageBoxW(npp::Window(), L"Open the preview panel or reading mode for this document first.", kPluginName,
                  MB_OK | MB_ICONINFORMATION);
    return;
  }
  const int id = ++exportSeq_;
  pendingExports_[id] = kind;
  host->Post(BuildExportMessage(id, /*dark=*/false));
}

void Plugin::ExportHtml() {
  RequestExport(ExportKind::SaveHtml);
}

void Plugin::CopyHtml() {
  RequestExport(ExportKind::CopyHtml);
}

void Plugin::FinishExport(int id, const std::string& html) {
  const auto found = pendingExports_.find(id);
  if (found == pendingExports_.end()) return;
  const ExportKind kind = found->second;
  pendingExports_.erase(found);

  const std::wstring source = npp::BufferPath(npp::CurrentBufferId());
  if (kind == ExportKind::SaveHtml) {
    const std::wstring path = AskSavePath(npp::Window(), L"HTML document", L"*.html;*.htm", L"html", DirectoryOf(source),
                                          FileNameWithoutExtension(source) + L".html");
    if (path.empty()) return;
    if (!WriteUtf8File(path, html)) {
      ::MessageBoxW(npp::Window(), (L"Could not write " + path).c_str(), kPluginName, MB_OK | MB_ICONERROR);
    }
  } else {
    const HWND scintilla = npp::CurrentScintilla();
    const std::wstring plain = Utf8ToWide(CodePageToUtf8(npp::DocumentBytes(scintilla), npp::CodePage(scintilla)));
    SetClipboard(npp::Window(), BuildClipboardHtml(html), plain);
  }
}

void Plugin::ExportPdf() {
  WebViewHost* host = HostShowingActiveDocument();
  auto webview7 = host ? wil::try_com_query<ICoreWebView2_7>(host->WebView()) : nullptr;
  if (!webview7) {
    ::MessageBoxW(npp::Window(), L"Open the preview panel or reading mode for this document first.", kPluginName,
                  MB_OK | MB_ICONINFORMATION);
    return;
  }
  const std::wstring source = npp::BufferPath(npp::CurrentBufferId());
  const std::wstring path = AskSavePath(npp::Window(), L"PDF document", L"*.pdf", L"pdf", DirectoryOf(source),
                                        FileNameWithoutExtension(source) + L".pdf");
  if (path.empty()) return;

  wil::com_ptr<ICoreWebView2PrintSettings> printSettings;
  if (auto environment6 = wil::try_com_query<ICoreWebView2Environment6>(host->Environment())) {
    if (SUCCEEDED(environment6->CreatePrintSettings(&printSettings))) {
      printSettings->put_ShouldPrintBackgrounds(TRUE);
      printSettings->put_ShouldPrintHeaderAndFooter(FALSE);
    }
  }
  webview7->PrintToPdf(path.c_str(), printSettings.get(),
                       Microsoft::WRL::Callback<ICoreWebView2PrintToPdfCompletedHandler>([path](HRESULT result, BOOL ok) -> HRESULT {
                         if (FAILED(result) || !ok) {
                           const std::wstring message = L"Could not export " + path;
                           // Not from inside the handler: a modal loop here is unsupported.
                           Plugin::Instance().Defer([message] {
                             ::MessageBoxW(npp::Window(), message.c_str(), kPluginName, MB_OK | MB_ICONERROR);
                           });
                         }
                         return S_OK;
                       }).Get());
}

void Plugin::Print() {
  WebViewHost* host = HostShowingActiveDocument();
  auto webview16 = host ? wil::try_com_query<ICoreWebView2_16>(host->WebView()) : nullptr;
  if (!webview16) {
    ::MessageBoxW(npp::Window(), L"Open the preview panel or reading mode for this document first.", kPluginName,
                  MB_OK | MB_ICONINFORMATION);
    return;
  }
  webview16->ShowPrintUI(COREWEBVIEW2_PRINT_DIALOG_KIND_BROWSER);
}

void Plugin::OpenSettings() {
  if (!FileExists(iniPath_)) {
    ::SHCreateDirectoryExW(nullptr, configDir_.c_str(), nullptr);
    settings_.Save(iniPath_);
  }
  npp::OpenFile(iniPath_);
}

void Plugin::ShowAbout() {
  const std::wstring runtime = WebViewEnv::RuntimeVersion();
  const std::wstring text = std::wstring(L"MarkdownReader ") + MDR_VERSION_WSTRING +
                            L"\nMarkdown preview and reading mode for Notepad++.\n\nWebView2 runtime: " +
                            (runtime.empty() ? L"not installed" : runtime) +
                            L"\nSettings: " + iniPath_ + L"\n\nLicense: GPL-3.0-or-later";
  ::MessageBoxW(npp::Window(), text.c_str(), L"About MarkdownReader", MB_OK | MB_ICONINFORMATION);
}

// ---- settings and theme -----------------------------------------------------------------------

bool Plugin::IsDark() const {
  switch (settings_.theme) {
    case ThemeSetting::Light: return false;
    case ThemeSetting::Dark: return true;
    default: return npp::IsDarkModeEnabled();
  }
}

COLORREF Plugin::Background() const {
  return IsDark() ? kDarkBackground : kLightBackground;
}

WebViewHost::Options Plugin::HostOptions() const {
  WebViewHost::Options options;
  options.webRoot = webRoot_;
  options.keyTarget = npp::Window();
  options.devTools = settings_.devTools;
  options.background = Background();
  return options;
}

std::wstring Plugin::SettingsMessage() const {
  PageSettings page;
  page.dark = IsDark();
  page.allowRemoteImages = settings_.allowRemoteImages;
  page.mermaid = settings_.mermaid;
  page.syncScroll = settings_.syncScroll;
  const std::wstring css = settings_.customCss.empty() ? configDir_ + L"\\MarkdownReader\\custom.css" : settings_.customCss;
  page.customCssUtf8 = ReadSmallUtf8File(css, 1024 * 1024);
  return BuildSettingsMessage(page);
}

void Plugin::PushSettings() {
  const std::wstring message = SettingsMessage();
  const bool dark = IsDark();
  const COLORREF background = Background();
  auto apply = [&](WebViewHost* host) {
    if (!host) return;
    host->SetTheme(dark, background);
    host->Post(message);
  };
  apply(panelHost_.get());
  for (auto& view : reading_) {
    apply(view.host.get());
    if (view.overlay.IsCreated()) view.overlay.SetBackground(background);
  }
}

void Plugin::ReloadSettings() {
  settings_ = Settings::Load(iniPath_);
  extensions_ = ParseExtensionList(settings_.extensions);
  PushSettings();
  OnActiveBufferChanged();
}

void Plugin::OnThemeChanged() {
  PushSettings();
}

void Plugin::UpdateMenuChecks() {
  const auto found = buffers_.find(npp::CurrentBufferId());
  npp::SetMenuCheck(commands_[kCmdPanel]._cmdID, PanelOpen());
  npp::SetMenuCheck(commands_[kCmdReading]._cmdID, found != buffers_.end() && found->second.reading);
  npp::SetMenuCheck(commands_[kCmdSyncScroll]._cmdID, settings_.syncScroll);
  npp::SetMenuCheck(commands_[kCmdReadingByDefault]._cmdID, settings_.readingModeByDefault);
}

// ---- page events -------------------------------------------------------------------------------

int Plugin::ViewOf(const WebViewHost& host) const {
  for (int view = 0; view < 2; ++view) {
    if (reading_[view].host.get() == &host) return view;
  }
  return -1;
}

void Plugin::OnPageReady(WebViewHost& host) {
  host.SetTheme(IsDark(), Background());
  host.Post(SettingsMessage());
  const int view = ViewOf(host);
  if (view < 0) {
    panelBufferId_ = static_cast<UINT_PTR>(-1);  // force a full render
    RefreshPanel(true);
  } else if (reading_[view].overlay.IsActive()) {
    RenderTo(host, reading_[view].bufferId, npp::ScintillaForView(view), true);
  }
}

void Plugin::OnPageMessage(WebViewHost& host, PageMessage message) {
  const int view = ViewOf(host);
  switch (message.type) {
    case PageMessage::Type::Reveal:
      if (view < 0) {
        if (!settings_.syncScroll || panelBufferId_ != npp::CurrentBufferId()) return;
        ignoreEditorScrollUntil_ = ::GetTickCount64() + 250;
        npp::ScrollToDocLine(npp::CurrentScintilla(), message.line);
      } else {
        // Keep the hidden editor at the reader's position, so leaving
        // reading mode lands in the same place.
        npp::ScrollToDocLine(npp::ScintillaForView(view), message.line);
      }
      break;

    case PageMessage::Type::GotoLine:
      Defer([this, view, line = message.line] {
        const int targetView = view < 0 ? npp::CurrentView() : view;
        if (view >= 0) {
          buffers_[reading_[view].bufferId].reading = false;
          UpdateReadingView(view, reading_[view].bufferId, false);
          UpdateMenuChecks();
        }
        const HWND scintilla = npp::ScintillaForView(targetView);
        npp::GotoDocLine(scintilla, line);
        ::SetFocus(scintilla);
      });
      break;

    case PageMessage::Type::Link:
      Defer([this, url = std::move(message.url)] { OpenLink(url); });
      break;

    case PageMessage::Type::ExportResult:
      Defer([this, id = message.id, html = std::move(message.html)] { FinishExport(id, html); });
      break;

    default:
      break;
  }
}

void Plugin::OnFocusLeaving(WebViewHost& host) {
  // Tab out of the panel goes back to the editor. In reading mode the
  // editor is hidden, so focus stays in the reading view.
  if (ViewOf(host) < 0) Defer([] { ::SetFocus(npp::CurrentScintilla()); });
}

void Plugin::OnHostFailed(WebViewHost& host, const std::wstring& what, HRESULT result) {
  WebViewHost* failed = &host;
  Defer([this, failed, what, result] {
    failed->Close();  // recreated the next time it is needed
    ReportError(what, result);
  });
}

void Plugin::OpenLink(const std::wstring& url) {
  const LinkAction action = ClassifyLink(url);
  switch (action.kind) {
    case LinkAction::Kind::OpenExternal:
      ::ShellExecuteW(npp::Window(), L"open", action.target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      break;
    case LinkAction::Kind::OpenInEditor:
      if (FileExists(action.target)) {
        npp::OpenFile(action.target);
      } else {
        ::MessageBeep(MB_ICONWARNING);
      }
      break;
    case LinkAction::Kind::Block:
      ::OutputDebugStringW((L"MarkdownReader: blocked link (" + action.reason + L"): " + url + L"\n").c_str());
      ::MessageBeep(MB_ICONWARNING);
      break;
  }
}

void Plugin::ReportError(const std::wstring& what, HRESULT result) {
  wchar_t code[16];
  swprintf_s(code, L"0x%08X", static_cast<unsigned>(result));
  ::OutputDebugStringW((L"MarkdownReader: " + what + L" (" + code + L")\n").c_str());
  if (errorShown_) return;
  errorShown_ = true;
  std::wstring text = what + L" (error " + code + L").";
  if (WebViewEnv::RuntimeVersion().empty()) {
    text += L"\n\nMarkdownReader needs the Microsoft Edge WebView2 Runtime, which is not installed. "
            L"Download it from https://developer.microsoft.com/microsoft-edge/webview2/";
  }
  ::MessageBoxW(npp::Window(), text.c_str(), kPluginName, MB_OK | MB_ICONWARNING);
}

// ---- deferred work ----------------------------------------------------------------------------------

// Work triggered from WebView2 event handlers (opening files, dialogs,
// ShellExecute) runs later from the message loop: re-entering a modal loop
// inside those handlers is not supported.
void Plugin::Defer(std::function<void()> task) {
  deferred_.push_back(std::move(task));
  if (messageWindow_) ::PostMessageW(messageWindow_, kMsgRunDeferred, 0, 0);
}

LRESULT CALLBACK Plugin::MessageWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  Plugin& self = Instance();
  try {
    switch (message) {
      case kMsgRunDeferred: {
        auto tasks = std::move(self.deferred_);
        self.deferred_.clear();
        for (auto& task : tasks) task();
        return 0;
      }
      case WM_TIMER:
        if (wParam == kRenderTimer) {
          ::KillTimer(hwnd, kRenderTimer);
          if (self.ready_) self.RenderActiveDocument();
          return 0;
        }
        break;
      default:
        break;
    }
  } catch (...) {
  }
  return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

namespace {

void CmdTogglePanel() { Plugin::Instance().TogglePanel(); }
void CmdToggleReading() { Plugin::Instance().ToggleReadingMode(); }
void CmdToggleSyncScroll() { Plugin::Instance().ToggleSyncScroll(); }
void CmdToggleReadingByDefault() { Plugin::Instance().ToggleReadingByDefault(); }
void CmdExportHtml() { Plugin::Instance().ExportHtml(); }
void CmdExportPdf() { Plugin::Instance().ExportPdf(); }
void CmdPrint() { Plugin::Instance().Print(); }
void CmdCopyHtml() { Plugin::Instance().CopyHtml(); }
void CmdSettings() { Plugin::Instance().OpenSettings(); }
void CmdAbout() { Plugin::Instance().ShowAbout(); }

}  // namespace

}  // namespace mdr
