#include "PreviewPanel.h"

namespace mdr {

INT_PTR CALLBACK PreviewPanel::run_dlgProc(UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_SIZE:
      if (events_.resized) events_.resized();
      return TRUE;

    case WM_MOVE:
      if (events_.moved) events_.moved();
      break;

    case WM_SETFOCUS:
      if (events_.focused) events_.focused();
      return TRUE;

    case WM_NOTIFY: {
      const auto* header = reinterpret_cast<const NMHDR*>(lParam);
      if (header->hwndFrom == _hParent) {
        switch (LOWORD(header->code)) {
          case DMN_CLOSE:
            setClosed(true);
            if (events_.closed) events_.closed();
            break;
          case DMN_FLOAT:
          case DMN_DOCK:
            if (events_.moved) events_.moved();
            break;
          default:
            break;
        }
      }
      break;
    }

    default:
      break;
  }
  return DockingDlgInterface::run_dlgProc(message, wParam, lParam);
}

}  // namespace mdr
