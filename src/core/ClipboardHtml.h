#pragma once

#include <string>
#include <string_view>

namespace mdr {

// Wraps a UTF-8 HTML document in the Windows "HTML Format" (CF_HTML)
// clipboard envelope. The fragment is the content of the first <main>
// element (the rendered document), or the whole body if there is none.
std::string BuildClipboardHtml(std::string_view html);

}  // namespace mdr
