#pragma once

#include <functional>

#include "DockingFeature/DockingDlgInterface.h"
#include "resource.h"

namespace mdr {

// The dockable "Markdown Preview" panel. It is an empty dialog; the plugin
// puts a WebViewHost in it and is told about size, docking and close events.
class PreviewPanel : public DockingDlgInterface {
 public:
  struct Events {
    std::function<void()> resized;
    std::function<void()> moved;   // docked/floated: WebView must be told
    std::function<void()> closed;  // user closed it with the X
    std::function<void()> focused;
  };

  PreviewPanel() : DockingDlgInterface(IDD_PREVIEW_PANEL) {}
  void SetEvents(Events events) { events_ = std::move(events); }

 protected:
  INT_PTR CALLBACK run_dlgProc(UINT message, WPARAM wParam, LPARAM lParam) override;

 private:
  Events events_;
};

}  // namespace mdr
